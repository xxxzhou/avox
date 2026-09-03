#include "player.h"
#include "source_player.h"
#include "source_probe.h"
#include "recorder.h"
#ifdef _WIN32
#include "voice.h"  // 全局热键/文本注入 Win32 专属 (Android 暂无对应概念)
#endif
#ifdef AVOX_ENABLE_AGENT
#include "agent.h"
#endif
#include "voice.h"
#include "face.h"
#include "video_face.h"
#include "body.h"
#include "agent.h"
#include "image.h"
#include "option.h"
#include "gpu_passthrough.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdlib>
#include <mutex>
#include <cstring>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>

#include <avox/AvoxLog.h>
#include <avox/AvoxBase.h>

#ifdef __ANDROID__
// ── Android 引导依赖: 插件目录解压 + AndroidEnv 接线 ──
#include <dlfcn.h>
#include <jni.h>
#include <android/asset_manager_jni.h>
#include <elf.h>

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/core/memory.hpp>

#include <vector>
#include <cstdint>

#include <avox/module/ModuleMgr.hpp>
#include <avox/module/AvoxManager.hpp>  // AndroidEnv/initAndroid (内部头, 仅 Android 分支引入)

// AndCommon.cpp 的 JNI 方法 ID 注册段 (常规路经 AvoxJava 的 jniSetup 触发,
// Godot 无该 Java 入口, 引导里显式调; 类本体由 patch_apk 注入 classesN.dex)
namespace avox {
extern "C" {
jint getAudiotrackFields();
jint getAudioRecordFields();
jint getSurfaceTextureFields();
jint getSurfaceFields();
jint getSurfaceTextureObFields();
}
}  // namespace avox
#endif

#define VK_NO_PROTOTYPES
#include <volk.h>

using namespace godot;

// ── GPU 直通可用标志 ──
bool gGpuPassthroughAvailable = false;

#ifdef _WIN32
// ── volk 未加载的 Win32 扩展函数 (备用加载) ──
// vkGetMemoryWin32HandlePropertiesKHR: 选内存类型用 (volk 未加载时 fallback 到 type 0)
PFN_vkGetMemoryWin32HandlePropertiesKHR g_vkGetMemoryWin32HandlePropertiesKHR = nullptr;
#endif

// ── GPU 直通延迟初始化 (必须在主线程、Godot 完全初始化后调用) ──
// 默认开启: RenderingDevice(Vulkan 后端) + volk 就绪即可用;
// 是否走直通由 SurfaceTextureBridge::gpuPassthroughEnabled (MediaPlayer.gpu_passthrough) 控制,
// 初始化失败自动回退 CPU 路径 (gGpuPassthroughAvailable = false)。
void avox_gpu_passthrough_init() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
        if (!rd) { UtilityFunctions::print("[avox_gpu] RenderingDevice 为 null"); return; }

        uint64_t vkInstance = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TOPMOST_OBJECT, RID(), 0);
        uint64_t vkDevice   = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0);

        VkResult volkRes = volkInitialize();
        if (volkRes != VK_SUCCESS) {
            UtilityFunctions::print("[avox_gpu] volkInitialize failed: ", (int)volkRes);
            return;
        }
        volkLoadInstance(reinterpret_cast<VkInstance>(vkInstance));
        volkLoadDevice(reinterpret_cast<VkDevice>(vkDevice));
        if (!vkCreateImage) {
            UtilityFunctions::print("[avox_gpu] volk 加载后 vkCreateImage 仍为 null");
            return;
        }

        // 如果 volk 编译时定义了平台宏, 这些指针应已加载;
        // 否则用 vkGetDeviceProcAddr 手动加载作为 fallback
        VkDevice device = reinterpret_cast<VkDevice>(vkDevice);

#ifdef _WIN32
        if (!vkGetMemoryWin32HandlePropertiesKHR) {
            g_vkGetMemoryWin32HandlePropertiesKHR =
                (PFN_vkGetMemoryWin32HandlePropertiesKHR)vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandlePropertiesKHR");
        }

        UtilityFunctions::print("[avox_gpu] init OK, GetMemWin32HandleProps=",
                                (int64_t)(void*)g_vkGetMemoryWin32HandlePropertiesKHR);
