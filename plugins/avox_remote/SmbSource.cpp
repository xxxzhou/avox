// 整文件条件编译: libsmb2 就位才定义此宏(CMakeLists, cmake/FindLibsmb2.cmake)
#ifdef AVOX_REMOTE_SMB

// winsock2 须先于 windows 系领头; POSIX 侧用 poll
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <poll.h>
#endif
#include <errno.h>

#include "SmbSource.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <thread>

#include <smb2/libsmb2-dcerpc-srvsvc.h>
#include <smb2/libsmb2-raw.h>

namespace avox {

namespace {

std::string toLowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return out;
}

bool isMediaPath(const std::string& path) {
  static const char* kExts[] = {".mp4", ".mkv", ".ts",  ".flv", ".webm",
                                ".avi", ".mov", ".m4v", ".mpg", ".mpeg",
                                ".wmv", ".3gp", ".rm",  ".rmvb"};
  size_t dot = path.rfind('.');
  if (dot == std::string::npos) {
    return false;
  }
  std::string ext = toLowerCopy(path.substr(dot));
  for (auto* e : kExts) {
    if (ext == e) {
      return true;
    }
  }
  return false;
}

}  // namespace

SmbSource::SmbSource() {}

SmbSource::~SmbSource() { close(); }

void SmbSource::setOb(IRemoteSourceOb* ob) { obAt.store(ob); }

uint32_t SmbSource::getCaps() {
  // 纯 SMB 协议无服务端搜索/缩略图/播放状态
  return 0;
}

RemoteAuthKind SmbSource::getAuthKind() { return RemoteAuthKind::userPass; }

void SmbSource::setParam(const char* key, const char* value) {
  (void)key;
  (void)value;
  // v1 无会话级参数(账密走 open 参数/URL userinfo; 超时走 timeoutMs)
}

bool SmbSource::parseUrl(const std::string& url, UrlParts* out) {
  const std::string kScheme = "smb://";
  if (url.rfind(kScheme, 0) != 0) {
    return false;
  }
  size_t rest = kScheme.size();
  size_t pathStart = url.find('/', rest);
  std::string authority =
      url.substr(rest, pathStart == std::string::npos ? std::string::npos
                                                      : pathStart - rest);
  std::string path =
      pathStart == std::string::npos ? "" : url.substr(pathStart + 1);
  // userinfo(取 host 前最后一个 '@'): user[:pass]
  size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    std::string ui = authority.substr(0, at);
    authority = authority.substr(at + 1);
    size_t colon = ui.find(':');
    if (colon == std::string::npos) {
      out->user = ui;
    } else {
      out->user = ui.substr(0, colon);
      out->pass = ui.substr(colon + 1);
    }
  }
  // host[:port] (IPv6 简单支持 [..]:port)
  if (!authority.empty() && authority[0] == '[') {
    size_t rb = authority.find(']');
    if (rb == std::string::npos) {
      return false;
    }
    out->host = authority.substr(1, rb - 1);
    if (rb + 1 < authority.size() && authority[rb + 1] == ':') {
      out->port = std::atoi(authority.c_str() + rb + 2);
    }
  } else {
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos &&
        authority.find(':') == colon) {  // 单冒号才当端口, 防裸 IPv6
      out->host = authority.substr(0, colon);
      out->port = std::atoi(authority.c_str() + colon + 1);
    } else {
      out->host = authority;
    }
  }
  if (out->host.empty()) {
    return false;
  }
  if (out->port == 0) {
    out->port = 445;
  }
  // 第一段路径 = 共享名; 无 share = 服务器级会话(仅供 listShares, §六228 M1)
  if (path.empty() || path == "/") {
    out->rootToken = "/";
    return true;
  }
  if (path.back() == '/') {
    path.pop_back();
  }
  size_t slash = path.find('/');
  if (slash == std::string::npos) {
    out->share = path;
    out->rootToken = "/";
  } else {
    out->share = path.substr(0, slash);
    out->rootToken = path.substr(slash);  // 保留首'/', 目录带尾'/'
    if (out->rootToken.back() != '/') {
      out->rootToken += '/';
    }
  }
  if (out->share.empty()) {
    return false;
  }
  return true;
}

