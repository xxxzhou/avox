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

// ============== 数据源探测 (磁力/BT 文件列表等) ==============
// 给定一个链接, 异步取其内部文件列表(如磁力多文件), 选择文件后写回播放选项。
// 跨 DLL 安全: 仅 const char* + 原始类型, 不传 STL (同 ITranslator 约束)。
// 实现: avox_torrent 插件 ("torrent"); 未装插件时 createSourceProbe 返回 nullptr。
class ISourceProbeOb {
 public:
  ISourceProbeOb() = default;
  virtual ~ISourceProbeOb() = default;

 public:
  // 探测结束 (探测工作线程回调, 上层自行切线程; code=0 成功, 非 0 详见 getLastError)
  virtual void onProbeResult(int32_t code) { (void)code; };
};

class ISourceProbe {
 public:
  virtual ~ISourceProbe() = default;

 public:
  // 观察者 (单播, start 前设置)
  virtual void setOb(ISourceProbeOb* ob) = 0;
  // 异步探测: 只等元数据不下载(有界 timeoutMs), 完成经 onProbeResult 回调, 本调用立即返回。
  // url: "magnet:?" 开头 或 本地 .torrent 路径。
  // cacheDir: 缓存目录, 与播放选项 torrent.cacheDir 传同值可让起播命中元数据缓存, 空用系统默认。
  // 返回 false = 上一轮探测未结束(先 stop)。
  virtual bool start(const char* url, const char* cacheDir,
                     int32_t timeoutMs) = 0;
  // 中止探测并回收工作线程 (可再次 start)
  virtual void stop() = 0;
  // 是否有探测在进行 (start 与 probe_result 之间为 true)
  virtual bool probing() = 0;

  // ---- 结果读取 (onProbeResult(0) 后有效; 字符串为内部缓冲, stop/析构前有效) ----
  virtual int32_t getFileCount() = 0;
  // 第 i 项在种子内的原始文件索引 (播放选项 torrent.fileIndex 用这个值)
  virtual int32_t getFileIndex(int32_t i) = 0;
  // 种子内相对路径
  virtual const char* getFilePath(int32_t i) = 0;
  virtual uint64_t getFileSize(int32_t i) = 0;
  // 是否命中内置媒体扩展名表 (.mp4/.mkv/.ts/...)
  virtual bool isMediaFile(int32_t i) = 0;
  // 种子名 / info-hash 十六进制 / 全部文件总字节
  virtual const char* getName() = 0;
  virtual const char* getInfoHash() = 0;
  virtual uint64_t getTotalSize() = 0;
  virtual const char* getLastError() = 0;

  // ---- 文件选择 (播放交互) ----
  virtual void selectFile(int32_t fileIndex) = 0;
  virtual int32_t getSelectedIndex() = 0;
  // 把选择写进播放选项 (torrent.fileIndex), 之后 IMediaPlayer::open(同 url) 即播该文件;
  // 未选择时 open 走默认规则(自动选最大媒体文件)
  virtual bool applyToOption(IOption* option) = 0;
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

// ============== 数据源探测 ==============
// 创建数据源探测器 (type: "torrent" 磁力/BT 文件列表; 插件未装/未知类型返回 nullptr)。
// 返回需释放的内存 (create* 约定)。
AVOX_EXPORT ISourceProbe* createSourceProbe(const char* type);

// ============== Python 执行器 ==============
// 拿 Python 执行器全局单例 (static SubprocessRunner, 编进 avox.dll; 首次调用 lazy 创建)
// 不 delete (进程生命周期)。Python 不可用时 available() 返回 false, 各调用方降级。
AVOX_EXPORT IPyRunner* getPyRunner();

// ============== 配置 ==============
AVOX_EXPORT IOption* createJsonOption();
}

}