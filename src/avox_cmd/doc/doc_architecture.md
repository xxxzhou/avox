# 框架设计 — ArgParser + CommandRegistry

## 实现方案

采用 **方案 B: 子命令注册表 + 轻量解析框架**，自包含，不引入外部依赖。

## 核心类

### Arg — 参数定义

ArgType 使用项目已有的定义 (`src/avox/AvoxBase.h`)，ArgParser 只用其中 4 种:

| ArgType (项目) | ArgParser 用途 |
|----------------|---------------|
| `Boolean` | 开关型: `-lowlatency`, `-detect` |
| `String`  | 字符串: `-i url`, `-o file` |
| `Int`     | 整数: `-t 60`, `-delay 100` |
| `Number`  | 浮点: `-speed 1.5` |

> `Null`/`Array`/`Object` 为 JSON 模块使用，ArgParser 不涉及。

```cpp
#include "avox/AvoxBase.h"  // ArgType

struct Arg {
    std::string shortName;  // "-i"
    std::string longName;   // "--input"
    ArgType type;           // Boolean / String / Int / Number
    bool required;          // 是否必填
    std::string desc;       // 帮助描述
    std::string defaultVal; // 默认值 (字符串表示)
};
```

### ParsedArgs — 解析结果

```cpp
class ParsedArgs {
public:
    bool has(const std::string& name) const;           // 是否存在
    std::string getString(const std::string& name) const;
    int getInt(const std::string& name) const;
    float getFloat(const std::string& name) const;
    bool getBool(const std::string& name) const;
    const std::vector<std::string>& positional() const; // 位置参数
};
```

### ArgParser — 参数解析器

```cpp
class ArgParser {
public:
    void addArg(const Arg& arg);
    ParsedArgs parse(int argc, char** argv) const;
    std::string helpText(const std::string& cmdName) const;
};
```

### Command — 子命令

```cpp
struct Command {
    std::string name;        // "play"
    std::string desc;        // "播放音视频"
    ArgParser parser;        // 参数定义
    std::function<int(const ParsedArgs&)> run;  // 执行函数
};
```

### CommandRegistry — 子命令注册表

```cpp
class CommandRegistry {
public:
    void add(Command cmd);
    const Command* find(const std::string& name) const;
    std::string helpText() const;
    int execute(int argc, char** argv) const;
};
```

## main.cpp 流程

```cpp
int main(int argc, char** argv) {
    // 1. 注册所有子命令
    CommandRegistry registry;
    registry.add(cmdDevice());
    registry.add(cmdPlay());
    registry.add(cmdRecord());
    // ...

    // 2. 全局选项处理
    if (argc < 2) {
        printf("%s", registry.helpText().c_str());
        return 1;
    }
    std::string first = argv[1];
    if (first == "-help" || first == "--help") {
        printf("%s", registry.helpText().c_str());
        return 0;
    }
    if (first == "-version" || first == "--version") {
        printf("avox_cli %s\n", AVOX_VERSION);
        return 0;
    }

    // 3. 查找并执行子命令
    auto* cmd = registry.find(first);
    if (!cmd) {
        fprintf(stderr, "Unknown command: %s\n", first.c_str());
        fprintf(stderr, "Run 'avox_cli -help' for usage.\n");
        return 1;
    }

    // 4. 子命令的 argv 从 argv[2] 开始
    return registry.execute(argc - 1, argv + 1);
}
```

## 帮助输出示例

```
$ avox_cli -help
avox_cli - avplay command line tool

Usage: avox_cli <command> [options]

Commands:
  device      枚举设备
  play        播放音视频
  record      录制/转存流
  transcode   转码录制
  inpaint     AI 去水印
  translate   翻译文本
  asr         语音识别
  webrtc      WebRTC 推拉流
  image       图片处理
  agent       AI 对话
  vulkan      Vulkan 信息

Global options:
  -loglevel <level>   日志级别: quiet/error/warning/info/verbose/debug
  -version            显示版本
  -help               显示帮助

Run 'avox_cli <command> -help' for command details.
```

## 新增子命令流程

1. 创建 `commands/CmdXxx.h` + `commands/CmdXxx.cpp`
2. 在 `CmdXxx.cpp` 中定义参数和执行逻辑:

```cpp
Command cmdXxx() {
    Command cmd;
    cmd.name = "xxx";
    cmd.desc = "功能描述";
    cmd.parser.addArg({"-i", "--input", ArgType::String, true, "输入源", ""});
    // ...
    cmd.run = [](const ParsedArgs& args) -> int {
        // 实现逻辑
        return 0;
    };
    return cmd;
}
```

3. 在 `main.cpp` 中注册: `registry.add(cmdXxx());`

## 待讨论

1. **信号处理**: Ctrl+C 优雅退出，在 main 层统一注册 `signal(SIGINT, ...)`，子命令通过回调获知中断
2. **退出码**: 统一定义 — 0=成功, 1=参数错误, 2=IO错误, 3=编码错误, ...
3. **日志格式**: 是否采用 ffmpeg 风格 `[模块名 @ 地址] 消息`?