#else
        // Android: GPU 直通走 AHardwareBuffer 导入(surface.cpp), 无 Win32 句柄查询。
        // MVP 暂不启用: Adreno 驱动对 exportable image 的 vkBindImageMemory 直接
        // SIGSEGV (不返回 vk error), CPU 回退路径工作正常, AHB 直通后续专修
        UtilityFunctions::print("[avox_gpu] init OK (android, CPU fallback MVP)");
        return;
#endif

        gGpuPassthroughAvailable = true;
    });
}

// ── Android 深链 (manifest intent-filter 注入 magnet scheme) ──
// 引导时经 ActivityThread.mActivities 反射读当前 activity 的 intent data
// (浏览器/文件管理器点 magnet: 链接唤起, 无 cmdline 参数路径); GDScript 经
// AppLinks 取走。双平台注册 (Windows 恒空), main.gd 才能统一编译。
// 注意: 必须定义在 __ANDROID__ 引导块之外 —— Windows 也注册本类
static std::string g_android_pending_url;

class AppLinks : public RefCounted {
    GDCLASS(AppLinks, RefCounted)

 public:
    static void setPending(const std::string& url) { g_android_pending_url = url; }
    String takePendingUrl() {
        String out(g_android_pending_url.c_str());
        g_android_pending_url.clear();
        return out;
    }

 protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("take_pending_url"), &AppLinks::takePendingUrl);
    }
};