bool SmbSource::open(const char* url, const char* user, const char* pass,
                     const char* token, int32_t timeoutMs) {
  (void)token;  // smb 无 token 形态鉴权 (NTLM 走 user/pass, domain 在 user 内)
  if (running()) {
    return false;
  }
  if (url == nullptr || *url == '\0') {
    return false;
  }
  auto p = std::make_unique<UrlParts>();
  if (!parseUrl(url, p.get())) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "unsupported url(expect smb://host[:port]/share[/path])";
    return false;
  }
  if (user != nullptr && *user != '\0') {
    p->user = user;
    p->pass = pass != nullptr ? pass : "";
  }
  opTimeoutMs = timeoutMs > 0 ? timeoutMs : 10000;
  {
    std::lock_guard<std::mutex> lk(resultMutex);
    batch.clear();
    sessionName = "";
    lastError = "";
    resolvedBuf = "";
  }
  parts = std::move(p);  // startTask 前写完, 工作线程读无竞态
  openedFlag.store(false);
  abortFlag.store(false);
  // 新会话: 上一会话残留连接作废(host/share 可能已换; running()已排除并发)
  if (smbCtx != nullptr) {
    smb2_disconnect_share(smbCtx);
    smb2_destroy_context(smbCtx);
    smbCtx = nullptr;
  }
  op = Op::open;
  taskName = "smb open";
  startTask();
  return true;
}

void SmbSource::close() {
  // libsmb2 同步调用不可中途打断: 当前调用返回后工作线程见 abortFlag 丢弃结果退出
  abortFlag.store(true);
  openedFlag.store(false);
  if (running()) {
    stopTask();  // join 后工作线程不再触碰 smbCtx, 此处销毁安全
  }
  if (smbCtx != nullptr) {
    smb2_disconnect_share(smbCtx);
    smb2_destroy_context(smbCtx);
    smbCtx = nullptr;
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  batch.clear();
  sessionName = "";
  lastError = "";
  resolvedBuf = "";
}

bool SmbSource::opened() { return openedFlag.load(); }

bool SmbSource::list(const char* nodeToken, int32_t timeoutMs) {
  if (running() || !openedFlag.load()) {
    return false;
  }
  listToken = nodeToken != nullptr ? nodeToken : "";
  opTimeoutMs = timeoutMs > 0 ? timeoutMs : opTimeoutMs;
  abortFlag.store(false);
  op = Op::list;
  taskName = "smb list";
  startTask();
  return true;
}

bool SmbSource::listShares(int32_t timeoutMs) {
  if (running() || !openedFlag.load()) {
    return false;
  }
  opTimeoutMs = timeoutMs > 0 ? timeoutMs : opTimeoutMs;
  abortFlag.store(false);
  op = Op::enumShares;
  taskName = "smb enum";
  startTask();
  return true;
}

void SmbSource::stopList() {
  abortFlag.store(true);
  if (running()) {
    stopTask();
  }
}

bool SmbSource::fillBatch(struct smb2_context* ctx, struct smb2dir* dir,
                          const std::string& dirToken) {
  (void)ctx;
  batch.clear();
  std::vector<Entry> items;
  struct smb2dirent* ent = nullptr;
  while ((ent = smb2_readdir(ctx, dir)) != nullptr) {
    if (std::strcmp(ent->name, ".") == 0 || std::strcmp(ent->name, "..") == 0) {
      continue;
    }
    Entry e;
    bool isDir = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;
    e.name = ent->name;
    // token 是 share 内绝对路径(含 rootToken 前缀), 下钻/resolve 直接可用
    e.token = dirToken + ent->name + (isDir ? "/" : "");
    e.type = isDir ? RemoteEntryType::dir
                   : (isMediaPath(e.name) ? RemoteEntryType::media
                                          : RemoteEntryType::file);
    e.size = isDir ? 0 : (uint64_t)ent->st.smb2_size;
    e.mtime = (int64_t)ent->st.smb2_mtime;
    items.push_back(std::move(e));
  }
  // 目录在前, 同类名称升序
  std::stable_sort(items.begin(), items.end(),
                   [](const Entry& a, const Entry& b) {
                     if (a.type != b.type) {
                       return a.type == RemoteEntryType::dir;
                     }
                     return a.name < b.name;
                   });
  batch = std::move(items);
  return !batch.empty();
}

struct smb2_context* SmbSource::ensureConnected() {
  const UrlParts& p = *parts;
  // 陈连接失效场景: 销毁重建全新重试一次(半开/服务端断连常见)
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (smbCtx == nullptr) {
      smbCtx = newServerCtx();
      if (smbCtx == nullptr) {
        return nullptr;
      }
    }
    smb2_set_timeout(smbCtx, std::max<int32_t>(1, (opTimeoutMs + 999) / 1000));
    if (!p.user.empty()) {
      smb2_set_user(smbCtx, p.user.c_str());
      smb2_set_password(smbCtx, p.pass.c_str());
    }
    if (smb2_connect_share(smbCtx, p.host.c_str(), p.share.c_str(),
                           p.user.empty() ? nullptr : p.user.c_str()) == 0) {
      return smbCtx;
    }
    std::string err = smb2_get_error(smbCtx);
    {
      std::lock_guard<std::mutex> lk(resultMutex);
      lastError = "connect share failed: " + err;
    }
    smb2_disconnect_share(smbCtx);
    smb2_destroy_context(smbCtx);
    smbCtx = nullptr;
    if (attempt > 0) {
      return nullptr;
    }
  }
  return nullptr;
}

