#include "avox_cmd/CmdExecute.h"
#include "avox_cmd/CmdRegistry.hpp"
#include "avox_cmd/Shell.hpp"
#include "avox_cmd/ConsoleWin32.hpp"
#include "avox/AvoxVersion.h"
#include "avox/Avox.hpp"  // ensurePythonPath
#include "avox/module/AvoxManager.hpp"  // AvoxManager::clean (cmdCliFinalize)

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace avox {

// 统一命令入口: 注册子命令 + 全局选项 + 派发。
// argv 约定同 C main: argv[0] 程序名, argv[1] 子命令名或 -help/-version。
int cmdExecute(int32_t argc, const char* const* argv) {
#ifdef _WIN32
    // Windows 控制台默认 GBK, 设为 UTF-8 以正确显示中文路径/日志 (动态解析见 ConsoleWin32.hpp)
    conSetUtf8Codepage();
#endif
    // 注册 AVOX_HOME 环境变量 + avox.pth, 让外部 Python 能 import avox (幂等)
    ensurePythonPath();
    // 共用 builtinCmdRegistry 单例 (cli 自身的 Shell 与帮助同源一份命令注册)
    CmdRegistry& registry = builtinCmdRegistry();
    // 无参数: 打印帮助, 进入交互式 Shell
    if (argc < 2) {
        printf("%s", registry.helpText().c_str());
        return Shell::run(registry);
    }
    // 全局选项
    std::string first = argv[1];
    if (first == "-help" || first == "--help") {
        printf("%s", registry.helpText().c_str());
        return 0;
    }
    if (first == "-version" || first == "--version") {
        printf("avox_cli %s\n", AVOX_COMMIT_VERSION);
        return 0;
    }
    // 查找并执行子命令 (argv[1] 为子命令名, argv[2..] 为子命令参数)
    return registry.execute(argc - 1, argv + 1);
}

// avox_cli 退出收尾, 详见 CmdExecute.h 声明处注释(根因链在那里)。
// 只允许独立进程的 CLI 在 main 返回前调用; 嵌入宿主严禁。
void cmdCliFinalize(int exitCode) {
    static bool bFinalized = false;
    if (bFinalized) {
        return;
    }
    bFinalized = true;
    // TerminateProcess 不走 CRT 退出路径, 先手动刷 stdio 防 CLI 输出截断
    fflush(nullptr);
    // 显式清理(cleanFuncs + 停异步日志线程), 与 DllMain(DETACH) 里的调用同一份,
    // 此处先跑后, DETACH 版本变成无害重复(mk_env_release 有 s_env_inited 守卫)
    AvoxManager::Get().clean();
    fflush(nullptr);
#ifdef _WIN32
    // 关键: 跳过 ExitProcess 的 DLL_PROCESS_DETACH 链, 规避 mk_api.dll
    // 静态析构在"其余线程已被杀"环境里的 wepoll reflock 无限等待(退出僵尸)
    TerminateProcess(GetCurrentProcess(), (UINT)exitCode);
#endif
}

namespace {

// cmdExecuteLine 的内部缓存 (返回指针的持有 + 上次退出码)。非 thread_local:
// chain 脚本顺序执行, 同 chainResultToVlmText 一致即可。
std::string g_lineOutput;
int g_lineExitCode = 0;

// 拆命令行: 空白分隔, 双引号包裹含空白的整体参数 (引号内不转义, 足够 URL/路径用)。
// 例: play -i rtsp://a b -t "15" → [play, -i, rtsp://a, b, -t, 15]
std::vector<std::string> splitCmdline(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cur;
    bool inQuote = false;
    bool hasToken = false;
    for (char c : line) {
        if (c == '"') {
            inQuote = !inQuote;
            hasToken = true;   // 空引号 "" 也算一个空 token
            continue;
        }
        if (!inQuote && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            if (hasToken) {
                tokens.push_back(cur);
                cur.clear();
                hasToken = false;
            }
            continue;
        }
        cur.push_back(c);
        hasToken = true;
    }
    if (hasToken) tokens.push_back(cur);
    return tokens;
}

// 临时把 fd1/fd2 重定向到同一文件捕获 printf, 完成后还原并读回内容 (ChainRunner StdoutRedirect 同思路)。
class CombinedRedirect {
 public:
    bool begin(const std::string& tmpPath) {
        path = tmpPath;
        fflush(stdout);
        fflush(stderr);
#ifdef _WIN32
        savedOut = _dup(1);
        savedErr = _dup(2);
        if (savedOut == -1 || savedErr == -1) return false;
        int fd = _open(tmpPath.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY,
                       _S_IREAD | _S_IWRITE);
        if (fd == -1) return false;
        _dup2(fd, 1);
        _dup2(fd, 2);
        _close(fd);
#else
        savedOut = dup(1);
        savedErr = dup(2);
        if (savedOut == -1 || savedErr == -1) return false;
        int fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) return false;
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
#endif
        return true;
    }
    std::string end() {
        fflush(stdout);
        fflush(stderr);
#ifdef _WIN32
        if (savedOut != -1) { _dup2(savedOut, 1); _close(savedOut); savedOut = -1; }
        if (savedErr != -1) { _dup2(savedErr, 2); _close(savedErr); savedErr = -1; }
#else
        if (savedOut != -1) { dup2(savedOut, 1); close(savedOut); savedOut = -1; }
        if (savedErr != -1) { dup2(savedErr, 2); close(savedErr); savedErr = -1; }
#endif
        std::string content;
        std::ifstream f(path, std::ios::binary);
        if (f.is_open()) {
            std::stringstream ss;
            ss << f.rdbuf();
            content = ss.str();
        }
        std::remove(path.c_str());
        return content;
    }

 private:
    std::string path;
    int savedOut = -1;
    int savedErr = -1;
};

std::string makeTempPath() {
#ifdef _WIN32
    char tmpDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpDir);
    return std::string(tmpDir) + "avox_cmdline.txt";
#else
    const char* tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";
    return std::string(tmp) + "/avox_cmdline.txt";
#endif
}
}  // namespace

const char* cmdExecuteLine(const char* cmdline) {
    g_lineOutput.clear();
    g_lineExitCode = 0;
    if (!cmdline || !cmdline[0]) return g_lineOutput.c_str();
    std::vector<std::string> tokens = splitCmdline(cmdline);
    if (tokens.empty()) return g_lineOutput.c_str();
    // argv[0] 占位 ("avox_cmd"), cmdExecute 跳过; argv[1..] = tokens
    std::vector<const char*> argv;
    argv.push_back("avox_cmd");
    for (auto& t : tokens) argv.push_back(t.c_str());
    std::string tmp = makeTempPath();
    CombinedRedirect redir;
    bool captured = redir.begin(tmp);
    int code = -1;
    try {
        code = cmdExecute(static_cast<int32_t>(argv.size()), argv.data());
    } catch (...) {
        code = -1;
    }
    if (captured) {
        g_lineOutput = redir.end();
    } else {
        // 重定向失败 (极罕见): 至少拿到退出码
        std::remove(tmp.c_str());
    }
    g_lineExitCode = code;
    return g_lineOutput.c_str();
}

int cmdExecuteLastExitCode() { return g_lineExitCode; }

}