#ifdef __ANDROID__
// ── Android 引导 (SCENE 级主线程调用一次, 先于任何 avox 模块使用) ──
// 1) 模块注册: 补调 AvoxManager::init() (Godot 加载 libavox.so 不触发 JNI_OnLoad)
// 2) 动态插件: Godot 的 jniLibs 平铺带不了 plugins/ 子目录, 动态插件 so 由
//    patch_apk.py 注入到 APK lib/arm64-v8a/ (与 libavox.so 同目录, Android 会
//    释放到 nativeLibraryDir), 扫描目录经 dladdr 定位 libavox.so 后取其同级。
// 3) AndroidEnv: JavaVM 经 libart ELF dynsym 手解取 (HyperOS/Android16 的
//    linker namespace 屏蔽 dlopen("libart.so") 与 RTLD_DEFAULT);
//    application 上下文经 ActivityThread.currentApplication() 反射取。
// ── libart 符号手解: HyperOS/Android16 的 app linker namespace 屏蔽
// dlopen("libart.so") 与 RTLD_DEFAULT, dlsym 拿不到 JNI_GetCreatedJavaVMs。
// 退路: /proc/self/maps 定位运行中的 libart.so (映射基址 + 磁盘路径), 自行解析
// 其 ELF dynsym, 运行时地址 = 映射基址 + (st_value - 最小 PT_LOAD vaddr)。
static void *resolveLibArtSymbol(const char *symName) {
    // 1) maps: libart.so 最低映射地址(基址) + 可执行段范围 + 磁盘完整路径
    uintptr_t base = 0, execStart = 0, execEnd = 0;
    char artPath[256] = {};
    if (FILE *maps = fopen("/proc/self/maps", "r")) {
        char line[1024];
        while (fgets(line, sizeof(line), maps)) {
            if (strstr(line, "/libart.so") == nullptr) {
                continue;
            }
            uintptr_t start = strtoull(line, nullptr, 16);
            uintptr_t end = strtoull(strchr(line, '-') + 1, nullptr, 16);
            if (base == 0 || start < base) {
                base = start;
            }
            if (execStart == 0 && strstr(line, "r-xp") != nullptr) {
                execStart = start;
                execEnd = end;
            }
            if (artPath[0] == '\0') {
                char *p = strchr(line, '/');
                if (p != nullptr) {
                    char *e = strchr(p, '\n');
                    if (e != nullptr) *e = '\0';
                    strncpy(artPath, p, sizeof(artPath) - 1);
                }
            }
        }
        fclose(maps);
    }
    if (base == 0 || artPath[0] == '\0') {
        return nullptr;
    }
    // 2) 整读 libart.so, 解析 program headers → PT_DYNAMIC
    FILE *f = fopen(artPath, "rb");
    if (f == nullptr) {
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(fileSize > 0 ? (size_t)fileSize : 0);
    bool ok = fileSize > 1024 &&
              fread(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f);
    if (!ok || memcmp(buf.data(), ELFMAG, SELFMAG) != 0) {
        return nullptr;
    }
    const auto *eh = reinterpret_cast<const Elf64_Ehdr *>(buf.data());
    if (eh->e_phoff == 0 || eh->e_phnum == 0 ||
        eh->e_phoff + (size_t)eh->e_phnum * sizeof(Elf64_Phdr) > buf.size()) {
        return nullptr;
    }
    const auto *phdrs = reinterpret_cast<const Elf64_Phdr *>(buf.data() + eh->e_phoff);
    uintptr_t dynV = 0, dynSz = 0, minV = ~(uintptr_t)0;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dynV = phdrs[i].p_vaddr;
            dynSz = phdrs[i].p_filesz;
        }
        if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_vaddr < minV) {
            minV = phdrs[i].p_vaddr;
        }
    }
    if (dynV == 0) {
        return nullptr;
    }
    // vaddr → 文件内指针 (按 PT_LOAD 的 vaddr/offset 映射换算)
    auto v2p = [&](uintptr_t v, size_t bytes) -> const uint8_t * {
        for (int i = 0; i < eh->e_phnum; i++) {
            const auto &ph = phdrs[i];
            if (ph.p_type != PT_LOAD || v < ph.p_vaddr) {
                continue;
            }
            uintptr_t off = v - ph.p_vaddr;
            if (off <= ph.p_filesz && bytes <= ph.p_filesz - off) {
                return buf.data() + (off + ph.p_offset);
            }
        }
        return nullptr;
    };
    const auto *dyns = reinterpret_cast<const Elf64_Dyn *>(v2p(dynV, dynSz));
    if (dyns == nullptr) {
        return nullptr;
    }
    uintptr_t symV = 0, strV = 0, hashV = 0;
    for (const Elf64_Dyn *d = dyns; d < dyns + dynSz / sizeof(Elf64_Dyn); d++) {
        if (d->d_tag == DT_SYMTAB) {
            symV = d->d_un.d_ptr;
        } else if (d->d_tag == DT_STRTAB) {
            strV = d->d_un.d_ptr;
        } else if (d->d_tag == DT_HASH) {
            hashV = d->d_un.d_ptr;
        }
    }
    if (symV == 0 || strV == 0) {
        return nullptr;
    }
    // 符号数: DT_HASH 的 nchain 最准; 无则按 symtab 紧邻 strtab 的常见布局估算
    size_t symCount = 0;
    if (hashV != 0) {
        const auto *hash = reinterpret_cast<const uint32_t *>(v2p(hashV, 8));
        if (hash != nullptr) {
            symCount = hash[1];
        }
    }
    if (symCount == 0) {
        symCount = (size_t)((strV - symV) / sizeof(Elf64_Sym));
    }
    const auto *syms =
        reinterpret_cast<const Elf64_Sym *>(v2p(symV, symCount * sizeof(Elf64_Sym)));
    const char *strtab = reinterpret_cast<const char *>(v2p(strV, 1));
    if (syms == nullptr || strtab == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < symCount; i++) {
        if (syms[i].st_value == 0 || syms[i].st_name >= (size_t)fileSize) {
            continue;
        }
        if (strcmp(strtab + syms[i].st_name, symName) == 0) {
            uintptr_t addr = base + (syms[i].st_value - minV);
            // 必须落在可执行映射内, 防错解出数据符号后直接调用崩进程
            if (execStart != 0 && addr >= execStart && addr < execEnd) {
                return reinterpret_cast<void *>(addr);
            }
            return nullptr;
        }
    }
    return nullptr;
}