bool SmbSource::isAuthErrorText(const std::string& err) {
  return err.find("LOGON") != std::string::npos ||
         err.find("ACCESS_DENIED") != std::string::npos ||
         err.find("BAD_USERID") != std::string::npos ||
         err.find("PASSWORD_EXPIRED") != std::string::npos;
}

void SmbSource::runOpen() {
  // 服务器级会话(无 share): 不 connect_share, 仅供 listShares
  if (parts->share.empty()) {
    int32_t code = (int32_t)RemoteCode::ok;
    struct smb2_context* ctx = newServerCtx();
    if (ctx == nullptr) {
      code = (int32_t)RemoteCode::net;
    } else {
      smbCtx = ctx;
      std::lock_guard<std::mutex> lk(resultMutex);
      sessionName = parts->host;
      lastError = "";
      openedFlag.store(true);
    }
    if (abortFlag.load()) {
      return;
    }
    IRemoteSourceOb* o = obAt.load();
    if (o) {
      o->onOpenResult(code);
    }
    return;
  }
  int32_t code = (int32_t)RemoteCode::ok;
  if (ensureConnected() == nullptr) {
    std::string err;
    {
      std::lock_guard<std::mutex> lk(resultMutex);
      err = lastError;
    }
    code = isAuthErrorText(err) ? (int32_t)RemoteCode::authFailed
                                : (int32_t)RemoteCode::net;
  } else {
    std::lock_guard<std::mutex> lk(resultMutex);
    sessionName = parts->share;
    lastError = "";
    openedFlag.store(true);
  }
  // 上层已 close: 结果作废, 不再回调
  if (abortFlag.load()) {
    return;
  }
  IRemoteSourceOb* o = obAt.load();
  if (o) {
    o->onOpenResult(code);
  }
}

