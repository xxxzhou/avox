# 新机器从零打包 Godot Android 播放器 (含 torrent)

目标: 在一台**没有任何 Android 环境**的新机器上, 把 Godot 播放器 (含 avox_torrent 磁力/边下边播)
编译出 Android APK 并装进手机。全程 Windows 交叉编译, 真机验证环境: 小米 14 (HyperOS/Android 16)。

整体链路 (一键脚本 `platform/godot/build_android_godot.sh` 已封装 3~5 步):

```
libtorrent/openssl 等预编译 (库仓)
        │
        ▼
python build_android.py            # libavox.so + libavox_godot.so + plugins/libavox_torrent.so
        │
        ▼
deploy_godot_android.py            # 产物拷进 tools/addons/avox_godot/{bin_android,plugins}
        │
        ▼
Godot 导出 APK                     # export_presets.cfg 已入库, 命令行无头导出
        │
        ▼
patch_apk.py                       # 注入伴随库 + torrent 插件 + zipalign + 签名
        │
        ▼
adb install -r avox_tools_debug.apk
```

## 一. 新机器前置 (一次性)

| 依赖 | 用途 | 获取方式 |
|------|------|----------|
| Android Studio | SDK/NDK/JDK 一站式 | 官网安装即可; 自带 JBR (JDK 21) 位于 `<Studio>/jbr` |
| Android SDK 组件 | 编译/签名 | Studio 的 SDK Manager: NDK **26.1.10909125**、SDK Platform **android-35**、Build-Tools **35+**、Platform-Tools |
| Godot 4.7 编辑器 | 导出 APK | 本仓库用的 `D:\Work\godot\godot.exe` (4.7.1.stable) |
| Godot **Android 导出模板** | 导出必需 | 见下"导出模板" |
| avc_library | 预编译大库 | `git clone xxxzhou/avc_library` 到 avox 同级 (openssl/onnxruntime/libtorrent android 产物都在) |
| libtorrent 源项目 | 重编 libtorrent android 预编译 | 见 `plugins/avox_torrent/REBUILD.md` (只需在新机器跑一次它的 android 部分) |
| boost 1.86 头文件 | libtorrent 编译 | `../boost`(布局 `boost/boost/version.hpp`), 同 REBUILD.md |

### 导出模板 (Godot 侧, 最容易漏)

编辑器里: 编辑器 → 管理导出模板 → 下载。命令行党可直下官方 tpz 后手动提取 **android 部分**:

```bash
# tpz 约 1.2GB, 断点续传 (GitHub 直连被掐时加 -C - 重跑即可)
curl -L -C - -o godot_templates.tpz \
  https://github.com/godotengine/godot/releases/download/4.7.1-stable/Godot_v4.7.1-stable_export_templates.tpz
python - << 'EOF'
import zipfile, os
dst = os.path.join(os.environ['APPDATA'], 'Godot', 'export_templates', '4.7.1.stable')
z = zipfile.ZipFile(r'<tpz 路径>')
for n in [x for x in z.namelist() if 'android' in x]:
    open(os.path.join(dst, os.path.basename(n)), 'wb').write(z.read(n))
    print('OK', os.path.basename(n))
# 期望: android_debug.apk / android_release.apk / android_source.zip
EOF
```

## 二. 编译 (build_android.py)

前置就位后:

```bash
# ANDROID_NDK 未设环境变量时, build_common 会自动找 Studio 的 NDK
python build_android.py
```

产物 (`build/android/avox/install/aarch64/`):

| 文件 | 说明 |
|------|------|
| `libavox.so` | avox 核心 (软解/渲染/IO/Agent 全功能编入) |
| `Release/libavox_godot.so` | GDExtension 主库 (Godot 类注册 + Android 引导) |
| `Release/plugins/libavox_torrent.so` | 磁力插件 (libtorrent+OpenSSL 静态封装) |
| `libc++_shared.so` / `libmk_api.so` / `libfdk-aac.so` | 运行依赖 |

