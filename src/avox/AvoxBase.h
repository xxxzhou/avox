#pragma once

#include "AvoxDef.h"

// 公共基础层头: 只依赖 AvoxDef.h, STL-free, 可被纯 C / SWIG 调用。
// 给别的 AvoxXXX.h 作前置依赖 (Language/ModelLevel/IOption 等), 以及跨模块的单实例工具
// (翻译器 ITranslator / Python IPyRunner / 路径 getAvoxRunDir)。改名链: AvoxNet.h → AvoxCommon.h → AvoxBase.h
// (并入 AvoxModel.h 的翻译/枚举、AvoxPython.h 的执行器、AvoxOption.h 的配置接口)。

namespace avox {

// ============== 网络 IP 类型 ==============
struct IP4Address {
  uint8_t arr1;
  uint8_t arr2;
  uint8_t arr3;
  uint8_t arr4;
};

struct IP4Endpoint {
  IP4Address address = {};
  uint16_t port = 0;
};

struct TimeBase {
  int32_t num = 1;
  int32_t den = 1;
};

// ============== 语言类型 ==============
enum class Language { none, zh, en, ja, other };

// ============== 模型精度等级 ==============
enum class ModelLevel {
  none = 0,
  mini,  // 轻量级: 快速，效果一般
  base,  // 标准: 平衡速度和效果 (推荐)
  high   // 高质量: 慢，效果好
};

// ============== 翻译器接口 ==============
// 支持多语言翻译的抽象接口 (跨 DLL 安全: 仅 const char* + 原始类型)
class ITranslator {
 public:
  virtual ~ITranslator() = default;

  // 打开 (经缓存取 session + 加载 tokenizer; 幂等)
  virtual bool open() = 0;
  // 关闭 (清指针 + 释放 tokenizer; 不释放共享模型, 模型随缓存常驻)
  virtual void close() = 0;
  // 是否就绪
  virtual bool ready() const = 0;

  // 设置源语言
  virtual void setSourceLanguage(Language lang) = 0;
  // 设置目标语言
  virtual void setTargetLanguage(Language lang) = 0;

  // 翻译文本
  virtual const char* translate(const char* text) = 0;

  // 获取最后的错误信息
  virtual const char* getLastError() const = 0;
};

// 翻译器后端类型(创建实例用,内部映射到工厂表字符串 key)
enum class TranslatorType { none, http, onnx };

// ============== Python 执行器接口 ==============
// (跨 DLL 安全: 仅 const char* + 原始类型, 不传 STL)
// 实现: SubprocessRunner (spawn 机器 python, 不嵌入 CPython, 不绑定版本; 编进 avox.dll 核心)
// 返回指针指向内部静态缓冲, 下次调用失效 (同 cmdExecuteLine 约定)
class IPyRunner {
 public:
  virtual ~IPyRunner() = default;

  // Python 是否可用 (PATH 上有 python/python3; 幂等, 首调触发探测并缓存)
  virtual bool available() = 0;

  // 跑 skill 脚本的 run(input) (脚本型 skill 用)
  // scriptPath: 脚本绝对路径 (skills/<name>/<script>.py); input: 传给 run(input)
  // 返回 run() 字符串结果 (内部持有, 下次调用失效); 失败返回 "FAIL: ..."
  virtual const char* runChain(const char* scriptPath, const char* input) = 0;

  // 内联 python 代码: exec(code) (input 变量可用)
  // 返回捕获的 stdout+stderr (+ 异常 traceback; 内部持有, 下次失效)
  virtual const char* runCode(const char* code, const char* input) = 0;

  // 按路径跑 .py: importlib 加载 (任意目录), 有 run() 则调 run(input)
  // 返回 stdout+stderr (+ run 返回值; 内部持有, 下次失效)
  virtual const char* runScript(const char* scriptPath, const char* input) = 0;
};

// ============== 配置键值存储 (JSON 选项) ==============
enum class ArgType { Null, Boolean, Int, Number, String, Array, Object };

class IOptionOb {
 public:
  IOptionOb() = default;
  virtual ~IOptionOb() = default;

 public:
  virtual void onOptionChange(const char* key, ArgType option){};
};

// 如果子类不同，设置需要很多不同键值对相关的设置，可内置Json对象(IOption实现)
// 约束使用，如果可能，尽量用确定的参数
class IOption {
 public:
  virtual ~IOption() {}
  virtual void setBool(const char* key, bool value) = 0;
  virtual void setInt(const char* key, int64_t value) = 0;
  virtual void setString(const char* key, const char* value) = 0;
  virtual void setNumber(const char* key, double value) = 0;