void SmbSource::runList() {
  const UrlParts& p = *parts;
  if (p.share.empty()) {
    // 服务器级会话无 share 可列(只许 listShares)
    {
      std::lock_guard<std::mutex> lk(resultMutex);
      lastError = "no share in session(server-level)";
    }
    if (abortFlag.load()) {
      return;
    }
    IRemoteSourceOb* o = obAt.load();
    if (o) {
      o->onListResult((int32_t)RemoteCode::notFound);
    }
    return;
  }
  // 顶层"/"映射到会话入口子目录(rootToken); 其余 token 即 share 内绝对路径
  std::string dir = listToken.empty() ? "/" : listToken;
  if ((dir == "/" || dir.empty()) && p.rootToken != "/") {
    dir = p.rootToken;
  }
  if (!isDirToken(dir)) {
    dir += "/";  // 容错: 目录 token 必须带尾'/'
  }
  // share 内路径: 去首'/'(libsmb2 以 share 为根, '/'分隔)
  std::string sharePath = dir;
  if (!sharePath.empty() && sharePath[0] == '/') {
    sharePath = sharePath.substr(1);
  }
  if (sharePath.size() > 1 && sharePath.back() == '/') {
    sharePath.pop_back();  // opendir 要的是不带尾'/'的路径
  }

  // 会话级连接复用(a05-T4): ensureConnected 已连即用, 失效自动重建重试
  int32_t code = (int32_t)RemoteCode::ok;
  struct smb2_context* ctx = ensureConnected();
  if (ctx == nullptr) {
    std::lock_guard<std::mutex> lk(resultMutex);
    code = (int32_t)RemoteCode::net;
  } else {
    struct smb2dir* d = smb2_opendir(ctx, sharePath.c_str());
    if (d == nullptr) {
      std::string err = smb2_get_error(ctx);
      {
        std::lock_guard<std::mutex> lk(resultMutex);
        lastError = "opendir " + dir + " failed: " + err;
      }
      if (err.find("NOT_FOUND") != std::string::npos) {
        code = (int32_t)RemoteCode::notFound;
      } else {
        // 连接级失效(opendir 非 NOT_FOUND): 弃用旧连接, 下次 op 全新建连
        code = (int32_t)RemoteCode::net;
        smb2_disconnect_share(ctx);
        smb2_destroy_context(ctx);
        smbCtx = nullptr;
      }
    } else {
      bool ok = fillBatch(ctx, d, dir);
      smb2_closedir(ctx, d);
      std::lock_guard<std::mutex> lk(resultMutex);
      if (!ok) {
        lastError = "no entries under " + dir;
        code = (int32_t)RemoteCode::notFound;
      }
    }
  }
  // 上层已 stopList/close: 结果作废, 不再回调
  if (abortFlag.load()) {
    return;
  }
  IRemoteSourceOb* o = obAt.load();
  if (o) {
    o->onListResult(code);
  }
}

void SmbSource::onRunTask() {
  if (op == Op::open) {
    runOpen();
  } else if (op == Op::enumShares) {
    runEnumShares();
  } else {
    runList();
  }
}

// 服务器级 ctx(init+凭据+timeout, 不 connect_share)
struct smb2_context* SmbSource::newServerCtx() {
  const UrlParts& p = *parts;
  struct smb2_context* ctx = smb2_init_context();
  if (ctx == nullptr) {
    std::lock_guard<std::mutex> lk(resultMutex);
    lastError = "smb2_init_context failed";
    return nullptr;
  }
  smb2_set_timeout(ctx, std::max<int32_t>(1, (opTimeoutMs + 999) / 1000));
  if (!p.user.empty()) {
    smb2_set_user(ctx, p.user.c_str());
    smb2_set_password(ctx, p.pass.c_str());
  }
  return ctx;
}

void SmbSource::enumCb(struct smb2_context* ctx, int status,
                       void* command_data, void* cb_data) {
  (void)ctx;
  auto* self = static_cast<SmbSource*>(cb_data);
  self->enumStatus = status;
  self->enumRep = status == 0 ? command_data : nullptr;
  self->enumDone = true;
}

