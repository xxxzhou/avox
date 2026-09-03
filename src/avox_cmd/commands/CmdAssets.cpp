/**
 * @file CmdAssets.cpp
 * @brief assets 子命令 - 管理插件运行时资源 (AI 模型 + 运行时 DLL)
 *
 * C++ 直接读 assets/script/assets_manifest.json + 扫描 plugins/ 比对缺失;
 * 下载时调 fetch_assets.py download (spawn 机器 python, stderr 直通控制台显示进度条)。
 * 不走 Python 做逻辑判断, Python 只负责下载。
 */

#include "CmdAssets.hpp"

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "avox/Avox.hpp"           // getAvoxPath
#include "avox/AvoxBase.h"       // getPyRunner (runCode, 下载进度条)
#include "avox/module/Json.hpp"   // parserJson
#include "avox_cmd/CmdHelper.hpp"  // cmdReadLine

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace avox {

namespace {
// ---------- 插件扫描 ----------
std::set<std::string> scanInstalledPlugins(const std::string& root) {
  std::set<std::string> result;
  fs::path pluginsDir = fs::path(root) / "plugins";
  if (!fs::is_directory(pluginsDir)) return result;
#ifdef _WIN32
  for (const auto& entry : fs::directory_iterator(pluginsDir)) {
    std::string name = entry.path().filename().string();
    if (name.rfind("avox_", 0) == 0 && name.size() > 4 &&
        name.substr(name.size() - 4) == ".dll") {
      result.insert(name.substr(0, name.size() - 4));
    }
  }
#else
  for (const auto& entry : fs::directory_iterator(pluginsDir)) {
    std::string name = entry.path().filename().string();
    if (name.rfind("libavox_", 0) == 0 && name.size() > 6 &&
        name.substr(name.size() - 3) == ".so") {
      result.insert(name.substr(3, name.size() - 6));
    }
  }
#endif
  return result;
}

// ---------- 平台检测 ----------
std::string detectPlatform() {
#ifdef _WIN32
  return "windows";
#elif __APPLE__
  return "macos";
#elif __linux__
  return "linux";
#else
  return "unknown";
#endif
}

// ---------- 单项状态 ----------
struct AssetItem {
  std::string id;
  std::string name;
  std::string plugin;
  std::string type;       // model / library
  std::string method;     // download / script / manual / build
  std::string dest;
  bool applicable = true;
  bool ready = false;
  bool hasReady = false;  // verify_files 存在, ready 有意义
  std::vector<std::string> missingFiles;
};

// ---------- 从 manifest JSON 构建 item 列表 ----------
std::vector<AssetItem> loadAssetItems(const Json& manifest, const std::string& platform,
                                       const std::string& root,
                                       const std::string& pluginFilter) {
  std::vector<AssetItem> items;
  if (!manifest.bObject() || !manifest.find("items")) return items;
  const Json& arr = manifest["items"];
  if (!arr.bArray()) return items;
  for (size_t i = 0; i < arr.size(); ++i) {
    const Json& it = arr[i];
    if (!it.bObject()) continue;
    AssetItem ai;
    auto strField = [&](const char* key) -> std::string {
      return (it.find(key) && it[key].bString()) ? it[key].get<std::string>() : "";
    };
    ai.id = strField("id");
    ai.name = strField("name");
    ai.plugin = strField("plugin");
    ai.type = strField("type");
    ai.method = strField("method");
    ai.dest = strField("dest");
    if (!pluginFilter.empty() && ai.plugin != pluginFilter) continue;
    // 平台解析
    if (it.find("platforms") && it["platforms"].bObject()) {
      const Json& plats = it["platforms"];
      if (plats.find(platform) && plats[platform].bObject()) {
        const Json& spec = plats[platform];
        // spec 可覆盖 method
        if (spec.find("method") && spec["method"].bString()) {
          ai.method = spec["method"].get<std::string>();
        }
        // 用平台的 verify_files
        if (spec.find("verify_files") && spec["verify_files"].bArray()) {
          const Json& vf = spec["verify_files"];
          ai.hasReady = vf.size() > 0;
          fs::path destDir = fs::path(root) / ai.dest;
          for (size_t v = 0; v < vf.size(); ++v) {
            if (vf[v].bString()) {
              std::string fn = vf[v].get<std::string>();
              if (!fs::exists(destDir / fn)) ai.missingFiles.push_back(fn);
            }
          }
          ai.ready = ai.hasReady && ai.missingFiles.empty();
        }
      } else {
        ai.applicable = false;
      }
    } else {
      // 平台无关: 用顶层 verify_files
      if (it.find("verify_files") && it["verify_files"].bArray()) {
        const Json& vf = it["verify_files"];
        ai.hasReady = vf.size() > 0;
        fs::path destDir = fs::path(root) / ai.dest;
        for (size_t v = 0; v < vf.size(); ++v) {
          if (vf[v].bString()) {
            std::string fn = vf[v].get<std::string>();
            if (!fs::exists(destDir / fn)) ai.missingFiles.push_back(fn);
          }
        }
        ai.ready = ai.hasReady && ai.missingFiles.empty();
      }
    }
    items.push_back(std::move(ai));
  }
  return items;
}

// ---------- 列表显示 ----------
void printAssetList(const std::vector<AssetItem>& items,
                     const std::set<std::string>& installedPlugins,
                     const std::string& platform) {
  printf("当前平台: %s  |  已安装插件: ", platform.c_str());
  bool first = true;
  for (const auto& p : installedPlugins) {
    if (!first) printf(", ");
    printf("%s", p.c_str());
    first = false;
  }
  if (first) printf("(无)");
  printf("\n");
  printf("  #   类型    插件           id                      方式      状态   缺失文件              名称\n");
  printf("  %s\n", std::string(115, '-').c_str());
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& it = items[i];
    if (!it.applicable) continue;
    const char* stat = it.hasReady ? (it.ready ? "\033[32m就绪\033[0m" : "\033[33m缺失\033[0m") : "\033[2m—\033[0m";
    std::string missStr;
    for (size_t m = 0; m < it.missingFiles.size(); ++m) {
      if (m > 0) missStr += ", ";
      missStr += it.missingFiles[m];
    }
    if (missStr.size() > 30) missStr = missStr.substr(0, 27) + "...";
    printf("%3zu  %-7s %-14s %-22s %-9s %s  %-20s %s\n",
           i + 1, it.type.c_str(), it.plugin.c_str(), it.id.c_str(),
           it.method.c_str(), stat, missStr.c_str(), it.name.c_str());
  }
  printf("\n共 %zu 项。\n", items.size());
}

// ---------- 交互选择 ----------
std::vector<size_t> interactiveSelect(const std::vector<AssetItem>& items) {
  std::set<size_t> selected;
  // 默认选中所有缺失项
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].applicable && items[i].hasReady && !items[i].ready) {
      selected.insert(i);
    }
  }
  while (true) {
    printf("\n");
    for (size_t i = 0; i < items.size(); ++i) {
      const auto& it = items[i];
      if (!it.applicable) continue;
      const char* mark = selected.count(i) ? "\033[32m[x]\033[0m" : "\033[2m[ ]\033[0m";
      const char* stat = it.hasReady ? (it.ready ? "\033[32m就绪\033[0m" : "\033[33m缺失\033[0m") : "\033[2m—\033[0m";
      printf("  %s %3zu. [%s] %-13s %s %s\n", mark, i + 1, it.type.c_str(),
             it.plugin.c_str(), stat, it.name.c_str());
    }
    printf("\n  输入编号切换 (如 1 3 5), a=全选缺失, c=全不选, 空回车=下载所选([x]项), q=退出\n");
    std::string raw;
    if (!cmdReadLine(raw)) return {};
    // trim
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.pop_back();
    // lowercase
    for (auto& c : raw) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (raw == "q" || raw == "quit" || raw == "exit") return {};
    if (raw.empty()) {
      if (selected.empty()) {
        printf("  未选中任何项。\n");
        continue;
      }
      return {selected.begin(), selected.end()};
    }
    if (raw == "a") {
      for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].applicable) selected.insert(i);
      }
      continue;
    }
    if (raw == "c") {
      selected.clear();
      continue;
    }
    // 解析编号
    std::string tok;
    for (size_t p = 0; p <= raw.size(); ++p) {
      char c = p < raw.size() ? raw[p] : ' ';
      if (c == ',' || c == ' ') {
        if (!tok.empty()) {
          int idx = std::atoi(tok.c_str()) - 1;
          if (idx >= 0 && static_cast<size_t>(idx) < items.size()) {
            if (selected.count(static_cast<size_t>(idx)))
              selected.erase(static_cast<size_t>(idx));
            else
              selected.insert(static_cast<size_t>(idx));
          } else {
            printf("  忽略越界编号: %s\n", tok.c_str());
          }
          tok.clear();
        }
      } else {
        tok += c;
      }
    }
  }
}