当前 Android 构建参数 (build_android.py): `AGENT=ON`(核心功能)、`GODOT=ON`、`CLI=OFF`、
`WEBRTC=OFF`(预编译库需 Linux 编)、`SWIG=OFF`(AvoxWrapper 引用 Agent 符号, 需要时 `AVOX_ENABLE_SWIG=ON`)。
opencv/sherpa 等无 Android 预编译的插件自动降级跳过。

## 三. 部署 + 导出 + 打补丁 (build_android_godot.sh)

脚本在 `platform/godot/` 下, 仓库内任意 cwd 可跑; godot/JDK/NDK 路径有默认值,
不同机器用环境变量覆盖: `GODOT_BIN=/path/godot.exe`、`JAVA_HOME=...`、`ANDROID_NDK=...`。
**不要并发跑多条链** (两份 patch 同时写同一个 APK 会互相覆盖)。

```bash
bash platform/godot/build_android_godot.sh
```

五步 = 编译 → 部署到 `tools/addons/avox_godot/{bin_android,plugins}` + 编译 avox Java
助手类 (javac+d8 → `build/android/avox_java/classes.dex`, 音频路径依赖) → Godot 无头导出
APK → `patch_apk.py` 注入 (伴随库/torrent 插件 → `lib/arm64-v8a/`, AvoxAudioTrack dex →
`classesN.dex`, Vulkan shader → `assets/glsl/`(源 `glsl/target/`), magnet deep-link
intent-filter → manifest) → adb 安装。手分步执行:

```bash
python platform/godot/plugin/deploy_godot_android.py
# Godot 导出 (JDK 指 Studio 的 jbr; 输出用绝对路径, --path 后相对路径会按工程目录解析)
JAVA_HOME="D:/Program Files/Android/Android Studio/jbr" \
  godot --headless --path platform/godot/tools --export-debug "Android" \
  "D:/绝对路径/avox_tools_debug.apk"
# 补丁: 注入伴随库 + dex + shader + deep-link, zipalign + debug 签名
python platform/godot/plugin/patch_apk.py "D:/绝对路径/avox_tools_debug.apk"
adb install -r "D:/绝对路径/avox_tools_debug.apk"
```

`tools/export_presets.cfg` 已入库 (Android 预设: arm64-v8a、非 gradle、 INTERNET 权限),
新机器无需手工配置; `project.godot` 里 ETC2/ASTC、canvas_items 拉伸、横屏等设置也都已就位。

## 四. 坑表 (全部真实踩过)