  // 如果确定key在,就直接用,否则先getType确定键值类型,再get
  virtual ArgType getType(const char* key) = 0;
  virtual int64_t getInt(const char* key) = 0;
  virtual double getDouble(const char* key) = 0;
  virtual const char* getString(const char* key) = 0;
  virtual bool getBool(const char* key) = 0;
};

// ============== 远程内容源统一接口 ==============
// 只读抽象, 统一"入口→目录树→选内容→播放"形态: 磁力/.torrent/WebDAV/SMB/Alist/
// 网盘/媒体服务器(Emby等)各实现一个。流程: open(入口)建会话 → list(token)异步列
// 子项(目录token继续下钻) → resolve(条目)产出可直接 IMediaPlayer::open 的规范 URL,
// 播放走现有协议路由, 播放核心零特判。list/search/listMore 共用同一结果批次。
// 跨 DLL 安全: 仅 const char* + 原始类型, 不传 STL (同 ITranslator 约束)。
// 实现: avox_torrent 插件 ("torrent"); 后续 avox_remote ("dav"/"smb"/"alist"/...);
// 未装对应插件时 createRemoteSource 返回 nullptr。
// 结果码 (onOpenResult/onListResult 的 code 参数; 0 成功, 负数失败, 同 AVError 风格)
enum class RemoteCode : int32_t {
  ok = 0,
  canceled = -1,    // 被 stopList/close 打断
  timeout = -2,     // 超时 (磁力元数据等慢源)
  authFailed = -3,  // 账密/token 错误
  authExpired = -4, // 会话中途鉴权过期 (UI 重新授权后重建会话)
  net = -5,         // 网络错误
  notFound = -6,    // 入口/节点不存在
  noSupport = -7,   // 协议/功能不支持
  other = -8,
};

// 条目类型 (dir 可继续下钻; media 命中媒体扩展名可直接 resolve)
enum class RemoteEntryType : int32_t { dir = 0, media = 1, file = 2, other = 3 };

// 能力位 (getCaps 返回按位或; UI 据此决定缩略图/搜索/断点续播等功能形态)
enum RemoteCap : uint32_t {
  kCapThumb = 1 << 0,    // getEntryThumb 有效 (网盘/Alist/媒体服务器)
  kCapSearch = 1 << 1,   // search 服务端搜索 (Alist/Emby/Jellyfin)
  kCapProgress = 1 << 2, // onListProgress 慢源进度 (磁力元数据等)
  kCapState = 1 << 3,    // 播放状态: 断点续播/已看标记 (媒体服务器)
  kCapMeta = 1 << 4,     // getEntryField 长尾元数据 (媒体服务器)
};

// 鉴权形态 (open 前声明, UI 据此渲染登录表单; OAuth 流程留在上层, SDK 只收 token)
enum class RemoteAuthKind : int32_t { none = 0, userPass = 1, token = 2, domain = 3 };

class IRemoteSourceOb {
 public:
  IRemoteSourceOb() = default;
  virtual ~IRemoteSourceOb() = default;

 public:
  // 会话建立结束 (open 工作线程回调, 上层自行切线程)
  virtual void onOpenResult(int32_t code) { (void)code; };
  // 一批结果就绪 (list/search/listMore 共用; 结果经 getEntry* 读取)
  virtual void onListResult(int32_t code) { (void)code; };
  // 慢源进度 0-100 (仅 kCapProgress 协议回调)
  virtual void onListProgress(int32_t percent) { (void)percent; };
};

class IRemoteSource {
 public:
  virtual ~IRemoteSource() = default;

 public:
  // 观察者 (单播, open 前设置)
  virtual void setOb(IRemoteSourceOb* ob) = 0;
  // 能力位 (RemoteCap 按位或)
  virtual uint32_t getCaps() = 0;
  // 鉴权形态
  virtual RemoteAuthKind getAuthKind() = 0;
  // 会话级参数 (open 前设置; 实现自定义键: torrent 用 "cacheDir"/"extraTrackers",
  // DAV 用 "verifyTls" 等; 通用性差的长尾参数走这里, 避免接口膨胀)
  virtual void setParam(const char* key, const char* value) { (void)key; (void)value; }

 public:
  // 建立会话: 磁力=引擎探元数据(慢, 有界 timeoutMs), DAV/SMB=连接, 网盘=验 token。
  // 完成经 onOpenResult; 返回 false = 有操作在进行(先 close/stopList)或参数非法。
  // user/pass: 账密形态; token: 网盘/媒体服务器鉴权 (上层完成 OAuth 后传入)。
  virtual bool open(const char* url, const char* user, const char* pass,
                    const char* token, int32_t timeoutMs) = 0;
  // 关闭会话并打断进行中的 list
  virtual void close() = 0;
  // 会话是否已建立 (open 成功与 close 之间为 true)
  virtual bool opened() = 0;