static void avoxAndroidBootstrap() {
    // 1) 插件扫描目录 = libavox.so 所在目录 (nativeLibraryDir)
    Dl_info avoxInfo = {};
    if (dladdr((void *)(uintptr_t)&avoxAndroidBootstrap, &avoxInfo) && avoxInfo.dli_fname) {
        std::string libDir = avoxInfo.dli_fname;
        size_t pos = libDir.find_last_of('/');
        if (pos != std::string::npos) {
            libDir = libDir.substr(0, pos);
            avox::ModuleMgr::Get().setPluginsDir(libDir.c_str());
            UtilityFunctions::print("[avox_android] plugins dir = ", libDir.c_str());
        }
    }

    // 2) 模块工厂注册: Windows 走 DllMain / AvoxJava 走 JNI_OnLoad 触发
    //    AvoxManager::init(), Godot 以 DT_NEEDED 方式加载 libavox.so 两者都不触发,
    //    不补调则 aRender 等 RegeditFactory 全空, 播放时 bad_function_call
    avox::AvoxManager::Get().init();

    // 3) AndroidEnv 接线: dlsym 拿不到 libart 符号 (linker namespace), 走 ELF 手解
    using JNI_GetCreatedVMsFn = jint (*)(JavaVM **, jsize, jsize *);
    auto getVMs = reinterpret_cast<JNI_GetCreatedVMsFn>(
        resolveLibArtSymbol("JNI_GetCreatedJavaVMs"));
    JavaVM *vm = nullptr;
    jsize count = 0;
    if (getVMs == nullptr || getVMs(&vm, 1, &count) != JNI_OK || count < 1 || vm == nullptr) {
        UtilityFunctions::print("[avox_android] JNI_GetCreatedJavaVMs resolve/call failed, AndroidEnv 未接线");
        return;
    }
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            UtilityFunctions::print("[avox_android] AttachCurrentThread failed");
            return;
        }
    }
    avox::AndroidEnv andEnv = {};
    andEnv.vm = vm;
    andEnv.env = env;
    // ActivityThread.currentApplication() → Application 上下文
    jclass at = env->FindClass("android/app/ActivityThread");
    if (at != nullptr) {
        jmethodID cur = env->GetStaticMethodID(at, "currentApplication",
                                               "()Landroid/app/Application;");
        jobject appObj = cur ? env->CallStaticObjectMethod(at, cur) : nullptr;
        if (appObj != nullptr) {
            andEnv.activity = env->NewGlobalRef(appObj);  // Godot 场景无独立 Activity 句柄, Application 兜底
            andEnv.application = andEnv.activity;
            jclass appCls = env->GetObjectClass(appObj);
            jmethodID getAssets = env->GetMethodID(appCls, "getAssets",
                                                   "()Landroid/content/res/AssetManager;");
            jobject assets = getAssets ? env->CallObjectMethod(appObj, getAssets) : nullptr;
            if (assets != nullptr) {
                andEnv.assetManager = AAssetManager_fromJava(env, assets);
                env->DeleteLocalRef(assets);
            }
            jclass ver = env->FindClass("android/os/Build$VERSION");
            if (ver != nullptr) {
                jfieldID fid = env->GetStaticFieldID(ver, "SDK_INT", "I");
                if (fid != nullptr) {
                    andEnv.sdkVersion = env->GetStaticIntField(ver, fid);
                }
                env->DeleteLocalRef(ver);
            }
            env->DeleteLocalRef(appCls);
        }
        env->DeleteLocalRef(at);
    }
    avox::AvoxManager::Get().initAndroid(andEnv);
    // JNI 方法 ID 注册 (主线程有 app classloader 上下文, FindClass 能找到
    // patch_apk 注入的 avox.android.library.*; 注册成 GlobalRef 后任意线程可用)
    jint atRet = avox::getAudiotrackFields();
    avox::getAudioRecordFields();
    avox::getSurfaceTextureFields();
    avox::getSurfaceFields();
    avox::getSurfaceTextureObFields();
    // 类缺失时 FindClass 会留 pending exception, 不清会毒化后续 JNI 调用
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    // ── 深链: 读当前 activity 的启动 intent data (magnet: 唤起场景) ──
    // activity 句柄经 ActivityThread.mActivities 反射取 (无导出静态入口);
    // 任一步失败只告警, 深链退化为不可用
    do {
        jclass atCls = env->FindClass("android/app/ActivityThread");
        if (atCls == nullptr) break;
        jmethodID cur = env->GetStaticMethodID(
            atCls, "currentActivityThread", "()Landroid/app/ActivityThread;");
        jobject at = cur ? env->CallStaticObjectMethod(atCls, cur) : nullptr;
        jfieldID fActs = (at != nullptr)
            ? env->GetFieldID(atCls, "mActivities", "Landroid/util/ArrayMap;")
            : nullptr;
        jobject acts = fActs ? env->GetObjectField(at, fActs) : nullptr;
        jmethodID valuesM = (acts != nullptr)
            ? env->GetMethodID(env->GetObjectClass(acts), "values",
                               "()Ljava/util/Collection;")
            : nullptr;
        jobject col = valuesM ? env->CallObjectMethod(acts, valuesM) : nullptr;
        jmethodID toArray = (col != nullptr)
            ? env->GetMethodID(env->GetObjectClass(col), "toArray",
                               "()[Ljava/lang/Object;")
            : nullptr;
        jobjectArray arr = toArray
            ? (jobjectArray)env->CallObjectMethod(col, toArray)
            : nullptr;
        if (arr == nullptr) break;
        jsize n = env->GetArrayLength(arr);
        for (jsize i = 0; i < n; i++) {
            jobject rec = env->GetObjectArrayElement(arr, i);
            if (rec == nullptr) continue;
            jfieldID fAct = env->GetFieldID(env->GetObjectClass(rec),
                                            "activity", "Landroid/app/Activity;");
            if (fAct == nullptr) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                continue;
            }
            jobject activity = env->GetObjectField(rec, fAct);
            if (activity == nullptr) continue;
            jmethodID gi = env->GetMethodID(env->GetObjectClass(activity),
                                            "getIntent", "()Landroid/content/Intent;");
            jobject intent = gi ? env->CallObjectMethod(activity, gi) : nullptr;
            if (intent == nullptr) continue;
            jmethodID gd = env->GetMethodID(env->GetObjectClass(intent),
                                            "getData", "()Landroid/net/Uri;");
            jobject uri = gd ? env->CallObjectMethod(intent, gd) : nullptr;
            if (uri == nullptr) continue;
            jmethodID ts = env->GetMethodID(env->GetObjectClass(uri),
                                            "toString", "()Ljava/lang/String;");
            jstring js = ts ? (jstring)env->CallObjectMethod(uri, ts) : nullptr;
            if (js == nullptr) continue;
            const char* c = env->GetStringUTFChars(js, nullptr);
            if (c != nullptr) {
                std::string url(c);
                env->ReleaseStringUTFChars(js, c);
                if (url.rfind("magnet:", 0) == 0) {
                    AppLinks::setPending(url);
                    UtilityFunctions::print("[avox_android] deep link: ", url.c_str());
                }
                break;
            }
        }
    } while (false);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    UtilityFunctions::print("[avox_android] AndroidEnv wired, sdk=", (int64_t)andEnv.sdkVersion,
                            " audiotrackFields=", (int64_t)atRet);
}
#endif // __ANDROID__

