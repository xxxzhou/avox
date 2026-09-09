#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "avox/module/RunTask.hpp"

// 前向声明避免在头文件里带 libtorrent 头(插件外部不可见)
namespace libtorrent {
struct add_torrent_params;
struct session;
class torrent_handle;
}  // namespace libtorrent

namespace avox {

// BitTorrent 边下边播引擎封装(libtorrent)
// 职责: magnet/.torrent -> 会话启动 -> 元数据等待 -> 选文件+顺序调度 -> 按字节区间供数
// 供数方式: piece下载完成即已写入目标文件且经哈希校验(have_piece),
// 直接对目标文件做预读, 不经read_piece回调(该路径磁盘线程依赖重, 调试成本高)
// 线程模型: 外部单线程调用(start/readAt/shutdown),
// RunTask任务线程仅做周期状态观测与alert队列排空
class TorrentEngine : public RunTask {
 public:
  TorrentEngine();
  // 析构定义在cpp: 成员持有前向声明类型的unique_ptr, 需在完整类型可见处销毁
  ~TorrentEngine();

 public:
  // 运行配置(open时快照)
  struct Config {
    // 种子缓存目录(save_path), 为空则用系统临时目录/avox_torrent/<infohash>
    std::string cacheDir = "";
    // 元数据获取超时(ms, 两阶段: 超时后强刷announce再等一轮同预算), 首2片预取也复用此预算
    int32_t metaTimeoutMs = 45000;
    // 播放位置前向缓冲(MB); 窗口越大peer请求队列越满, 单片卡顿不拖慢整体
    int32_t lookaheadMB = 32;
    // 单片下载等待超时(ms)
    int32_t pieceTimeoutMs = 20000;
    // 额外tracker, 分号或逗号分隔
    std::string extraTrackers = "";
    // 关闭时是否删除已下载数据
    bool deleteOnClose = false;
    // 手动指定文件索引(-1自动选最大媒体文件)
    int32_t fileIndex = -1;
    // 全局下载限速(KB/s, <=0不限)
    int64_t maxDownloadSpeedKB = 0;
    // 缓存目录总量上限(GB, <=0不限): 超限按LRU淘汰最久未用的种子缓存
    int64_t cacheMaxGB = 0;
  };
  // 选中的目标文件信息
  struct FileInfo {
    int32_t index = -1;
    std::string path = "";
    uint64_t size = 0;
  };
  // 内部等待的结束原因
  enum class ReadResult { ok, timeout, aborted };

 private:
  // 会话对象(libtorrent完整类型在cpp); 播放与探测共用进程级共享会话(见cpp
  // sharedTorrentSession), 摘种子时机由引用计数裁定
  std::shared_ptr<libtorrent::session> session;
  // 本引擎在共享会话中的种子引用标记(info-hash hex); shutdown按引用计数裁定
  // 是否摘种子(归零才摘, 防误杀并发在播/在探引擎)
  std::string refHash;
  // 当前种子句柄(cpp内构造)
  std::unique_ptr<libtorrent::torrent_handle> handle;
  // 本次种子参数(magnet/.torrent解析产物)
  std::unique_ptr<libtorrent::add_torrent_params> params;
  Config config;
  FileInfo fileInfo;
  // 分片大小(字节)
  int32_t pieceLen = 0;
  // 总分片数
  int32_t totalPieces = 0;
  // 目标文件在种子内的起始字节偏移(多文件种子非0, 供数换算piece序号用)
  uint64_t fileOffsetInTorrent = 0;
  // 目标文件首/末piece序号(窗口/尾部预取都钳制在此范围, 不越界拉别的文件)
  int32_t fileFirstPiece = 0;
  int32_t fileLastPiece = 0;
  // 当前播放位置(字节, 文件内偏移), 驱动优先级窗口
  uint64_t playheadByte = 0;
  // 上次窗口调度的起始片号(avio每次只读64KB, 必须按片号节流否则全窗口重发)
  int32_t lastScheduledPiece = -1;
  // 上次窗口调度区间(seek后旧窗口前向残留降级用)
  int32_t lastWindowStart = -1;
  int32_t lastWindowEnd = -1;
  // 落后片降级游标(已降到的下一片)
  int32_t nextDemotePiece = 0;
  // 自定义tracker拆分暂存
  std::vector<std::string> extraTrackers;
  // 目标文件的绝对路径(读盘用)
  std::string absFilePath;
  // probe 模式(stop_when_ready 零下载, 元数据落盘后即关)
  bool probeMode = false;
  // 探测结果(纯值拷贝): 全量文件列表 + 种子名/infohash/总大小
  std::vector<FileInfo> fileList;
  std::string torrentName;
  std::string infoHash;
  uint64_t totalSize = 0;
  // 目标文件读句柄(单demux线程使用)
  std::ifstream fileIn;
  // 最近错误信息
  std::string lastError;
  std::mutex errMutex;
  // 状态日志节流计数(任务循环200ms一轮, 25轮≈5s一条)
  int32_t statusTick = 0;
  // 中断源(seek打断等), 非拥有指针
  const std::atomic<bool>* abortCheck = nullptr;
  // readAt 读者计数: shutdown 先等读者退出再释放 handle/session,
  // 防止播放中关闭时读线程解引用已释放的 libtorrent 句柄(UAF)
  std::atomic<int32_t> readers{0};
  // 元数据 HTTP 缓存通道(itorrents.org): 与 BEP-9 竞速拿元数据, 谁先到用谁
  std::thread httpMetaThread;
  // 线程停止位/存活位(线程持shared快照, shutdown限时收线, 超时detach)
  std::shared_ptr<std::atomic<bool>> httpMetaStop;
  std::shared_ptr<std::atomic<bool>> httpMetaAlive;