 public:
  // 列出节点子项: nodeToken 空 = 根。树协议按目录下钻, 磁力根 = 整包探测(慢)。
  // 完成经 onListResult; 返回 false = 有列表在进行(先 stopList)或会话未建立。
  // timeoutMs 仅对慢源生效, 快源(本地枚举)忽略。
  virtual bool list(const char* nodeToken, int32_t timeoutMs) = 0;
  // 服务端搜索, 结果进同一结果批次 (kCapSearch; 不支持返回 false)
  virtual bool search(const char* keyword, int32_t timeoutMs) { (void)keyword; (void)timeoutMs; return false; }
  // 追加下一批 (大目录分页, getEntryCount 增长; 无更多返回 false。
  // 页态由实现自持或编码进 token, 上层始终不解析 token)
  virtual bool listMore(int32_t timeoutMs) { (void)timeoutMs; return false; }
  // 打断进行中的列表/搜索 (结果作废, 不再回调)
  virtual void stopList() = 0;

 public:
  // ---- 结果批次 (onListResult(0) 后有效; 字符串为内部缓冲, 下次 list/析构前有效) ----
  virtual int32_t getEntryCount() = 0;
  virtual RemoteEntryType getEntryType(int32_t i) = 0;
  // 显示名 (文件名/目录名/媒体标题)
  virtual const char* getEntryName(int32_t i) = 0;
  // 字节数 (目录为子树合计, 未知为 0)
  virtual uint64_t getEntrySize(int32_t i) = 0;
  // 不透明节点句柄: 路径(磁力/SMB/DAV)或服务端ID(网盘/媒体服务器), 作 list 下钻凭证
  virtual const char* getEntryToken(int32_t i) = 0;
  // 缩略图/海报 URL (kCapThumb; 无则空)
  virtual const char* getEntryThumb(int32_t i) { (void)i; return ""; }
  // 协议长尾元数据 (kCapMeta; 键约定见各实现文档: "year"/"rating"/"overview"...)
  virtual const char* getEntryField(int32_t i, const char* key) { (void)i; (void)key; return ""; }
  // 会话级信息键值 (如 torrent 的 "name"/"infoHash"/"totalSize"; 无则空)
  virtual const char* getSessionField(const char* key) { (void)key; return ""; }

 public:
  // ---- 选择播放 (统一出口; 本接口只读, 不含任何写操作) ----
  // 把条目解析为可直接 IMediaPlayer::open 的规范 avox URL 并返回 (如 https 直链/
  // smb://.../磁力原链), 同时把偏好写入 option (协议自读自键, 磁力即 torrent.fileIndex)。
  // 特殊容器在此翻译: 磁力单文件/蓝光原盘(BDMV)文件夹/剧集聚合, 语义同 Kodi Resolve。
  // 返回内部缓冲, 下次调用失效; 条目不可播返回 nullptr (原因见 getLastError)。
  virtual const char* resolve(int32_t entryIndex, IOption* option) = 0;
  // 解析结果过期重取 (网盘直链有时效; 磁力/SMB 无需, 返回 nullptr)
  virtual const char* refresh(int32_t entryIndex, IOption* option) { (void)entryIndex; (void)option; return nullptr; }

 public:
  // ---- 播放状态 (kCapState: 媒体服务器断点续播/已看; 其余默认不支持) ----
  virtual uint64_t getResumeMs(int32_t entryIndex) { (void)entryIndex; return 0; }
  virtual bool reportProgress(int32_t entryIndex, uint64_t posMs, uint64_t durMs) { (void)entryIndex; (void)posMs; (void)durMs; return false; }
  virtual bool markWatched(int32_t entryIndex, bool watched) { (void)entryIndex; (void)watched; return false; }

  // 最后一次错误描述 (内部缓冲, 下次调用失效)
  virtual const char* getLastError() = 0;
};

extern "C" {
// ============== 网络 ==============
AVOX_EXPORT bool parseIP4Address(const char* str, IP4Address& ip4Address);
AVOX_EXPORT bool parseIP4Endpoint(const char* str, IP4Endpoint& pr4Endpoint);

// ============== 路径 ==============
// avox 运行目录 (avox.dll/exe 装载目录)。返回内部静态缓存, 路径恒定, 线程安全。
// C/SWIG 调用入口; C++ 侧用 Avox.hpp 的 std::string getAvoxPath()。
AVOX_EXPORT const char* getAvoxRunDir();

// ============== 翻译器 ==============
// 检查插件是否被加载
AVOX_EXPORT void checkModelLoad(const char* modelName);
// 创建翻译器(后端未注册/none 返回 nullptr)
AVOX_EXPORT ITranslator* createTranslator(TranslatorType type);

// ============== 远程内容源 ==============
// 创建远程内容源会话 (type: "torrent" 磁力/BT; 后续 "dav"/"smb"/"alist"...;
// 插件未装/未知类型返回 nullptr)。返回需释放的内存 (create* 约定)。
AVOX_EXPORT IRemoteSource* createRemoteSource(const char* type);

// ============== Python 执行器 ==============
// 拿 Python 执行器全局单例 (static SubprocessRunner, 编进 avox.dll; 首次调用 lazy 创建)
// 不 delete (进程生命周期)。Python 不可用时 available() 返回 false, 各调用方降级。
AVOX_EXPORT IPyRunner* getPyRunner();

// ============== 配置 ==============
AVOX_EXPORT IOption* createJsonOption();
}

}