// ── avox 文件日志 sink ──
// GUI Godot 无控制台 (或 stdout 被吞), avox 内部日志会丢; 挂文件 sink 落盘。
// 路径: AVOX_GODOT_LOG_PATH (run_tools.py tee 时注入, 与 Godot stdout 合并) > <runDir>/logs/godot_<ts>.log。
class GodotFileLogOb : public avox::ILogOb {
 public:
    explicit GodotFileLogOb(const std::string& path) { ofs.open(path, std::ios::out | std::ios::app); }
    void onLogEvent(int level, const char* message) override {
        if (!ofs.is_open()) return;
        // level: 0=info 1=warn 2=error 3=debug
        const char* tag = (level == 1) ? "warn" : (level == 2) ? "error" : (level == 3) ? "debug" : "info";
        std::lock_guard<std::mutex> lock(mtx);
        ofs << "[" << lineStamp() << "] [" << tag << "] " << message << std::endl;
    }

 private:
    std::ofstream ofs;
    std::mutex mtx;
    // HH:MM:SS.mmm
    static std::string lineStamp() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        tm = *std::localtime(&t);
#endif
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
        char out[32];
        std::snprintf(out, sizeof(out), "%s.%03lld", buf, (long long)ms.count());
        return out;
    }
};

// 一次性挂载 avox 文件日志 (进程生命期常驻, 不 delete — 同 avox_cmd/cmdPlay)
// 无论何种启动方式都只落一份 godot_<ts>.log: 优先 AVOX_GODOT_LOG_PATH (run_tools.py 注入,
// avox 日志与 Godot stdout 合并进同一文件), 否则自建 <runDir>/logs/godot_<ts>.log。
static void setupAvoxFileLog() {
    std::string path;
    const char *envPath = std::getenv("AVOX_GODOT_LOG_PATH");
    if (envPath && envPath[0] != '\0') {
        path = envPath;
    } else {
        std::string dir = avox::getAvoxRunDir();
        if (dir.empty()) return;
        std::string logsDir = dir + "/logs";
        std::error_code ec;
        std::filesystem::create_directories(logsDir, ec);
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        tm = *std::localtime(&t);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
        path = logsDir + "/godot_" + stamp + ".log";
    }
    avox::setLogObserver(new GodotFileLogOb(path));
    std::string mountMsg = std::string("[godot_plugin] avox file log mounted: ") + path;
    avox::logMsg(avox::LogLevel::info, mountMsg.c_str());  // 首行自检: 落盘即 sink 生效
}