// 异步 op 泵: 官方示例同款(select/poll 服务 fd), 完成回调在泵内联触发
bool SmbSource::pumpAsync(struct smb2_context* ctx,
                          const std::function<bool()>& done) {
  for (;;) {
    if (done()) {
      return true;
    }
    if (abortFlag.load()) {
      return false;
    }
    int fd = smb2_get_fd(ctx);
    short events = (short)smb2_which_events(ctx);
    short revents = 0;
    if (fd < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
#ifdef _WIN32
    fd_set rfds, wfds, efds;
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    FD_SET((SOCKET)fd, &rfds);
    FD_SET((SOCKET)fd, &wfds);
    FD_SET((SOCKET)fd, &efds);
    timeval tv{0, 100 * 1000};
    int r = select(0, &rfds, &wfds, &efds, &tv);
    if (r > 0) {
      if (FD_ISSET((SOCKET)fd, &rfds)) revents |= POLLIN;
      if (FD_ISSET((SOCKET)fd, &wfds)) revents |= POLLOUT;
      if (FD_ISSET((SOCKET)fd, &efds)) revents |= POLLERR;
    }
#else
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    int r = poll(&pfd, 1, 100);
    if (r > 0) {
      revents = pfd.revents;
    }
#endif
    if (r < 0) {
      return false;
    }
    if (revents != 0 && smb2_service_fd(ctx, fd, revents) < 0) {
      return false;
    }
  }
}

void SmbSource::runEnumShares() {
  const UrlParts& p = *parts;
  int32_t code = (int32_t)RemoteCode::ok;
  // srvsvc 走 IPC$ 会话(官方示例同款); ctx 复用/新建均服务器级
  if (smbCtx == nullptr) {
    smbCtx = newServerCtx();
  }
  if (smbCtx != nullptr) {
    smb2_set_security_mode(smbCtx, SMB2_NEGOTIATE_SIGNING_ENABLED);
  }
  if (smbCtx == nullptr ||
      smb2_connect_share(smbCtx, p.host.c_str(), "IPC$",
                         p.user.empty() ? nullptr : p.user.c_str()) != 0) {
    {
      std::lock_guard<std::mutex> lk(resultMutex);
      lastError = "connect IPC$ failed: " +
                  (smbCtx != nullptr ? std::string(smb2_get_error(smbCtx)) : "");
    }
    code = (int32_t)RemoteCode::net;
    if (smbCtx != nullptr) {
      smb2_disconnect_share(smbCtx);
      smb2_destroy_context(smbCtx);
      smbCtx = nullptr;
    }
  } else {
    enumDone = false;
    enumStatus = 0;
    enumRep = nullptr;
    if (smb2_share_enum_async(smbCtx, SHARE_INFO_1, &SmbSource::enumCb,
                              this) != 0) {
      {
        std::lock_guard<std::mutex> lk(resultMutex);
        lastError =
            "share enum start failed: " + std::string(smb2_get_error(smbCtx));
      }
      code = (int32_t)RemoteCode::net;
    } else if (!pumpAsync(smbCtx, [this] { return enumDone; })) {
      {
        std::lock_guard<std::mutex> lk(resultMutex);
        lastError = "share enum timeout/aborted";
      }
      code = (int32_t)RemoteCode::net;
    } else if (enumStatus != 0) {
      std::lock_guard<std::mutex> lk(resultMutex);
      lastError = "share enum failed: status " + std::to_string(enumStatus) +
                  ": " + std::string(smb2_get_error(smbCtx));
      code = (int32_t)RemoteCode::net;
    } else {
      // SHARE_INFO_1 → 批次(只收 Disk 共享; IPC$/打印/设备滤除)
      auto* rep = static_cast<struct srvsvc_NetrShareEnum_rep*>(enumRep);
      std::vector<Entry> items;
      uint32_t n = rep->ses.ShareInfo.Level1.EntriesRead;
      for (uint32_t i = 0; i < n; ++i) {
        const auto& s = rep->ses.ShareInfo.Level1.Buffer->share_info_1[i];
        if ((s.type & 3) != SHARE_TYPE_DISKTREE) {
          continue;
        }
        Entry e;
        e.type = RemoteEntryType::dir;
        e.name = s.netname.utf8 != nullptr ? s.netname.utf8 : "";
        e.token = e.name;  // 共享条目 token = 共享名(与目录 token 空间不同)
        items.push_back(std::move(e));
      }
      std::stable_sort(items.begin(), items.end(),
                       [](const Entry& a, const Entry& b) {
                         return a.name < b.name;
                       });
      {
        std::lock_guard<std::mutex> lk(resultMutex);
        batch = std::move(items);
      }
      smb2_free_data(smbCtx, enumRep);
      enumRep = nullptr;
    }
  }
  if (abortFlag.load()) {
    return;
  }
  IRemoteSourceOb* o = obAt.load();
  if (o) {
    o->onListResult(code);
  }
}

std::string SmbSource::baseName(const std::string& token) {
  std::string noSlash = token;
  if (noSlash.size() > 1 && noSlash.back() == '/') {
    noSlash.pop_back();
  }
  size_t pos = noSlash.find_last_of('/');
  return pos == std::string::npos ? noSlash : noSlash.substr(pos + 1);
}

bool SmbSource::isDirToken(const std::string& token) {
  return !token.empty() && token.back() == '/';
}

std::string SmbSource::entryUrl(const std::string& token) const {
  const UrlParts& p = *parts;
  std::string auth;
  if (!p.user.empty()) {
    auth = p.user + ":" + p.pass + "@";
  }
  std::string port;
  if (p.port != 445) {
    port = ":" + std::to_string(p.port);
  }
  // token 是 share 内绝对路径(首'/'开头): 拼成 smb://host[:port]/share/path
  std::string path = token;
  if (!path.empty() && path.back() == '/') {
    path.pop_back();
  }
  return "smb://" + auth + p.host + port + "/" + p.share + path;
}

int32_t SmbSource::getEntryCount() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (int32_t)batch.size();
}