// ---------- 路径转义 (Windows 反斜杠 → Python 安全格式) ----------
std::string pyPath(const std::string& path) {
  std::string r = path;
  // Windows 反斜杠在 Python exec 字符串里被当转义, 替换为正斜杠 (Python 接受)
  for (auto& c : r) {
    if (c == '\\') c = '/';
  }
  return r;
}

// ---------- 调 fetch_assets.py 下载 ----------
int downloadAssets(const std::string& scriptPath, const std::string& selectedIds,
                    const std::string& root, const std::string& platform, bool force) {
  // 设环境变量, 让 _avox_run 的 stderr 直通真实控制台 (进度条实时显示)
#ifdef _WIN32
  SetEnvironmentVariableA("AVOX_PY_STDERR_PASSTHROUGH", "1");
#endif
  // 构造内联代码: 设 sys.argv + __file__ → exec fetch_assets.py → catch SystemExit
  // 路径用正斜杠 (Windows 反斜杠在 Python exec 里被当转义)
  std::string pyRoot = pyPath(root);
  std::string pyScript = pyPath(scriptPath);
  std::string code =
      "import sys\n"
      "sys.argv = ['fetch_assets.py', 'download', '--select', '" + selectedIds +
      "', '--root', '" + pyRoot + "', '--platform', '" + platform + "'";
  if (force) code += ", '--force'";
  code += ", '--no-color']\n"
          "__file__ = r'" + pyScript + "'\n"
          "try:\n"
          "    exec(open(r'" + pyScript + "').read())\n"
          "except SystemExit:\n"
          "    pass\n";
  IPyRunner* py = getPyRunner();
  std::string result = py ? py->runCode(code.c_str(), "") : "FAIL: no python found on PATH";
#ifdef _WIN32
  SetEnvironmentVariableA("AVOX_PY_STDERR_PASSTHROUGH", nullptr);
#endif
  // 结果在 stdout (StringIO 捕获), 进度在 stderr (已直通控制台)
  if (result.rfind("FAIL:", 0) == 0) {
    fprintf(stderr, "%s\n", result.c_str());
    return 1;
  }
  // 成功时 result 可能为空或含 JSON 汇总
  if (!result.empty()) {
    // stdout 捕获的内容也打印 (如 --json 结果)
    printf("%s", result.c_str());
    if (result.back() != '\n') printf("\n");
  }
  return 0;
}