// ── 插件初始化 ──
void avoxGodotInit(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
#ifdef __ANDROID__
        avoxAndroidBootstrap();  // 插件目录解压 + AndroidEnv, 必须先于任何 avox 模块使用
#endif
        ClassDB::register_class<MediaPlayer>();
        ClassDB::register_class<SourcePlayer>();
        ClassDB::register_class<DeviceManager>();
        ClassDB::register_class<MediaRecorder>();
#ifdef _WIN32
        ClassDB::register_class<GlobalHotkey>();
        ClassDB::register_class<MicCapture>();
        ClassDB::register_class<SttNode>();
        ClassDB::register_class<TtsNode>();
#endif
        ClassDB::register_class<FaceNode>();
        ClassDB::register_class<VideoFaceNode>();
        ClassDB::register_class<BodyNode>();
#ifdef AVOX_ENABLE_AGENT
        ClassDB::register_class<AgentNode>();
#endif
#ifdef _WIN32
        ClassDB::register_class<TextInjector>();
#endif
        ClassDB::register_class<AvoxImage>();
        ClassDB::register_class<AvoxOption>();
        ClassDB::register_class<SourceProbe>();
        ClassDB::register_class<AppLinks>();
    }
}

// ── 插件卸载 ──
void avoxGodotTerminate(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
        gGpuPassthroughAvailable = false;
    }
}

// ── Godot 入口点 ──
extern "C" {
GDExtensionBool GDE_EXPORT avox_godot_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                           GDExtensionClassLibraryPtr p_library,
                                           GDExtensionInitialization *r_initialization) {
    static std::once_flag logFlag;
    std::call_once(logFlag, setupAvoxFileLog);  // avox.dll 已加载, 任何 MediaPlayer 构造前挂日志
    GDExtensionBinding::InitObject initObj(p_get_proc_address, p_library, r_initialization);
    initObj.register_initializer(avoxGodotInit);
    initObj.register_terminator(avoxGodotTerminate);
    initObj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_CORE);
    return initObj.init();
}
}
