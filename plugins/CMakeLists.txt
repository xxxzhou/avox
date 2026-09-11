# 统一以 src 为根目录引用头文件
# (根 CMakeLists.txt 已 include_directories(src plugins), 此处保留兼容)
include_directories("../src/")

# 插件(组件)清单 —— 把模块名挪到 STATIC / DYNAMIC 即可切换编译模式
# STATIC : 源码编进主 avox(老路, 由 src/CMakeLists.txt add_sub_path 处理)
# DYNAMIC: 独立 avox_<name>.dll/.so 输出到 ${CMAKE_INSTALL_PREFIX}/plugins/
set(PLUGINS_STATIC  "")
# avox_python (嵌入 CPython) 已移除: 被 avox.dll 核心的 SubprocessRunner (spawn 机器 python) 取代。
set(PLUGINS_DYNAMIC avox_opencv avox_sherpa avox_onnx avox_translation avox_cv avox_ocr avox_webrtc avox_openvino avox_avatar avox_torrent avox_calib avox_fbx avox_remote)
if(ANDROID)
  # Android: jniLibs 平铺 + 目录不可枚举, 动态插件无法被 avox 扫描加载(见 plugins/README.md
  # 静态化规划); Godot 打包场景仅保留 avox_torrent —— so 作为 APK 资源打进包,
  # 运行期由 avox_godot 解压到 filesDir 并 setPluginsDir 动态加载。
  # 其余插件在 Android 无预编译依赖(opencv 等)或链接不完整(sherpa 等), 一并排除。
  set(PLUGINS_DYNAMIC avox_torrent)
endif()
if(WIN32)
  # DeckLink(Blackmagic)采集卡, 仅Windows
  list(APPEND PLUGINS_DYNAMIC avox_decklink)
endif()
if(APPLE)
  # avox_webrtc 在 macOS 静态编入 libavox(src/CMakeLists.txt), 不建动态插件
  list(REMOVE_ITEM PLUGINS_DYNAMIC avox_webrtc)
endif()

# 动态插件: 各自子目录调用 register_plugin(<name> DYNAMIC ...)
foreach(_plugin ${PLUGINS_DYNAMIC})
  add_subdirectory(${_plugin})
endforeach()

# 静态插件: 试点无(源码仍在 src/ 下由 add_sub_path 编进 avox)
# 后续 zlmediakit/webrtc 等可登记到此, 并在 src/CMakeLists.txt 改走 register_plugin