 private:
  // 并行拉 HTTP 种子缓存(按info-hash直取.torrent), 校验后注入运行中的磁力
  void startHttpMetaFetch();
  // 解析url类型并填充add_torrent_params
  bool makeParams(const std::string& url, std::string* errMsg);
  // start/probe 共享前段: 会话建立 + makeParams + add_torrent + 等元数据
  // (失败返回 false, 由调用方 shutdown)
  bool waitForTorrent(const std::string& url, std::string* errMsg);
  // 元数据 -> 纯值结果(fileList/torrentName/infoHash/totalSize)
  void collectFileList();
  // ti 落盘 <save_path>/metadata.torrent (probe 顺带缓存; start 快路径读)
  void saveMetadata();
  // 缓存基础目录(<cacheDir|tmp>/avox_torrent)
  std::string cacheBaseDir() const;
  // <cacheBaseDir>/<infohash>/metadata.torrent
  std::string metadataCachePath() const;
  // 缓存目录解析/创建(按infohash落盘, 同种子跨会话复用已下载数据)
  void makeSavePath();
  // 阻塞等元数据, 有界(BEP-9从peer交换, 冷门种子靠DHT慢)
  bool waitMetadata(std::string* errMsg);
  // 选目标文件(手动index > 最大媒体 > 最大文件), 设置文件级优先级
  bool selectFile(std::string* errMsg);
  // 目标文件尾部预取片数(0.4%文件大小, 3~12MB钳制; moov/cues场景)
  int32_t tailPrefetchPieces() const;
  // 把播放位置推进到pos并调度前向窗口(按片号变化节流); firstSchedule时额外
  // 预取尾部索引段(MP4 moov/MKV cues尾部场景)
  void updatePlayhead(uint64_t pos, bool firstSchedule);
  // 等待单片下载落地(have_piece即哈希校验通过), 有界; 期间续期deadline
  bool waitForPiece(int32_t pieceIndex, int64_t budgetMs, int64_t* waitedSum);
  // 保证[offset,offset+len)对应分片全部落地
  TorrentEngine::ReadResult ensureRange(uint64_t offset, int32_t len);
  // readAt 实体(readers 计数包裹层之下, 供 shutdown 等待读者退出)
  int32_t readAtInner(uint64_t offset, uint8_t* dst, int32_t len);
  // 毫秒睡眠小工具
  static void sleepMillis(int64_t ms);

 protected:
  // RunTask任务循环: 5s一条状态观测日志 + 排空alert队列防堆积
  virtual void onRunTask() override;

 public:
  // 探测: 只等元数据建文件列表, 不选文件不下载(stop_when_ready 元数据一到自动暂停),
  // 元数据顺带落盘 metadata.torrent, 同 cacheDir 的 start() 命中后免 BEP-9 等待。
  // 阻塞有界(元数据超时即返回), 返回后可读 getFileList 等结果, 引擎已关闭。
  bool probe(const std::string& url, const Config& cfg, std::string* errMsg);
  // 打开magnet:/xxx 或本地 .torrent 文件路径
  // 阻塞有界: 元数据就绪+选好文件+首2片落地(head/tail首轮调度完成)才返回
  bool start(const std::string& url, const Config& cfg, std::string* errMsg);
  // 关闭会话(deleteOnClose时删除已下载数据)
  void shutdown();
  // 读回调的中断检查源(seek打断时快速退出阻塞读), 非拥有
  void setAbortFlag(const std::atomic<bool>* abort);
  // 选中的目标文件信息(start成功后有效)
  const FileInfo& getFileInfo() const;
  // ---- 探测结果(probe 后有效; start 也会顺带填充) ----
  // 全量文件列表(种子内顺序, 已剔除 .pad 对齐文件; index 为种子内原始索引)
  const std::vector<FileInfo>& getFileList() const;
  // 种子名 / info-hash 十六进制 / 全部文件总字节
  const std::string& getTorrentName() const;
  const std::string& getInfoHash() const;
  uint64_t getTotalSize() const;
  // 媒体扩展名判定(探测列表标注用)
  static bool isMediaPath(const std::string& path);
  // 分片大小(字节)
  int32_t getPieceLength() const;
  // 整体下载进度0~1(统计日志用)
  float downloadProgress();
  // 顺序读取[off,off+len)到dst并保证读满(len以内受文件尾钳制),
  // 返回实际读取长度; 数据不可用时返回负数(-1)或被seek打断返回0
  int32_t readAt(uint64_t offset, uint8_t* dst, int32_t len);
};
}
