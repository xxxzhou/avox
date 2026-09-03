#include "TorrentEngine.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/error.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "avox/module/LogHelper.hpp"

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <httplib.h>

namespace avox {

// 本文件成员方法大量使用libtorrent类型, 整文件可见(仅本cpp编译单元)
using namespace libtorrent;

namespace {
constexpr int64_t kMB = 1024 * 1024;

// 媒体扩展名优先匹配, 全不命中退回选最大文件
bool isMediaFile(const std::string& path) {
  static const char* exts[] = {".mp4", ".mkv", ".ts",  ".flv", ".webm", ".avi",
                               ".mov", ".m4v", ".mpg", ".mpeg", ".wmv", ".3gp"};
  auto dot = path.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = path.substr(dot);
  for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
  for (auto* e : exts) {
    if (ext == e) {
      return true;
    }
  }
  return false;
}

// 拆分tracker列表(分号/逗号/空白分隔)
void splitTrackers(const std::string& src, std::vector<std::string>& out) {
  std::string cur = "";
  for (char c : src) {
    if (c == ';' || c == ',' || c == ' ' || c == '\t' || c == '\r' ||
        c == '\n') {
      if (!cur.empty()) {
        out.push_back(cur);
        cur = "";
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) {
    out.push_back(cur);
  }
}

// info-hash 转16进制目录名
std::string hashDirName(const sha1_hash& hash) {
  const std::string bytes = hash.to_string();
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (unsigned char b : bytes) {
    ss << std::setw(2) << (int)b;
  }
  return ss.str();
}

// 会话基础参数: 监听端口/告警掩码/调优项; 限速按调用方配置
settings_pack baseSessionPack(const TorrentEngine::Config& cfg) {
  settings_pack pack;
  pack.set_str(settings_pack::listen_interfaces,
               "0.0.0.0:6881-6889,[::]:6881-6889");
  pack.set_int(settings_pack::alert_mask,
               alert::status_notification | alert::error_notification |
                   alert::storage_notification);
  // 全部 tracker/层级并行 announce, 缩短 peer 发现时间(默认逐 tier 串行)
  pack.set_bool(settings_pack::announce_to_all_trackers, true);
  pack.set_bool(settings_pack::announce_to_all_tiers, true);
  // 更大的连接池 + 连接爬坡加成(默认 200 连接, boost 默认 50), 起播 ramp 更快
  pack.set_int(settings_pack::connections_limit, 500);
  pack.set_int(settings_pack::torrent_connect_boost, 100);
  // 死peer快速回收(默认30s, ramp期半开槽位被僵尸连接占住拖慢爬坡)
  pack.set_int(settings_pack::peer_connect_timeout, 10);
  // 单种子流播场景禁自动管理限流(默认 active_downloads=3/limit=12 会节流)
  pack.set_int(settings_pack::active_downloads, -1);
  pack.set_int(settings_pack::active_seeds, -1);
  pack.set_int(settings_pack::active_limit, -1);
  pack.set_int(settings_pack::aio_threads, 8);
  // webseed(HTTP直链)多路下载: 每条 ws 连接的 HTTP 请求流水线加深(默认16,
  // 等价 aria2 分段并发对高延迟链路的吞吐提升); 多个 ws 镜像时允许更多连接
  pack.set_int(settings_pack::urlseed_pipeline_size, 64);
  pack.set_int(settings_pack::max_web_seed_connections, 8);
  // 并行新建连接上限放宽(默认较保守, 峰值 ramp 时半开连接排队拖慢爬坡)
  pack.set_int(settings_pack::half_open_limit, 200);
  // 显式补充DHT引导节点(默认集较瘦, 冷bootstrap更快找到邻居)
  pack.set_str(settings_pack::dht_bootstrap_nodes,
               "dht.libtorrent.org:25401,router.bittorrent.com:6881,"
               "router.utorrent.com:6881,router.bitcomet.com:6881,"
               "dht.transmissionbt.com:6881");
  // 局域网发现/NAT打通显式开启(默认已开, 显式化防上游默认变化)
  pack.set_bool(settings_pack::enable_lsd, true);
  pack.set_bool(settings_pack::enable_upnp, true);
  pack.set_bool(settings_pack::enable_natpmp, true);
  // webseed域名解析失败重试(镜像DNS偶发抖动时不再整场放弃)
  pack.set_bool(settings_pack::web_seed_name_lookup_retry, true);
  // alert队列加大(500连接并发事件多, 防状态/错误告警被挤丢)
  pack.set_int(settings_pack::alert_queue_size, 10000);
  if (cfg.maxDownloadSpeedKB > 0) {
    pack.set_int(settings_pack::download_rate_limit,
                 (int)(cfg.maxDownloadSpeedKB * 1024));
  }
  return pack;
}

// 会话状态持久化(DHT路由表): 进程重启免DHT冷bootstrap(实测2~5s)。
// 存系统temp/avplay_torrent(不随业务cacheDir被清理), 只存dht state——
// settings不存(防旧存档覆盖新调优参数, 干扰A/B对比)
std::string sessionStatePath() {
  std::error_code fec;
  std::filesystem::path base = std::filesystem::temp_directory_path(fec);
  if (fec) {
    base = std::filesystem::path(".");
  }
  base /= "avplay_torrent";
  std::error_code cec;
  std::filesystem::create_directories(base, cec);
  return (base / "session.state").generic_string();
}

std::mutex g_sessionStateMutex;
int64_t g_lastSessionSaveTick = 0;
int64_t g_sessionStartTick = 0;
// 节流落盘会话状态(onRunTask每200ms调一次): 会话运行满45s才首次保存——
// 太早存是空路由表, 白占存档还挡了后续有效保存
void saveSessionStateThrottled(const std::shared_ptr<libtorrent::session>& s) {
  if (!s) {
    return;
  }
  std::lock_guard<std::mutex> lk(g_sessionStateMutex);
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count();
  if (!g_sessionStartTick) {
    g_sessionStartTick = now;
  }
  if (now - g_sessionStartTick < 45000 ||
      (g_lastSessionSaveTick && now - g_lastSessionSaveTick < 120000)) {
    return;
  }
  g_lastSessionSaveTick = now;
  try {
    entry e;
    s->save_state(e, session::save_dht_state);
    std::vector<char> buf;
    bencode(std::back_inserter(buf), e);
    if (buf.empty()) {
      return;
    }
    std::ofstream out(sessionStatePath(), std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
      out.write(buf.data(), (std::streamsize)buf.size());
    }
  } catch (const std::exception&) {
  }
}

// 进程级共享会话(播放与探测共用): 热DHT/peer/路由缓存让探测→播放全链路
// 免掉每次冷启动 bootstrap(2~5s), 探测期发现的peer直接供起播使用。
// 会话随进程存活(空转近零开销)。同一info-hash多引擎并发存活由进程级引用
// 计数保护(g_torrentRefs), 摘种子只在引用归零时执行。引擎逻辑全部轮询
// 句柄状态(have_piece/torrent_file), 不依赖alert驱动; alert是单队列pop,
// 并发引擎互抢只影响日志(见onRunTask按句柄过滤)。
std::shared_ptr<libtorrent::session> sharedTorrentSession(
    const TorrentEngine::Config& cfg) {
  static std::mutex mtx;
  static std::shared_ptr<libtorrent::session> s;
  std::lock_guard<std::mutex> lk(mtx);
  if (!s) {
    s = std::make_shared<libtorrent::session>(baseSessionPack(cfg));
    // 恢复上次会话的DHT路由(存档坏/版本不符则忽略, 走冷bootstrap)
    try {
      std::string path = sessionStatePath();
      std::error_code fec;
      if (std::filesystem::exists(path, fec) && !fec) {
        std::ifstream in(path, std::ios::binary);
        if (in) {
          std::vector<char> buf(
              (std::istreambuf_iterator<char>(in)),
              std::istreambuf_iterator<char>());
          if (!buf.empty()) {
            bdecode_node node;
            error_code dec;
            // bdecode成功返回0(负值/非0为解析错误)
            if (bdecode(buf.data(), buf.data() + buf.size(), node, dec) == 0 &&
                !dec) {
              s->load_state(node, session::save_dht_state);
              LOGFLF(LogLevel::info,
                     "[torrent] dht state loaded:", path,
                     " bytes:", (double)buf.size());
            }
          }
        }
      }
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[torrent] dht state load failed:", e.what());
    }
  } else {
    // 限速是会话级配置: 每次引擎打开按当前配置刷新(0=不限)
    settings_pack lim;
    lim.set_int(settings_pack::download_rate_limit,
                cfg.maxDownloadSpeedKB > 0
                    ? (int)(cfg.maxDownloadSpeedKB * 1024)
                    : 0);
    s->apply_settings(lim);
  }
  return s;
}

// 公共 tracker 兜底: 磁力往往只带一两个 tracker, 某个不通时 peer 发现全靠
// DHT 慢路径; 追加多路并行 announce(去重)显著缩短元数据等待
constexpr const char* kFallbackTrackers[] = {
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://tracker.openbittorrent.com:6969/announce",
    "udp://exodus.desync.com:6969/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://open.demonii.com:1337/announce",
    "udp://tracker.tiny-vps.com:6969/announce",
};

// 上层 probe/open 有并发开同一磁力的场景(实测同帧双引擎), 共享会话下并发
// add_torrent 同一种子会卡死在 libtorrent 内部, 串行化后第二个自然命中
// duplicate_torrent 走句柄复用
std::mutex g_sessionAddMutex;

// 进程级种子引用计数(共享会话): 同一info-hash可有探测/播放多引擎并发持句柄,
// 摘种子只在引用归零时执行。旧实现"探测私有会话"每次冷启动DHT慢, 直接复用
// 共享会话又会因probe退出摘种子误杀在播引擎(实测), 引用计数同时解决两头
std::mutex g_torrentRefMutex;
std::map<std::string, int32_t> g_torrentRefs;
// 登记引用(add锁内调用, 与并发add/duplicate判定串行化)
void torrentRefAdd(const std::string& hash) {
  std::lock_guard<std::mutex> lk(g_torrentRefMutex);
  ++g_torrentRefs[hash];
}
// 释放引用, 返回是否归零(归零者负责摘种子)
bool torrentRefRelease(const std::string& hash) {
  std::lock_guard<std::mutex> lk(g_torrentRefMutex);
  auto it = g_torrentRefs.find(hash);
  if (it == g_torrentRefs.end()) {
    return false;
  }
  bool last = --it->second <= 0;
  if (last) {
    g_torrentRefs.erase(it);
  }
  return last;
}

// 缓存LRU淘汰: baseDir下40位hex目录按最近写入时间升序删除, 直到总占用
// (含当前在用目录, 但其不可删)降至maxBytes内; 在播/在探引用的目录跳过
void evictCacheLRU(const std::string& baseDir, const std::string& keepHash,
                   int64_t maxBytes) {
  namespace fs = std::filesystem;
  if (maxBytes <= 0 || baseDir.empty()) {
    return;
  }
  std::error_code ec;
  struct Entry {
    std::string hash;
    uint64_t bytes = 0;
    int64_t stamp = 0;
  };
  uint64_t total = 0;
  std::vector<Entry> entries;
  for (fs::directory_iterator it(baseDir, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (!it->is_directory(ec)) {
      continue;
    }
    std::string name = it->path().filename().generic_string();
    if (name.size() != 40) {
      continue;
    }
    Entry e;
    e.hash = name;
    for (fs::recursive_directory_iterator fit(
             it->path(), fs::directory_options::skip_permission_denied, ec),
         fend;
         !ec && fit != fend; fit.increment(ec)) {
      std::error_code fec;
      if (!fit->is_regular_file(fec) || fec) {
        continue;
      }
      e.bytes += (uint64_t)fit->file_size(fec);
      int64_t mt = (int64_t)fit->last_write_time(fec).time_since_epoch().count();
      e.stamp = std::max(e.stamp, mt);
    }
    ec.clear();
    total += e.bytes;
    if (name != keepHash) {
      entries.push_back(std::move(e));
    }
  }
  if (entries.empty() || total <= (uint64_t)maxBytes) {
    return;
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.stamp < b.stamp; });
  for (const auto& e : entries) {
    if (total <= (uint64_t)maxBytes) {
      break;
    }
    if (g_torrentRefs.count(e.hash)) {
      continue;
    }
    fs::remove_all(fs::path(baseDir) / e.hash, ec);
    LOGFLF(LogLevel::info, "[torrent] cache evict:", e.hash,
           " freed(MB):", (double)(e.bytes / kMB));
    total -= e.bytes;
  }
}
}  // namespace

TorrentEngine::TorrentEngine() {}

// 析构在完整类型可见处定义(session/handle的unique_ptr删除器)
TorrentEngine::~TorrentEngine() { shutdown(); }

// 探测: 只等元数据建文件列表, 不选文件不下载。元数据顺带落盘,
// 同 cacheDir 的 start() 命中缓存后免 BEP-9 等待。
bool TorrentEngine::probe(const std::string& url, const Config& cfg,
                          std::string* errMsg) {
  config = cfg;
  auto fail = [&](const std::string& msg) {
    LOGFLF(LogLevel::error, "[torrent] probe failed:", msg, " url:", url);
    if (errMsg) *errMsg = msg;
    return false;
  };
  if (running()) {
    return fail("engine already started");
  }
  probeMode = true;
  if (!waitForTorrent(url, errMsg)) {
    shutdown();
    return false;
  }
  collectFileList();
  saveMetadata();
  LOGFLF(LogLevel::info, "[torrent] probe ok files:", (double)fileList.size(),
         " name:", torrentName, " total(MB):", (double)(totalSize / kMB));
  // 结果已拷贝进纯值成员, 即刻关会话(getter 不再依赖 libtorrent)
  shutdown();
  return true;
}

bool TorrentEngine::start(const std::string& url, const Config& cfg,
                          std::string* errMsg) {
  config = cfg;
  auto fail = [&](const std::string& msg) {
    LOGFLF(LogLevel::error, "[torrent] start failed:", msg, " url:", url);
    if (errMsg) *errMsg = msg;
    return false;
  };
  if (running()) {
    return fail("engine already started");
  }
  probeMode = false;
  if (!waitForTorrent(url, errMsg)) {
    shutdown();
    return false;
  }
  if (!selectFile(errMsg)) {
    shutdown();
    return false;
  }
  LOGFLF(LogLevel::info, "[torrent] stage metadata_ready");
  LOGFLF(LogLevel::info, "[torrent] select file:", fileInfo.path,
         " size(MB):", (double)(fileInfo.size / kMB),
         " pieceLen:", pieceLen, " pieces:", totalPieces);
  // 首轮调度: 头部起播段(top+deadline)与尾部索引段(MP4 moov/MKV cues尾部场景)
  updatePlayhead(0, true);
  // 首2片落地后才返回: avformat探测第一步就是读文件头, 把下载ramp期的
  // 不确定性挡在open内(预算与元数据同级, 冷swarm场景可经option放宽)。
  // 注意必须在 fileIn.open 之前: 缓存快路径 waitMetadata 秒回时 libtorrent
  // 还没把目标文件分配出来, 先开文件必失败; 有片落地则文件必已创建。
  // 片序号按目标文件首片起算(多文件种子文件起点非种子起点)
  for (int32_t p = fileFirstPiece;
       p < fileFirstPiece + 2 && p <= fileLastPiece; ++p) {
    ReadResult r = ReadResult::ok;
    if (!waitForPiece(p, std::max<int32_t>(config.metaTimeoutMs, 30000),
                      nullptr)) {
      if (errMsg) *errMsg = "first piece download timeout(swarm无响应或网络受限)";
      return fail("first piece timeout");
    }
  }
  LOGFLF(LogLevel::info, "[torrent] stage head_ready");
  // 尾片: 目标文件末片(MP4 moov/MKV cues 在文件尾部), 不等齐就 open 会拿到
  // 截断索引, demux 只注册出视频轨 (实测 no audio track); 超时不阻塞(兼容
  // moov 在头部的 faststart 封装, 尾片只是普通数据)
  if (fileLastPiece > fileFirstPiece + 1) {
    if (!waitForPiece(fileLastPiece,
                      std::max<int32_t>(config.metaTimeoutMs, 30000),
                      nullptr)) {
      LOGFLF(LogLevel::warn, "[torrent] tail piece timeout, open anyway");
    }
  }
  LOGFLF(LogLevel::info, "[torrent] stage tail_done");
  // 打开目标文件供后续预读(此时首片已落地, 文件必然存在)
  fileIn.open(absFilePath, std::ios::binary);
  if (!fileIn.is_open()) {
    return fail("open target file failed: " + absFilePath);
  }
  LOGFLF(LogLevel::info, "[torrent] stage file_opened");
  return true;
}

// start/probe 共享前段: 会话建立 + makeParams + add_torrent + 等元数据。
// 失败返回 false, 由调用方 shutdown(此处不关, 保留现场日志)。
bool TorrentEngine::waitForTorrent(const std::string& url,
                                   std::string* errMsg) {
  // 播放与探测共用进程级会话(热DHT/peer缓存); 摘种子时机见shutdown引用计数
  session = sharedTorrentSession(config);
  // 状态观测线程(startTask内部创建, stopTask回收)
  taskName = "torrent engine status";
  startTask();
  if (!makeParams(url, errMsg)) {
    return false;
  }
  error_code ec;
  std::unique_lock<std::mutex> addLock(g_sessionAddMutex);
  torrent_handle h = session->add_torrent(*params, ec);
  if (ec == errors::duplicate_torrent) {
    // 同会话已有该种子(同磁力二次打开/探测后播放): 复用句柄并按本次配置
    // 重新选择文件; 探测复用在播句柄时元数据即刻就位(免等BEP-9)
    h = session->find_torrent(params->info_hashes.get_best());
    LOGFLF(LogLevel::warn, "[torrent] duplicate torrent, reuse handle");
    ec.clear();
  }
  if (ec || !h.is_valid()) {
    std::string msg = ec ? ec.message() : "invalid handle";
    LOGFLF(LogLevel::error, "[torrent] add_torrent failed:", msg);
    if (errMsg) *errMsg = "add_torrent: " + msg;
    return false;
  }
  // 引用在add锁内登记: 与并发add/duplicate判定/摘种子串行化
  refHash = hashDirName(params->info_hashes.get_best());
  torrentRefAdd(refHash);
  addLock.unlock();
  // default_flags 含 paused, 显式恢复让元数据流动;
  // probe 模式 params 带 stop_when_ready, 元数据一到本句柄即自动暂停(零下载)
  h.resume();
  handle = std::make_unique<torrent_handle>(h);
  LOGFLF(LogLevel::info, "[torrent] added, waiting metadata:",
         params->name.empty() ? url : params->name);
  // 磁力且元数据未就位(缓存快路径会直接带元数据): 并行拉 HTTP 种子缓存,
  // 与 BEP-9 竞速, 谁先到用谁
  if (!handle->torrent_file()) {
    startHttpMetaFetch();
  }
  return waitMetadata(errMsg);
}

// HTTP 元数据拉取上下文: 线程只持shared快照, 引擎shutdown可限时收线
// (收不到就detach), 线程存活期间对象随shared_ptr自持, 无悬垂
struct HttpMetaCtx {
  std::string upper;
  std::string cachePath;
  sha1_hash wantHash;
  std::shared_ptr<torrent_handle> handle;
  std::shared_ptr<std::atomic<bool>> stop;
  std::shared_ptr<std::atomic<bool>> alive;
};

// HTTP 元数据缓存通道: itorrents.org 按 info-hash 直取 .torrent, 解析校验
// info-hash 与磁力一致后 set_metadata 注入运行中的磁力(免等 BEP-9)。
// 命中后顺带落元数据缓存(下次秒开)。线程只碰ctx快照, shutdown置停止位后
// 限时等待, 超时detach由ctx自持寿命(最坏阻塞从网络超时16s降到500ms)
void TorrentEngine::startHttpMetaFetch() {
  std::string hash = hashDirName(params->info_hashes.get_best());
  auto ctx = std::make_shared<HttpMetaCtx>();
  ctx->upper = hash;
  for (auto& c : ctx->upper) c = (char)std::toupper((unsigned char)c);
  ctx->cachePath = metadataCachePath();
  ctx->wantHash = params->info_hashes.get_best();
  ctx->handle = std::make_shared<torrent_handle>(*handle);
  ctx->stop = std::make_shared<std::atomic<bool>>(false);
  ctx->alive = std::make_shared<std::atomic<bool>>(false);
  httpMetaStop = ctx->stop;
  httpMetaAlive = ctx->alive;
  httpMetaThread = std::thread([ctx]() {
    ctx->alive->store(true);
    httplib::Client cli("https://itorrents.org");
    cli.set_connection_timeout(6);
    cli.set_read_timeout(10);
    auto res = cli.Get("/torrent/" + ctx->upper + ".torrent");
    if (ctx->stop->load()) {
      ctx->alive->store(false);
      return;
    }
    if (!res || res->status != 200 || res->body.size() < 100) {
      LOGFLF(LogLevel::info, "[torrent] http metadata cache miss");
      ctx->alive->store(false);
      return;
    }
    try {
      add_torrent_params atp = load_torrent_buffer(
          span<char const>(res->body.data(), (long long)res->body.size()));
      if (atp.ti == nullptr ||
          atp.ti->info_hashes().get_best() != ctx->wantHash) {
        LOGFLF(LogLevel::warn,
               "[torrent] http metadata hash mismatch, discard");
        ctx->alive->store(false);
        return;
      }
      std::ofstream out(ctx->cachePath, std::ios::binary | std::ios::trunc);
      if (out.is_open()) {
        out.write(res->body.data(), (std::streamsize)res->body.size());
      }
      if (!ctx->stop->load() && ctx->handle->is_valid() &&
          !ctx->handle->torrent_file()) {
        if (ctx->handle->set_metadata(atp.ti->info_section())) {
          LOGFLF(LogLevel::info,
                 "[torrent] metadata via http cache(itorrents)");
        }
      }
    } catch (const std::exception& e) {
      LOGFLF(LogLevel::warn, "[torrent] http metadata parse failed:", e.what());
    }
    ctx->alive->store(false);
  });
}

void TorrentEngine::shutdown() {
  bool wasRunning = running();
  // 先停状态观测线程(stopTask内join), 再摘除种子与会话
  if (wasRunning) {
    stopTask();
  }
  // HTTP 元数据线程内部引用 handle/params: 先置停止位, 限时收线; 网络请求
  // 不可中断, 超过500ms转detach(线程持shared_ptr快照, 不再解引用本引擎)
  if (httpMetaStop) {
    httpMetaStop->store(true);
  }
  if (httpMetaThread.joinable()) {
    bool done = false;
    for (int i = 0; i < 50; ++i) {
      // alive=false = 线程已退出(或未启动), 可以安全join
      if (!httpMetaAlive || !httpMetaAlive->load()) {
        done = true;
        break;
      }
      sleepMillis(10);
    }
    if (done) {
      httpMetaThread.join();
    } else {
      httpMetaThread.detach();
      LOGFLF(LogLevel::warn,
             "[torrent] http meta thread detached (network slow)");
    }
  }
  // 等在途读线程退出: readAt 内的 waitForPiece/ensureRange 以 running() 为
  // 退出条件(stopTask 后 ≤100ms 内返回), 此处按读者计数排空后再释放,
  // 否则 handle/session 释放与读线程解引用并发 = 播放中关闭即 UAF 崩溃
  for (int i = 0; i < 40 && readers.load() > 0; ++i) {
    sleepMillis(50);
  }
  if (fileIn.is_open()) {
    fileIn.close();
  }
  fileIn.clear();
  // 引用归零才摘种子: 并发引擎(探测/播放)仍持句柄时摘除会误杀在播。
  // 全程持add锁, 与并发add_torrent/duplicate判定串行(防摘到刚复制的句柄)
  std::lock_guard<std::mutex> addLock(g_sessionAddMutex);
  bool last = refHash.empty() ? false : torrentRefRelease(refHash);
  refHash.clear();
  if (last && handle && session && handle->is_valid()) {
    session->remove_torrent(*handle, config.deleteOnClose
                                         ? session::delete_files
                                         : libtorrent::remove_flags_t{});
  }
  handle.reset();
  params.reset();
  session.reset();
}

bool TorrentEngine::makeParams(const std::string& url, std::string* errMsg) {
  params = std::make_unique<add_torrent_params>();
  error_code ec;
  bool isMagnet = url.rfind("magnet:", 0) == 0;
  bool isTorrentFile =
      !isMagnet && url.size() > 8 &&
      (url.compare(url.size() - 8, 8, ".torrent") == 0 ||
       url.find(".torrent?") != std::string::npos);
  if (isMagnet) {
    parse_magnet_uri(url, *params, ec);
    if (ec) {
      if (errMsg) *errMsg = std::string("parse_magnet_uri: ") + ec.message();
      return false;
    }
    // 元数据缓存快路径: probe 落盘的 metadata.torrent 直接载入, 免 BEP-9 等待。
    // 缓存只含元数据, 磁力 trackers 保留合并; info_hashes 沿用磁力的
    // (保证同缓存目录与同 swarm 身份, 防混用 v1/v2 hash)
    std::string cached = metadataCachePath();
    std::error_code fsec;
    if (!cached.empty() && std::filesystem::exists(cached, fsec)) {
      try {
        add_torrent_params fromCache = load_torrent_file(cached);
        auto hashes = params->info_hashes;
        fromCache.trackers.insert(fromCache.trackers.end(),
                                  params->trackers.begin(),
                                  params->trackers.end());
        // webseed(ws=)同理: 只在磁力 URI 上, 缓存的 .torrent 里没有;
        // 丢了在 BT 不通的网络(手机蜂窝/WiFi 屏蔽 UDP)下会 0 peer 卡死
        fromCache.url_seeds.insert(fromCache.url_seeds.end(),
                                   params->url_seeds.begin(),
                                   params->url_seeds.end());
        *params = std::move(fromCache);
        params->info_hashes = hashes;
        LOGFLF(LogLevel::info, "[torrent] metadata cache hit:", cached);
      } catch (const libtorrent::system_error& e) {
        LOGFLF(LogLevel::warn,
               "[torrent] metadata cache load failed, fallback magnet:",
               e.what());
      }
    }
  } else if (isTorrentFile) {
    // 仅支持本地.torrent路径; 网络URL请由业务侧先落地为本地文件
    try {
      *params = load_torrent_file(url);
    } catch (const libtorrent::system_error& e) {
      if (errMsg) *errMsg = std::string("load_torrent_file: ") + e.what();
      return false;
    }
  } else {
    if (errMsg)
      *errMsg = "unsupported url scheme(magnet:? 开头 或本地 .torrent 路径)";
    return false;
  }
  if (probeMode) {
    // 探测语义: 元数据齐了立刻自动暂停, 文件零下载(无下载竞态窗口)
    params->flags |= torrent_flags::stop_when_ready;
  }
  makeSavePath();
  // 公共 tracker 兜底(去重追加): 磁力往往仅带一两个 tracker, 配合
  // announce_to_all_trackers 多路并行发现 peer, 缩短元数据等待
  for (const char* t : kFallbackTrackers) {
    if (std::find(params->trackers.begin(), params->trackers.end(), t) ==
        params->trackers.end()) {
      params->trackers.push_back(t);
    }
  }
  splitTrackers(config.extraTrackers, extraTrackers);
  for (auto& t : extraTrackers) {
    params->trackers.push_back(t);
  }
  return true;
}

void TorrentEngine::makeSavePath() {
  // 按infohash落盘; 目录不可得时回退当前目录。同种子跨会话复用已下载数据,
  // 并以 metadata.torrent 元数据缓存加速起播(见 saveMetadata/缓存快路径)
  std::string hash = hashDirName(params->info_hashes.get_best());
  std::filesystem::path savePath =
      std::filesystem::path(cacheBaseDir()) / std::filesystem::path(hash);
  std::error_code fsec;
  std::filesystem::create_directories(savePath, fsec);
  params->save_path = savePath.generic_string();
  // 磁盘总量治理: 超限按LRU淘汰最久未用的种子缓存(在用目录除外)
  evictCacheLRU(cacheBaseDir(), hash, config.cacheMaxGB * (int64_t)(1024 * kMB));
  LOGFLF(LogLevel::info, "[torrent] save path:", params->save_path);
}

std::string TorrentEngine::cacheBaseDir() const {
  std::filesystem::path base;
  if (!config.cacheDir.empty()) {
    base = std::filesystem::path(config.cacheDir);
  } else {
    std::error_code fec;
    base = std::filesystem::temp_directory_path(fec);
    if (fec) {
      base = std::filesystem::path(".");
    }
    base /= "avplay_torrent";
  }
  return base.generic_string();
}

std::string TorrentEngine::metadataCachePath() const {
  if (!params) {
    return "";
  }
  std::filesystem::path p =
      std::filesystem::path(cacheBaseDir()) /
      std::filesystem::path(hashDirName(params->info_hashes.get_best())) /
      "metadata.torrent";
  return p.generic_string();
}

bool TorrentEngine::waitMetadata(std::string* errMsg) {
  // BEP-9从peer交换元数据, 无tracker冷启动只能等DHT bootstrap, 有界等待。
  // 两阶段: 第一轮超时后强制重新 announce(DHT+tracker) 再等一轮同预算 ——
  // 慢网络/peer响应慢场景成功率显著提高; 真僵尸swarm两轮也救不活, 总开销有界
  int64_t limitMs = std::max<int32_t>(config.metaTimeoutMs, 1000);
  bool retried = false;
  int64_t waited = 0;
  while (running()) {
    // 外部中止源(上层 stop 等), 快速退出有界等待
    if (abortCheck && abortCheck->load()) {
      if (errMsg) *errMsg = "aborted while waiting metadata";
      return false;
    }
    if (handle->torrent_file()) {
      return true;
    }
    auto st = handle->status();
    if (st.errc) {
      if (errMsg) *errMsg = "metadata: " + st.errc.message();
      return false;
    }
    sleepMillis(50);
    waited += 50;
    if (waited % 5000 == 0) {
      LOGFLF(LogLevel::info, "[torrent] waiting metadata",
             (double)(waited / 1000), "s peers:", st.num_peers);
    }
    if (waited >= limitMs) {
      if (!retried) {
        retried = true;
        waited = 0;
        // 有peer连接着却不给元数据: 多为announce时序问题, 强刷一轮发现源
        handle->resume();
        handle->force_dht_announce();
        handle->force_reannounce();
        LOGFLF(LogLevel::warn, "[torrent] metadata timeout, force announce & retry",
               (double)(limitMs / 1000), "s more");
        continue;
      }
      if (errMsg)
        *errMsg = "metadata timeout(" + std::to_string(limitMs * 2 / 1000) +
                  "s), swarm可能无源";
      return false;
    }
  }
  if (errMsg) *errMsg = "aborted while waiting metadata";
  return false;
}

bool TorrentEngine::selectFile(std::string* errMsg) {
  std::shared_ptr<const torrent_info> ti = handle->torrent_file();
  if (!ti) {
    if (errMsg) *errMsg = "no metadata";
    return false;
  }
  // 顺带填充探测结果(fileList/种子名等), start/probe 数据口径一致
  collectFileList();
  const file_storage& fs = ti->files();
  int32_t nFiles = fs.num_files();
  if (nFiles <= 0) {
    if (errMsg) *errMsg = "empty torrent";
    return false;
  }
  // 目标文件: 手动index > 最大媒体文件 > 最大文件兜底
  bool picked = false;
  if (config.fileIndex >= 0 && config.fileIndex < nFiles) {
    fileInfo.index = config.fileIndex;
    fileInfo.path = fs.file_path(config.fileIndex);
    fileInfo.size = fs.file_size(config.fileIndex);
    picked = true;
  } else {
    uint64_t mediaMax = 0, anyMax = 0;
    int32_t mediaIdx = -1, anyIdx = -1;
    for (int32_t i = 0; i < nFiles; ++i) {
      uint64_t sz = fs.file_size(i);
      if (sz > anyMax) {
        anyMax = sz;
        anyIdx = i;
      }
      if (sz > mediaMax && isMediaFile(fs.file_path(i))) {
        mediaMax = sz;
        mediaIdx = i;
      }
    }
    int32_t idx = mediaIdx >= 0 ? mediaIdx : anyIdx;
    if (idx >= 0) {
      fileInfo.index = idx;
      fileInfo.path = fs.file_path(idx);
      fileInfo.size = fs.file_size(idx);
      picked = true;
    }
  }
  if (!picked) {
    if (errMsg) *errMsg = "no selectable file in torrent";
    return false;
  }
  // 未选中文件完全跳过(dont_download); 选中文件低优先级作底噪整体顺带顺序下载,
  // 播放窗口内再用单片top优先级+deadline精确加速(updatePlayhead)
  pieceLen = ti->piece_length();
  totalPieces = ti->num_pieces();
  // 目标文件在种子内的piece范围: 多文件种子文件起点非0, 供数必须按
  // (文件偏移+文件内偏移)换算piece序号; 窗口/尾部预取钳制在此范围内,
  // 不越界拉其他文件浪费带宽
  fileOffsetInTorrent = fs.file_offset(fileInfo.index);
  fileFirstPiece = (int32_t)(fileOffsetInTorrent / (uint64_t)pieceLen);
  fileLastPiece = fileInfo.size > 0
      ? (int32_t)((fileOffsetInTorrent + fileInfo.size - 1) /
                  (uint64_t)pieceLen)
      : fileFirstPiece;
  nextDemotePiece = fileFirstPiece;
  lastWindowStart = -1;
  lastWindowEnd = -1;
  std::vector<download_priority_t> prios(nFiles, dont_download);
  prios[fileInfo.index] = low_priority;
  // 与目标文件共享边缘piece的邻接文件提到default(只多下几十~几百KB):
  // 完全跳过会让头/尾piece永不落地(open超时); low也会拖——本版本file优先级
  // 盖过piece优先级, 实测头片28s+才到, default档才能让头片跟上piece调度
  for (int32_t i = 0; i < nFiles; ++i) {
    if (i == fileInfo.index ||
        (fs.file_flags(i) & file_storage::flag_pad_file)) {
      continue;
    }
    uint64_t f0 = fs.file_offset(i);
    int32_t fLast = fs.file_size(i) > 0
        ? (int32_t)((f0 + fs.file_size(i) - 1) / (uint64_t)pieceLen)
        : (int32_t)(f0 / (uint64_t)pieceLen);
    if ((int32_t)(f0 / (uint64_t)pieceLen) <= fileLastPiece &&
        fLast >= fileFirstPiece) {
      prios[i] = default_priority;
    }
  }
  handle->prioritize_files(prios);
  // 目标文件绝对路径(读盘用); 统一分隔符避免拼接歧义
  std::string rel = fileInfo.path;
  for (auto& c : rel) {
    if (c == '\\') c = '/';
  }
  absFilePath = params->save_path;
  if (!absFilePath.empty() && absFilePath.back() != '/') {
    absFilePath += '/';
  }
  absFilePath += rel;
  return true;
}

// 目标文件尾部预取片数: 文件大小0.4%(3~12MB钳制)覆盖典型moov/cues体积;
// 以文件尾为基准换算piece区间(多文件种子文件尾≠种子尾)
int32_t TorrentEngine::tailPrefetchPieces() const {
  uint64_t bytes = fileInfo.size / 256;
  bytes = std::min<uint64_t>(std::max<uint64_t>(bytes, (uint64_t)(3 * kMB)),
                             (uint64_t)(12 * kMB));
  int32_t pieces = (int32_t)((bytes + (uint64_t)pieceLen - 1) /
                             (uint64_t)pieceLen);
  int32_t filePieces = fileLastPiece - fileFirstPiece + 1;
  return std::max(1, std::min(pieces, filePieces));
}

void TorrentEngine::updatePlayhead(uint64_t pos, bool firstSchedule) {
  playheadByte = pos;
  if (!handle || !running() || pieceLen <= 0) {
    return;
  }
  // avio每次只读64KB, 必须按片号变化节流否则同片内读全窗口重发
  int32_t curPiece =
      (int32_t)((fileOffsetInTorrent + pos) / (uint64_t)pieceLen);
  if (curPiece == lastScheduledPiece && !firstSchedule) {
    return;
  }
  int32_t oldWinStart = lastWindowStart;
  int32_t oldWinEnd = lastWindowEnd;
  lastScheduledPiece = curPiece;
  // 前向窗口全部提到top(钳制在目标文件片范围内, 不越界拉别的文件)。
  // 首轮只顶头部8片: 头片落地前灌满32MB窗口会跟头片抢请求队列(webseed
  // 64深流水线被窗口占满, 实测头片28s+才到), 头片就绪后随读推进滚动放大
  int64_t aheadBytes = (int64_t)config.lookaheadMB * kMB;
  int32_t aheadPieces =
      std::max(1, (int32_t)((aheadBytes + pieceLen - 1) / pieceLen));
  if (firstSchedule) {
    aheadPieces = std::min(aheadPieces, 8);
  }
  int32_t winEnd =
      std::min(std::min(totalPieces, fileLastPiece + 1), curPiece + aheadPieces);
  std::vector<std::pair<piece_index_t, download_priority_t>> jobs;
  for (int32_t i = curPiece; i < winEnd; ++i) {
    jobs.emplace_back(piece_index_t(i), top_priority);
  }
  if (!jobs.empty()) {
    handle->prioritize_pieces(jobs);
  }
  // 未完成片给deadline(alert_when_available), 12片×250ms错峰让webseed/peer
  // 请求按序流水化(全部delay=0会挤在同一批, seek落点首批数据并行度反而差)
  int32_t deadlines = 0;
  for (int32_t i = curPiece; i < winEnd && deadlines < 12; ++i) {
    if (!handle->have_piece(piece_index_t(i))) {
      handle->set_piece_deadline(piece_index_t(i), deadlines * 250);
      ++deadlines;
    }
  }
  // 尾部索引预取仅在首轮执行: 以目标文件尾为基准(MP4 moov/MKV cues在文件尾
  // 是常态), 小代价换来seek秒级可用
  if (firstSchedule) {
    int32_t tailPieces = tailPrefetchPieces();
    int32_t tailStart = std::max(fileFirstPiece, fileLastPiece + 1 - tailPieces);
    jobs.clear();
    for (int32_t i = tailStart; i <= fileLastPiece; ++i) {
      jobs.emplace_back(piece_index_t(i), top_priority);
    }
    if (!jobs.empty()) {
      handle->prioritize_pieces(jobs);
    }
    int64_t delayMs = 0;
    for (int32_t n = 0;
         n < std::min<int32_t>(tailPieces, 4) && tailStart + n <= fileLastPiece;
         ++n) {
      handle->set_piece_deadline(piece_index_t(tailStart + n), (int)delayMs);
      delayMs += 200;
    }
    LOGFLF(LogLevel::info, "[torrent] initial schedule head+tail, tail:",
           (double)tailPieces);
  } else if (oldWinEnd > oldWinStart) {
    // 旧窗口前向残留降级: seek(尤其回跳)后旧位置不再与新窗口抢带宽;
    // 与新窗口重叠的部分保持top不动
    int32_t staleStart = std::max(oldWinStart, winEnd);
    int32_t staleEnd = std::max(staleStart, oldWinEnd);
    jobs.clear();
    for (int32_t i = staleStart; i < staleEnd; ++i) {
      jobs.emplace_back(piece_index_t(i), low_priority);
    }
    if (!jobs.empty()) {
      handle->prioritize_pieces(jobs);
      LOGFLF(LogLevel::debug, "[torrent] demote stale window pieces:",
             (double)jobs.size());
    }
  }
  // 播放位置后方降回低优先级(释放带宽但保留数据, 回跳seek免重下直取磁盘);
  // 游标增量执行, 每次片窗变化只提交新越过的片
  int64_t backKeep = (int64_t)config.lookaheadMB * kMB / 2;
  uint64_t absHead = fileOffsetInTorrent + pos;
  uint64_t demoteEdge =
      absHead > (uint64_t)backKeep ? absHead - (uint64_t)backKeep : 0;
  int32_t demoteEnd =
      std::min((int32_t)(demoteEdge / (uint64_t)pieceLen), fileLastPiece + 1);
  jobs.clear();
  while (nextDemotePiece < demoteEnd) {
    jobs.emplace_back(piece_index_t(nextDemotePiece), low_priority);
    ++nextDemotePiece;
  }
  if (!jobs.empty()) {
    handle->prioritize_pieces(jobs);
  }
  lastWindowStart = curPiece;
  lastWindowEnd = winEnd;
}

TorrentEngine::ReadResult TorrentEngine::ensureRange(uint64_t offset,
                                                     int32_t len) {
  if (pieceLen <= 0) {
    return ReadResult::timeout;
  }
  // 文件内偏移换算种子内绝对piece序号(多文件种子文件起点非0)
  uint64_t absOff = fileOffsetInTorrent + offset;
  int32_t first = (int32_t)(absOff / (uint64_t)pieceLen);
  int32_t last = (int32_t)((absOff + (uint64_t)len - 1) / (uint64_t)pieceLen);
  last = std::min(last, totalPieces - 1);
  for (int32_t p = first; p <= last; ++p) {
    if (handle->have_piece(piece_index_t(p))) {
      continue;
    }
    if (!waitForPiece(p, std::max<int32_t>(config.pieceTimeoutMs, 2000),
                      nullptr)) {
      // 失败必须上抛: 若吞掉返回ok, 上层会对未下载区域做文件预读,
      // 读到稀疏零数据(花屏/静音)而非触发重试
      return (abortCheck && abortCheck->load()) ? ReadResult::aborted
                                                : ReadResult::timeout;
    }
  }
  return ReadResult::ok;
}

bool TorrentEngine::waitForPiece(int32_t pieceIndex, int64_t budgetMs,
                                 int64_t* waitedSum) {
  // have_piece即哈希校验通过且数据已写入目标文件, 此时普通文件预读即安全
  int64_t waited = waitedSum ? *waitedSum : 0;
  int64_t lastDeadlineAt = -1;
  while (running()) {
    if (handle->have_piece(piece_index_t(pieceIndex))) {
      if (waitedSum) *waitedSum = waited;
      return true;
    }
    // deadline驱动下载(每2s续期一次)
    if (lastDeadlineAt < 0 || waited - lastDeadlineAt >= 2000) {
      handle->set_piece_deadline(piece_index_t(pieceIndex), 0);
      lastDeadlineAt = waited;
    }
    // peers归零且无进度: 等下去没有意义, 提前判定swarm无响应
    // (webseed连接不计数为peers, 有url seed时0 peers不代表swarm死)。
    // status()是会话线程往返, 降频到1s一次, 50ms轮询只查have_piece
    if (waited % 1000 == 0) {
      auto st = handle->status();
      if (st.num_peers == 0 && handle->url_seeds().empty() && waited > 15000) {
        LOGFLF(LogLevel::warn, "[torrent] no peers for piece", pieceIndex);
        if (waitedSum) *waitedSum = waited;
        return false;
      }
    }
    sleepMillis(50);
    waited += 50;
    if (waitedSum) *waitedSum = waited;
    if (waited >= budgetMs) {
      return false;
    }
  }
  if (waitedSum) *waitedSum = waited;
  return false;
}

int32_t TorrentEngine::readAt(uint64_t offset, uint8_t* dst, int32_t len) {
  // 读者计数: shutdown 按此排空在读线程后再释放句柄(见 shutdown 注释)
  if (!running() || !handle) {
    return 0;
  }
  readers.fetch_add(1);
  int32_t served = readAtInner(offset, dst, len);
  readers.fetch_sub(1);
  return served;
}

int32_t TorrentEngine::readAtInner(uint64_t offset, uint8_t* dst,
                                   int32_t len) {
  if (!running() || !handle) {
    return 0;
  }
  if (offset >= fileInfo.size || len <= 0) {
    return 0;
  }
  const uint64_t remain = fileInfo.size - offset;
  const uint64_t wantTotal = std::min<uint64_t>((uint64_t)len, remain);
  updatePlayhead(offset, false);
  int32_t served = 0;
  // 超时重试: 冷启动peer刚连上时优先级生效有延迟, 单次超时常见
  int32_t retries = 0;
  while ((uint64_t)served < wantTotal) {
    int32_t want = (int32_t)std::min<uint64_t>(wantTotal - (uint64_t)served,
                                               (uint64_t)kMB);
    if (ensureRange(offset + (uint64_t)served, want) != ReadResult::ok) {
      if (retries < 2) {
        ++retries;
        LOGFLF(LogLevel::info, "[torrent] chunk wait retry:", retries,
               " at(MB):", (double)((offset + (uint64_t)served) / kMB));
        lastScheduledPiece = -1;
        updatePlayhead(offset + (uint64_t)served, false);
        continue;
      }
      LOGFLF(LogLevel::warn, "[torrent] read abort at(MB):",
             (double)((offset + (uint64_t)served) / kMB));
      return -1;
    }
    retries = 0;
    if (!fileIn.is_open()) {
      return -1;
    }
    fileIn.clear();
    fileIn.seekg((std::streamoff)(offset + (uint64_t)served));
    fileIn.read((char*)(dst + served), want);
    std::streamsize got = fileIn.gcount();
    if (got <= 0) {
      LOGFLF(LogLevel::warn, "[torrent] file read got 0 at:",
             (double)((offset + (uint64_t)served) / kMB), "MB");
      return -1;
    }
    served += (int32_t)got;
    // 消费速度观测日志: 每推进16MB一条, 对比download_rate判断是否追帧
    static thread_local int64_t lastLogPos = -1;
    int64_t logPos = (int64_t)(offset + (uint64_t)served);
    if (lastLogPos < 0 || logPos - lastLogPos >= 16 * kMB) {
      lastLogPos = logPos;
      auto st = handle->status();
      LOGFLF(LogLevel::info, "[torrent] read pos(MB):", (double)(logPos / kMB),
             " dl(KB/s):", st.download_rate / 1024, " peers:", st.num_peers,
             " prog(%):", (double)(st.progress * 100.f));
    }
  }
  return served;
}

void TorrentEngine::onRunTask() {
  // 5s一条状态观测 + 排空alert队列(不pop会无限堆积)
  while (running()) {
    // DHT路由状态节流落盘(2分钟一次), 进程重启后免冷bootstrap
    saveSessionStateThrottled(session);
    session->wait_for_alert(std::chrono::milliseconds(200));
    std::vector<alert*> alerts;
    alerts.reserve(32);
    session->pop_alerts(&alerts);
    for (alert* a : alerts) {
      if (auto* te = alert_cast<torrent_error_alert>(a)) {
        // 共享会话下会 pop 到其他引擎种子的告警: 只记自己的
        if (handle && handle->is_valid() && !(te->handle == *handle)) {
          continue;
        }
        std::string msg = te->message();
        // timeout类是set_piece_deadline的正常副产物, 过滤掉避免刷日志
        if (msg.find("time") == std::string::npos && msg != lastError) {
          lastError = msg;
          LOGFLF(LogLevel::warn, "[torrent]", lastError);
        }
      } else if (alert_cast<metadata_received_alert>(a)) {
        // 元数据到达告警只对种子句柄有意义; waitMetadata 轮询兜底, 无需区分
        LOGFLF(LogLevel::info, "[torrent] metadata received");
      }
    }
    if (++statusTick >= 25) {
      statusTick = 0;
      if (handle && handle->is_valid()) {
        auto st = handle->status();
        // 目标文件头部16片到位情况(0=有,1=缺), 观察顺序调度是否生效
        std::string headBits;
        for (int32_t p = fileFirstPiece;
             p < fileFirstPiece + 16 && p <= fileLastPiece; ++p) {
          headBits += handle->have_piece(piece_index_t(p)) ? "0" : "1";
        }
        LOGFLF(LogLevel::info, "[torrent] status peers:", st.num_peers,
               " ws:", (int64_t)handle->url_seeds().size(),
               " dl(KB/s):", st.download_rate / 1024, " done(MB):",
               (double)(st.total_done / kMB), " state:", (int)st.state,
               " head:", headBits);
      }
    }
  }
}

const TorrentEngine::FileInfo& TorrentEngine::getFileInfo() const {
  return fileInfo;
}

// 元数据 -> 纯值结果(文件列表/种子名/infohash/总大小), probe 关会话后仍可读。
// index 保留种子内原始索引(播放 torrent.fileIndex 直接用), pad 对齐文件不进列表
void TorrentEngine::collectFileList() {
  fileList.clear();
  totalSize = 0;
  std::shared_ptr<const torrent_info> ti = handle->torrent_file();
  if (!ti) {
    return;
  }
  const file_storage& fs = ti->files();
  int32_t n = fs.num_files();
  fileList.reserve(n);
  for (int32_t i = 0; i < n; ++i) {
    if (fs.file_flags(i) & file_storage::flag_pad_file) {
      continue;
    }
    FileInfo fi;
    fi.index = i;
    fi.path = fs.file_path(i);
    fi.size = fs.file_size(i);
    totalSize += fi.size;
    fileList.push_back(std::move(fi));
  }
  torrentName = ti->name();
  infoHash = hashDirName(params->info_hashes.get_best());
  pieceLen = ti->piece_length();
  totalPieces = ti->num_pieces();
}

// ti -> <save_path>/metadata.torrent (probe 顺带落盘; start 缓存快路径读)
void TorrentEngine::saveMetadata() {
  std::shared_ptr<const torrent_info> ti = handle->torrent_file();
  if (!ti) {
    return;
  }
  try {
    create_torrent ctor(*ti);
    entry e = ctor.generate();
    std::vector<char> buf;
    bencode(std::back_inserter(buf), e);
    std::ofstream out(metadataCachePath(), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      LOGFLF(LogLevel::warn, "[torrent] write metadata failed(open):",
             metadataCachePath());
      return;
    }
    out.write(buf.data(), (std::streamsize)buf.size());
    LOGFLF(LogLevel::info, "[torrent] metadata cached:", metadataCachePath());
  } catch (const std::exception& e) {
    LOGFLF(LogLevel::warn, "[torrent] save metadata failed:", e.what());
  }
}

bool TorrentEngine::isMediaPath(const std::string& path) {
  return isMediaFile(path);
}

const std::vector<TorrentEngine::FileInfo>& TorrentEngine::getFileList()
    const {
  return fileList;
}

const std::string& TorrentEngine::getTorrentName() const {
  return torrentName;
}

const std::string& TorrentEngine::getInfoHash() const { return infoHash; }

uint64_t TorrentEngine::getTotalSize() const { return totalSize; }

int32_t TorrentEngine::getPieceLength() const { return pieceLen; }

float TorrentEngine::downloadProgress() {
  if (!handle || !running()) {
    return 0.f;
  }
  return handle->status().progress;
}

void TorrentEngine::setAbortFlag(const std::atomic<bool>* abort) {
  abortCheck = abort;
}

void TorrentEngine::sleepMillis(int64_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}