RemoteEntryType SmbSource::getEntryType(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].type
                                               : RemoteEntryType::other;
}

const char* SmbSource::getEntryName(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].name.c_str() : "";
}

uint64_t SmbSource::getEntrySize(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].size : 0;
}

int64_t SmbSource::getEntryMtime(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].mtime : 0;
}

const char* SmbSource::getEntryToken(int32_t i) {
  std::lock_guard<std::mutex> lk(resultMutex);
  return (i >= 0 && i < (int32_t)batch.size()) ? batch[i].token.c_str() : "";
}

const char* SmbSource::getSessionField(const char* key) {
  if (key == nullptr) {
    return "";
  }
  std::lock_guard<std::mutex> lk(resultMutex);
  if (std::strcmp(key, "name") == 0) {
    return sessionName.c_str();
  }
  if (parts && std::strcmp(key, "host") == 0) {
    return parts->host.c_str();
  }
  return "";
}

const char* SmbSource::resolve(int32_t entryIndex, IOption* option) {
  (void)option;  // smb 播放无需私有 option 键 (鉴权已嵌 userinfo)
  std::lock_guard<std::mutex> lk(resultMutex);
  if (parts && parts->share.empty()) {
    lastError = "server-level session has no share";
    return nullptr;
  }
  if (entryIndex < 0 || entryIndex >= (int32_t)batch.size()) {
    lastError = "entry index out of range";
    return nullptr;
  }
  const Entry& e = batch[entryIndex];
  if (e.type == RemoteEntryType::dir) {
    lastError = "entry is a directory(先 list 下钻): " + e.token;
    return nullptr;
  }
  resolvedBuf = entryUrl(e.token);
  return resolvedBuf.c_str();
}

const char* SmbSource::getLastError() {
  std::lock_guard<std::mutex> lk(resultMutex);
  return lastError.c_str();
}

}

#endif  // AVOX_REMOTE_SMB