// ---------- 拼接选中 id ----------
std::string joinIds(const std::vector<AssetItem>& items, const std::vector<size_t>& indices) {
  std::string ids;
  for (size_t i = 0; i < indices.size(); ++i) {
    if (i > 0) ids += ",";
    ids += items[indices[i]].id;
  }
  return ids;
}
}  // namespace

Command cmdAssets() {
  Command cmd;
  cmd.name = "assets";
  cmd.desc = "插件资源管理: -l 列缺失项 / -i 交互下载 / --all 全下 / -s 指定下载";
  cmd.parser.addArg({"-l", "--list", ArgType::Boolean, false,
                     "列出所有项及就绪状态", ""});
  cmd.parser.addArg({"-i", "--interactive", ArgType::Boolean, false,
                     "交互式多选下载", ""});
  cmd.parser.addArg({"", "--all", ArgType::Boolean, false,
                     "下载所有缺失项", ""});
  cmd.parser.addArg({"-s", "--select", ArgType::String, false,
                     "逗号分隔的 item id 列表", ""});
  cmd.parser.addArg({"", "--plugin", ArgType::String, false,
                     "按插件过滤 (如 avox_sherpa)", ""});
  cmd.parser.addArg({"", "--force", ArgType::Boolean, false,
                     "强制重新下载 (忽略已存在)", ""});
  cmd.parser.addArg({"", "--root", ArgType::String, false,
                     "部署根目录 (默认 avox.dll 同级目录)", ""});
  cmd.run = [](const ParsedArgs& args) -> int {
    std::string root = args.getString("root");
    if (root.empty()) root = getAvoxPath();
    std::string platform = detectPlatform();
    std::string pluginFilter = args.getString("plugin");
    bool force = args.getBool("force");
    // 找 manifest (规范位置 <root>/assets/script/; avox.dll 在子目录时退一级)
    fs::path manifestPath = fs::path(root) / "assets" / "script" / "assets_manifest.json";
    if (!fs::exists(manifestPath)) {
      manifestPath = fs::path(root) / ".." / "assets" / "script" / "assets_manifest.json";
    }
    if (!fs::exists(manifestPath)) {
      fprintf(stderr, "找不到清单: %s\n", manifestPath.string().c_str());
      return 1;
    }
    // 读 manifest
    std::ifstream ifs(manifestPath.string());
    if (!ifs.is_open()) {
      fprintf(stderr, "无法打开清单: %s\n", manifestPath.string().c_str());
      return 1;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();
    Json manifest = parserJson(content.c_str());
    // 扫描已安装插件
    auto installedPlugins = scanInstalledPlugins(root);
    // 构建项列表
    auto items = loadAssetItems(manifest, platform, root, pluginFilter);
    if (items.empty()) {
      printf("清单无适用项 (平台: %s)。\n", platform.c_str());
      return 0;
    }
    // 仅列表
    bool wantList = args.getBool("list");
    bool wantInteractive = args.getBool("interactive");
    bool wantAll = args.getBool("all");
    std::string selectIds = args.getString("select");
    if (wantList && !wantInteractive && !wantAll && selectIds.empty()) {
      printAssetList(items, installedPlugins, platform);
      return 0;
    }
    // 确定要下载的项
    std::vector<size_t> chosen;
    if (wantInteractive) {
      chosen = interactiveSelect(items);
      if (chosen.empty()) {
        printf("已取消。\n");
        return 0;
      }
    } else if (!selectIds.empty()) {
      // 解析逗号分隔 id
      std::set<std::string> wanted;
      std::string tok;
      for (size_t p = 0; p <= selectIds.size(); ++p) {
        char c = p < selectIds.size() ? selectIds[p] : ',';
        if (c == ',') {
          while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) tok.pop_back();
          if (!tok.empty()) wanted.insert(tok);
          tok.clear();
        } else {
          tok += c;
        }
      }
      for (size_t i = 0; i < items.size(); ++i) {
        if (wanted.count(items[i].id)) chosen.push_back(i);
      }
      auto foundIds = std::set<std::string>();
      for (auto idx : chosen) foundIds.insert(items[idx].id);
      for (const auto& w : wanted) {
        if (!foundIds.count(w)) fprintf(stderr, "未知 id: %s\n", w.c_str());
      }
    } else if (wantAll) {
      for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].applicable && items[i].hasReady && !items[i].ready) {
          chosen.push_back(i);
        }
      }
      if (chosen.empty()) {
        printf("所有项已就绪, 无需下载。\n");
        return 0;
      }
    } else {
      // 无动作: 显示列表 + 提示
      printAssetList(items, installedPlugins, platform);
      printf("\n用 -i 交互选择, --all 下载全部缺失, -s id1,id2 下载指定项。\n");
      return 0;
    }
    if (chosen.empty()) {
      printf("未选中任何可下载项。\n");
      return 0;
    }
    // 找 fetch_assets.py (规范位置 <root>/assets/script/; avox.dll 在子目录时退一级)
    fs::path scriptPath = fs::path(root) / "assets" / "script" / "fetch_assets.py";
    if (!fs::exists(scriptPath)) {
      scriptPath = fs::path(root) / ".." / "assets" / "script" / "fetch_assets.py";
    }
    if (!fs::exists(scriptPath)) {
      fprintf(stderr, "找不到脚本: %s\n", scriptPath.string().c_str());
      return 1;
    }
    // 拼接 id
    std::string ids = joinIds(items, chosen);
    // 执行下载
    printf("下载 %zu 项: %s\n", chosen.size(), ids.c_str());
    int rc = downloadAssets(scriptPath.string(), ids, root, platform, force);
    return rc;
  };
  return cmd;
}

}