| 现象 | 原因/解法 |
|------|-----------|
| gdextension 的 android 键不生效, APK 缺 libavox_godot.so | **Godot ConfigFile 不支持 `#` 注释** —— .gdextension 里的注释行会被并进键名。.gdextension 文件内禁止写注释 |
| APK 无任何权限 (app 无网, torrent/webseed 全 0 下载) | `permissions/Internet=true` 开关在 4.7 实测**不进 manifest**; 必须写 `permissions/custom_permissions=PackedStringArray("android.permission.INTERNET")` 显式列出。aapt dump permissions 验证 |
| 键写 `android.debug.arm64` 匹配不到 | 用**两段式** `android.arm64` (debug/release 通用) |
| APK 里没有任何 .so (lib/ 只有 Godot 自己的) | Godot 导出器**不打包未在 gdextension [libraries] 声明的 .so**; 伴随库与动态插件必须 patch_apk.py 注入 |
| libavox_godot.so 能加载但磁力插件不出现 | Android 的 jniLibs 平铺, `plugins/` 子目录不存在 → avox_godot 启动引导会把 `nativeLibraryDir` 设为插件扫描目录 (libavox_torrent.so 由 patch 注入到 lib/ 与 libavox.so 同目录) |
| 导出报需要 ETC2/ASTC | 工程设置 `rendering/textures/vram_compression/import_etc2_astc=true` (tools 工程已配) |
| 导出报目标文件夹不存在 | `--path` 之后 Godot 会切目录, 导出输出路径必须**绝对路径** |
| MIUI/HyperOS 装不上 (INSTALL_FAILED_USER_RESTRICTED "Invalid apk") | 开发者选项 → **USB 安装** 开关被吊销: 需登录小米账号重开 (仅"盯屏点确认"不够时); 增量安装误报时可 `adb install --no-incremental` 试一次 |
| 启动即崩 (SIGSEGV in AvoxManager::detachThread) | AndroidEnv 未接线时 vm 为空, 线程退出路径无条件解引用。已加空保护; 若回退版本需注意 |
| 播放创建时 SIGABRT (bad_function_call in getDefaultAudioOutput) | Godot 以 DT_NEEDED 加载 libavox.so, **JNI_OnLoad/DllMain 都不触发**, AvoxManager::init() 没人调 → 工厂全空。已在 godot_init 引导补调 `AvoxManager::Get().init()` |
| JavaVM 拿不到 (dlopen("libart.so")/RTLD_DEFAULT 均被拒) | HyperOS/Android16 linker namespace 屏蔽。已在引导里 **ELF dynsym 手解**: /proc/self/maps 定位基址+磁盘路径, 自解析 libart .dynsym (resolveLibArtSymbol) |
| 音频起不来 (logcat "not find audio track class") | AndAudioRender 依赖 `avox.android.library.AvoxAudioTrack` Java 类, Godot APK 没有 → deploy 编 dex + patch 注入 `classesN.dex` (API 21+ 自动加载) |
| dex 注入后 App 启动崩 (ClassNotFound) | Godot APK 自带 classes2..N.dex, **固定名会顶掉 Godot 自己的类**; 必须取现有最大 N+1 (patch_apk 已做幂等: 已含 AvoxAudioTrack 则跳过) |
| 视频起播即 SIGSEGV (AAsset_getLength) | VkVideoRender 初始化经 AAssetManager 读 `glsl/*.spv`, **APK 没打这些资产**; release 下 assert 被编译掉直接空指针。patch_apk 注入 assets/glsl/ (源 glsl/target/ 已在库) + loadShader 已加空保护 |
| 起播渲染崩在 Adreno 驱动 (vkBindImageMemory SEGV) | 根因在 **avox 核心导出侧**: VkSharedImage::createExportable 对 AHB 可导出内存未用专用分配 (dedicated) + 裸 vkBindImageMemory, Adreno 直接 SIGSEGV。已改 dedicated + vkBindImageMemory2 (2026-09-07 真机过)。注意 Adreno 对未启用扩展的设备, vkGetDeviceProcAddr(AHB 扩展函数) 返回 NULL, 实例级获取后查询也回全零 → **Godot 建 VkDevice 未启用 AHB 扩展, 引擎层零拷贝仍不可用**, godot_init 探测后自动 CPU 回退 (导入失败 3 次运行时降级兜底) |
| http/本地文件 mp4 卡死 opening (无任何请求) | tools 工程 io_plan=auto 时本地无 scheme 路径被分给 ZLMediaKit, ZL 报 "not supported play schema" 后挂死不回退; 且 MediaPlayer::play() 会 destroy+create 重建播放器, play 前 set_io_plan 被丢掉。已修: 插件缓存 ioPlan 在 createPlayer 重放; 本地文件请显式 ffmpeg IO |
| 开流后 "no audio track" (logcat: unsupported audio codec:86017) | `ffACodec` 映射表没有 mp3/ac3 → 整个音频被关。已补 ACodecId mp3=7/ac3=8 + ffACodec/getFFCodecId 双向映射 (BBB 的音轨就是 mp3+ac3) |
| 强杀 (force-stop) 播放中的种子后重开卡死 | piece 0 可能失效, 重取极慢 → first piece timeout。清缓存重下: `adb shell "run-as com.avox.godottools rm -rf cache/avox_torrent/<infohash>"` |
| patch 步骤跑了两遍, 注入重复/丢失 | **别并发跑多条 build_android_godot.sh** (两份 patch 同时写同一 APK); patch 本身已幂等 (dex 按 AvoxAudioTrack 判重) |
| RtspPusher.cpp 菱形继承编译错 (clang) | ZLMediaKit 子模块已打消歧补丁 (static_cast<PusherBase&>), 见 git log |
| sentencepiece 链接缺 `__android_log_write` | SPM 加 `-DCMAKE_EXE_LINKER_FLAGS=-llog` (build_android.py 已加; 注意同名 -D 会被全局 16KB 对齐参数覆盖, build_common 已改为合并两值) |
| webrtc 链接缺符号 | Windows 交叉构建关 (`-DAVOX_ENABLE_WEBRTC=OFF`); android webrtc 预编译需 Linux 产 |
| CMake4 报 "Could NOT find Boost (missing: Boost_INCLUDE_DIR)" (编 libtorrent 预编译时) | CMake4 的 FindBoost 只认 config 模式, 纯头文件 boost 没有 BoostConfig.cmake; 且 -DBOOST_ROOT 被 CMP0144 旧策略忽略。解法: 用 SDK cmake 4.0.2 带 `-G Ninja -DBoost_INCLUDE_DIR=<boost根>` 手动预配置两个 ABI 的 build 目录, 再跑 `python build_android.py` (见 CMakeCache 即跳过 configure) |
| ninja.exe "--version failed: no such file or directory" (配置主工程时) | build_common 原 AVOX_NINJA_PATH 硬编他机用户目录; 已改为 `AVOX_NINJA_PATH` 环境变量优先, 否则依次探测旧默认/`%LOCALAPPDATA%\Android\Sdk` |
| godot_init.cpp 编译错 "use of undeclared identifier 'AppLinks'" | 类定义在首个使用点 (avoxAndroidBootstrap) 之后, 已把 AppLinks 块整体前移 |
| Godot 导出报 "编辑器设置中需要有效的 Java SDK 路径" (设了 JAVA_HOME 仍报) | 编辑器设置 `export/android/java_sdk_path` (editor_settings-4.7.tres) 也要写 JDK 路径 |
| 真机装包报 INSTALL_PARSE_FAILED_MANIFEST_MALFORMED "IntentFilter$MalformedMimeTypeException: magnet" | axml_patch.py 里 android:scheme 的资源 id 写错过: scheme=**0x01010027**, 0x01010026 是 mimeType, 写错后注入的 `<data>` 被解析成 mimeType="magnet"。**aapt dump xmltree 看不出来** (按字符串名打印), 只有真机装包才暴露; 已修 |
| MIUI 报 INSTALL_FAILED_USER_RESTRICTED "Invalid apk" 且 USB 安装开关明明是开的 | 先排除包本身问题: 用干净未 patch 的导出包含机装一次对照。真机错误码与 MIUI 拦截码不同 (MANIFEST_MALFORMED vs USER_RESTRICTED), 对照实验能快速定位是包坏还是权限 |
| Windows 高 DPI (4K+150%) 窗口显小 | 见 [Windows高DPI窗口适配.md](Windows高DPI窗口适配.md): _ready 里按 `DisplayServer.screen_get_scale()` 同步放大窗口尺寸与 content_scale_factor |
| patch_apk 最后 apksigner 报 FileNotFoundException: debug.keystore | 新机器 `~/.android/debug.keystore` 不存在 (Godot 导出用的是它自己那份)。生成: `keytool -genkeypair -keystore ~/.android/debug.keystore -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"` |
| 不装 Android Studio 的裸机 | sdkmanager 自装: 下载 commandlinetools 解到 `<根>/cmdline-tools/latest`, `sdkmanager --sdk_root=<根> platform-tools platforms;android-35 build-tools;35.0.0 ndk;26.1.10909125 cmake;4.0.2`(cmake 包含 ninja, 正好命中 build_common 的 ninja 路径); 再建目录 junction `%LOCALAPPDATA%\Android\Sdk → <根>` 使全部硬编路径生效。JDK 可用 Temurin 21 zip 解压即用 |

## 五. 真机测试磁力

1. 手机开 USB 调试, `adb devices` 确认; `adb install -r <apk>` (MIUI 弹确认要点允许)
2. 启动即进播放器 (tools 工程 main_scene 已设为播放器场景);
   UI 缩放: canvas_items 拉伸 + 移动端按 `dpi/160` 内容缩放 (上限 2.0)
3. 菜单 **文件 → 打开磁力/BT…** → 贴入磁力 (推荐 Blender 官方 BBB:
   `magnet:?xt=urn:btih:dd8255ecdc7ca55fb0bbf81323d87062db1f6d1c&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337&ws=https%3A%2F%2Fwebtorrent.io%2Ftorrents%2F`)
   → 解析文件列表 → 选视频 → 播放。
   **最近磁力下拉**: 探测成功的源自动入历史 (显示种子名, 无名时中段省略显示 url),
   选中即解析; 直播源历史同样做了长源截断
4. **深链**: 手机浏览器/文件管理器点 magnet 链接应唤起播放器直接进解析
   (manifest intent-filter 由 patch_apk 自动注入; 若系统弹"选择应用"选播放器即可)。
   命令行模拟: `adb shell "am start -a android.intent.action.VIEW -d 'magnet:?xt=...' -p com.avox.godottools"`
   Windows 侧: `python platform/godot/register_magnet_protocol.py` 注册后浏览器点击直接拉起
   (**已双端真实验证**: Android 深链冷启动自动进解析 + BBB 元数据出列表; Windows 注册表协议
   拉起新实例同样直接进解析出文件列表。注意磁力弹窗/文件列表弹窗已支持按住标题拖动,
   手机上软键盘遮挡时可拖开; Windows 高 DPI 窗口适配见坑表)
5. 日志: `adb logcat -s godot:*`; 关键行: `[avox_android] plugins dir / AndroidEnv wired, sdk=`、
   `discovered plugin: avox_torrent`、`[torrent] metadata cache hit / select file / status peers:.. ws:1`
6. **加速技巧**: 探测要拉的 metadata.torrent 可从桌面缓存直推手机秒回:
   `adb push <桌面>/avox_torrent/<ih>/metadata.torrent //data/local/tmp/` &&
   `adb shell "run-as com.avox.godottools sh -c 'mkdir -p cache/avox_torrent/<ih>; cat /data/local/tmp/meta.torrent > cache/avox_torrent/<ih>/metadata.torrent'"`

## 六. 已知限制 (MVP)

- **硬解**: MediaCodec 走 NDK AMediaCodec (不依赖 Java 类), 但 `main.gd` 的
  force_soft_decode 还开着 —— AndroidEnv 接线已修好 (libart ELF 手解), 待真机验证后移除
- **音频**: 管线已通 (AvoxAudioTrack 经 classes dex 注入; 音频工厂经引导 init() 注册);
  mp3/ac3 解码映射已补。mp3/ac3 出声的最终验证在真机上还没跑完 (安装被 MIUI 拦), 明日先验
- **渲染**: Android 走 CPU 回退 (NV12 回读 → R8 纹理 → SubViewport shader 转 RGB)。GPU 直通代码已就位
  (avox 导出 AHardwareBuffer + Godot 侧 AHB 导入), 但 Adreno 驱动对 exportable image
  的 vkBindImageMemory 直接 SIGSEGV, 已在 godot_init 里禁用, 待专修
- **深链**: Windows 协议已注册可测; Android 的 manifest 注入已就位并通过 aapt 校验,
  真机装包验证待做 (卡 MIUI USB 安装)
- 首次磁力探测依赖 DHT/tracker 网络, 冷门种子可能吃满 45s 超时; 成功一次后元数据落盘,
  二次打开秒回 (也可按上面技巧直推 metadata)
- torrent 状态行 `head:` 位图只显示前 16 片; ws:1 表示 webseed 已挂载 (BT 不通的
  网络下主要靠它, peers 计数不含 webseed 连接)
