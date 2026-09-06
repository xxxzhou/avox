# 模块之间目录的对位相对于code,导出到外部相对于统一位置
include_directories("./")

# 源码下所有模块添加导出符号
add_definitions(-DAVOX_EXPORT_DEFINE)

# windows平台max/min 宏函数冲突
add_definitions(-DNOMINMAX=1)

# 用于存储所有头文件和源文件的变量
set(AVOX_HEADER "")
set(AVOX_SOURCE "")

# avox下文件 使用add_sub_path在对应工程文件里也能保持文件目录结构
add_sub_path(avox AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/audio AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/avatar AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/module AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/vision AVOX_HEADER AVOX_SOURCE)
if(APPLE)
  set_source_files_properties(avox/video/Window.cpp PROPERTIES LANGUAGE OBJCXX)
endif()
add_sub_path(avox/player AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/subtitle AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/video AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/codec AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/layer AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/muxer AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/source AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox/Input AVOX_HEADER AVOX_SOURCE)
# TestSdpOb 已移入 avox_zlmediakit/(依赖 mk_httpclient, 随 ZLMediaKit 守卫编译)
# windows系统
if(WIN32)
    add_sub_path(avox_windows AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_windows/dx11 AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_windows/dx12 AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_windows/winrt AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_windows/audio AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_windows/camera AVOX_HEADER AVOX_SOURCE)
endif() 

# android系统
if(ANDROID)
    add_sub_path(avox_egl AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_android AVOX_HEADER AVOX_SOURCE) 
endif()

# ios系统
if(APPLE)
    add_sub_path(avox_apple AVOX_HEADER AVOX_SOURCE)
endif()

# linux系统 UNIX LINUX
if(ONLY_LINUX)
    add_sub_path(avox_linux AVOX_HEADER AVOX_SOURCE)
endif()

# vulkan可用
if(AVOX_ENABLE_VULKAN)
    add_sub_path(avox_vulkan AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/layer AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/share AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/vulkan AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/extra AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/geometry AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_vulkan/quality AVOX_HEADER AVOX_SOURCE)
    # 根据平台设置如下几个CPP文件 的编译语言
    if(APPLE)
        add_sub_path(avox_vulkan/ios AVOX_HEADER AVOX_SOURCE)
        set_source_files_properties(avox_vulkan/layer/VkInputLayer.cpp PROPERTIES LANGUAGE OBJCXX)
        set_source_files_properties(avox_vulkan/layer/VkOutputLayer.cpp PROPERTIES LANGUAGE OBJCXX)
        set_source_files_properties(avox_vulkan/vulkan/VkVideoRender.cpp PROPERTIES LANGUAGE OBJCXX)
        set_source_files_properties(avox_vulkan/ios/VkIosImage.mm PROPERTIES LANGUAGE OBJCXX)
    endif()
    if(WIN32)
        add_sub_path(avox_vulkan/windows AVOX_HEADER AVOX_SOURCE)
    endif()
    if(ANDROID)
        add_sub_path(avox_vulkan/android AVOX_HEADER AVOX_SOURCE)
    endif()
    if(AVOX_ENABLE_VULKAN_DECODE)
        add_sub_path(avox_vulkan/decode AVOX_HEADER AVOX_SOURCE)
    endif()
endif()

# ffmpeg可用
if(AVOX_ENABLE_FFMPEG)
    add_sub_path(avox_ffmpeg AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_ffmpeg/decoder AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_ffmpeg/encoder AVOX_HEADER AVOX_SOURCE)
endif()

# zlmediakit可用
if(AVOX_ENABLE_ZLMEDIAKIT)
    message(STATUS "zlmediakit 可用")
    add_sub_path(avox_zlmediakit AVOX_HEADER AVOX_SOURCE)
endif()

# faad2/faac/fdk-aac可用
if(AVOX_ENABLE_FAAD2 OR AVOX_ENABLE_FAAC OR AVOX_ENABLE_FDKAAC)
    if(NOT AVOX_ENABLE_FAAD2)
        message(STATUS "faad2 不可用")
    endif()
    if(NOT AVOX_ENABLE_FAAC)
        message(STATUS "faac 不可用")
    endif()
    if(NOT AVOX_ENABLE_FDKAAC)
        message(STATUS "fdk-aac 不可用")
    endif()
    add_sub_path(avox_aac AVOX_HEADER AVOX_SOURCE)
endif()

# freetype可用
if(AVOX_ENABLE_FREETYPE)
    message(STATUS "freetype 可用")
    add_sub_path(avox_freetype AVOX_HEADER AVOX_SOURCE)
endif()

# Agent 可用（VLM 客户端，多媒体 AI 工具层）
if(AVOX_ENABLE_AGENT)
    message(STATUS "avox_agent Agent client 可用")
    # 作为预处理宏暴露给源码, 供 avox_agent 模块 / avox_agent / functest 等按 #ifdef gate 编译
    add_definitions(-DAVOX_ENABLE_AGENT)
    add_sub_path(avox_agent AVOX_HEADER AVOX_SOURCE)
    # core: 会话事件日志 + surface 投影 + 持久化 (与业务无关的通用骨架)
    add_sub_path(avox_agent/core AVOX_HEADER AVOX_SOURCE)
    # policy: 注册到扩展点的策略 (超时/循环卫生/溢出/审批/压缩/模型路由)
    add_sub_path(avox_agent/policy AVOX_HEADER AVOX_SOURCE)
    # compose: 显式装配 (取代插件加载器) + 部署配置
    #（AgentHost / AgentConfig / DiagnosticAgent: 装配的唯一定义处, shell 与导出层共用）
    add_sub_path(avox_agent/compose AVOX_HEADER AVOX_SOURCE)
    # team: Agent Teams 持久运行时 (journal/fold/roster/mailbox/task-board/service)
    #（Lead 会话隐式成队; compose 单向依赖本目录, 本目录只依赖 core）
    add_sub_path(avox_agent/team AVOX_HEADER AVOX_SOURCE)
    # adapter: 把现有 provider/transport/agent.json 接进新架构
    #（源文件 glob 在配置期展开: 增删文件后需要重新配置一次）
    add_sub_path(avox_agent/adapter AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_agent/skills AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_agent/tools AVOX_HEADER AVOX_SOURCE)
    # plugins: 各插件一个子目录 (对齐 dsh 的 packages/<group>/<name> 布局)
    add_sub_path(avox_agent/plugins/token-meter AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_agent/plugins/vision-toolkit AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_agent/plugins/agent-instructions AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_agent/transport AVOX_HEADER AVOX_SOURCE)
    add_sub_path(avox_agent/provider AVOX_HEADER AVOX_SOURCE)
endif()

# avox_cv 已迁移为 plugins/avox_cv 动态插件(dependsOn avox_onnx + avox_opencv, 见 plugins/CMakeLists.txt)
# 核心纯接口 IWatermarkRemoval 在 src/avox/AvoxVision.h, ITranslator 在 AvoxBase.h (随 avox.dll 编译)

# avox_sherpa 已迁移为 plugins/avox_sherpa 动态插件(见 plugins/CMakeLists.txt)
# if(AVOX_ENABLE_SHERPA)
#     message(STATUS "sherpa-onnx streaming speech recognition 可用")
#     add_sub_path(avox_sherpa AVOX_HEADER AVOX_SOURCE)
# endif()

# avox_translation 已迁移为 plugins/avox_translation 动态插件(见 plugins/CMakeLists.txt)
# SubtitleAsr 走 AvoxManager::translatorHub.create 查表, 核心不 include avox_translation
# message(STATUS "Translation 可用")
# add_sub_path(avox_translation AVOX_HEADER AVOX_SOURCE)

# avox_cmd 
add_sub_path(avox_cmd AVOX_HEADER AVOX_SOURCE)
add_sub_path(avox_cmd/commands AVOX_HEADER AVOX_SOURCE)
if(AVOX_ENABLE_CLI)
    add_subdirectory(avox_cmd/cli)
endif()
if(AVOX_ENABLE_AGENT AND AVOX_ENABLE_CLI)
    add_subdirectory(avox_agent/cli)
endif()

# 获取所有的宏编译
get_directory_property(definitions COMPILE_DEFINITIONS)
message(STATUS "All definitions: ${definitions}")

include_directories(${CMAKE_CURRENT_BINARY_DIR})
message(STATUS "AVOX_DLL_TYPE: ${AVOX_DLL_TYPE}")

# Windows: 把版本资源编进 avox.dll (右键属性 -> 详细信息 可见版本号/commit)
if(WIN32)
  list(APPEND AVOX_SOURCE avox/module/AvoxVersion.rc)
endif()

add_library(avox ${AVOX_DLL_TYPE} ${AVOX_SOURCE} ${AVOX_HEADER})
target_compile_options(avox PRIVATE ${COMPILE_OPTIONS_DEFAULT})
message(STATUS "AVOX_LINK_LIBRARIES: ${AVOX_LINK_LIBRARIES}")

target_link_libraries(avox ${AVOX_LINK_LIBRARIES})

# Agent 用 WebRTC 的 BoringSSL 时: 把 BoringSSL 头目录和 OPENSSL_IS_BORINGSSL 宏
# 只作用到 AgentClient.cpp (httplib 唯一引用 openssl/ 的编译单元), 不污染其它源文件。
# BoringSSL 符号由 webrtc 库静态提供, 无需额外链接。
if(AVOX_AGENT_USE_BORINGSSL)
  # httplib 引用 openssl/ 的编译单元: AgentClient.cpp(框架, available 探活 + SSE Post 上层)
  #   + HttplibTransport.cpp(SSE Post)
  foreach(f avox_agent/AgentClient.cpp avox_agent/transport/HttplibTransport.cpp)
    set_source_files_properties(${f} PROPERTIES
      INCLUDE_DIRECTORIES "${WEBRTC_BORINGSSL_INCLUDE_DIR}"
      COMPILE_DEFINITIONS "OPENSSL_IS_BORINGSSL")
  endforeach()
  message(STATUS "Agent httplib units -> BoringSSL (include=${WEBRTC_BORINGSSL_INCLUDE_DIR})")
endif()

# Android: 隐藏静态库内部符号，防止跨 .so 的符号解析冲突
# 例如 sentencepiece 和 sherpa-onnx 都包含 protobuf，版本不同会导致崩溃
if(ANDROID)
    target_link_options(avox PRIVATE "LINKER:--exclude-libs,ALL")
endif()

avox_output(avox)

# 查找glsl/target目录下的所有.spv 文件并复制到各平台封装目录
# 文件夹创建可能需要相应权限，以管理员身份运行CMake或者手动创建目录
set(SPV_FILES "${CMAKE_SOURCE_DIR}/glsl/target/")
set(IMAGE_FILES "${CMAKE_SOURCE_DIR}/assets/images/program/")
set(FONT_FILES "${CMAKE_SOURCE_DIR}/assets/fonts/")
set(CONFIG_FILES "${CMAKE_SOURCE_DIR}/assets/config/")
set(AGENT_SKILLS_FILES "${CMAKE_SOURCE_DIR}/assets/agent/")
set(SCRIPT_FILES "${CMAKE_SOURCE_DIR}/assets/script/")

# 模型文件 - 按平台分离以减少包体积
set(COMMON_MODELS "${CMAKE_SOURCE_DIR}/assets/models/stt/")
set(TTS_MODELS "${CMAKE_SOURCE_DIR}/assets/models/tts/")
set(TRANSLATION_MODELS "${CMAKE_SOURCE_DIR}/assets/models/translation/")
set(QUALITY_MODELS "${CMAKE_SOURCE_DIR}/assets/models/quality/")
set(WIN_ONLY_MODELS "${CMAKE_SOURCE_DIR}/assets/models/inpaint/")
set(AVATAR_MODELS "${CMAKE_SOURCE_DIR}/assets/models/avatar/")

if(APPLE)
    message(STATUS "Copy SPV files to IOS bundle")
    # 创建 bundle 目标
    add_custom_target(AvoxBundle ALL)
    # 创建 bundle 目录结构
    set(BUNDLE_DIR "${CMAKE_INSTALL_PREFIX}/avox.bundle")
    # 使用封装函数复制GLSL资源到bundle
    avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "glsl" "${SPV_FILES}")
    # 复制图像资源
    avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "images" "${IMAGE_FILES}")
    # 复制字体资源
    avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "fonts" "${FONT_FILES}")
    # 复制配置文件
    avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "config" "${CONFIG_FILES}")
    avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "agent" "${AGENT_SKILLS_FILES}")
    avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "script" "${SCRIPT_FILES}")
    # 复制模型资源 (stt + translation)
    if(AVOX_ENABLE_SHERPA)
        avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "models/stt" "${COMMON_MODELS}")
        avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "models/tts" "${TTS_MODELS}")
    endif()
    if(AVOX_ENABLE_TRANSLATION)
        avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "models/translation" "${TRANSLATION_MODELS}")
    endif()
    if(AVOX_ENABLE_ONNX)
        avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "models/quality" "${QUALITY_MODELS}")
    endif()
    if(AVOX_ENABLE_AVATAR)
        avox_copy_ios_assets("AvoxBundle" "${BUNDLE_DIR}" "models/avatar" "${AVATAR_MODELS}")
    endif()
    # 确保在构建可执行文件前先构建bundle
    add_dependencies(avox AvoxBundle)
else()
    # Windows/Linux: 复制所有模型 (包括 inpaint)
    message(STATUS "Copy assets files to build directory") 
    avox_copy_assets("glsl" "${SPV_FILES}")
    avox_copy_assets("images" "${IMAGE_FILES}")
    avox_copy_assets("fonts" "${FONT_FILES}")
    avox_copy_assets("config" "${CONFIG_FILES}")
    avox_copy_assets("agent" "${AGENT_SKILLS_FILES}")
    avox_copy_assets("script" "${SCRIPT_FILES}")
    if(AVOX_ENABLE_SHERPA)
        avox_copy_assets("models/stt" "${COMMON_MODELS}")
        avox_copy_assets("models/tts" "${TTS_MODELS}")
    endif()
    if(AVOX_ENABLE_TRANSLATION)
        avox_copy_assets("models/translation" "${TRANSLATION_MODELS}")
    endif()
    if(AVOX_ENABLE_ONNX)
        avox_copy_assets("models/quality" "${QUALITY_MODELS}")
    endif()
    if(AVOX_ENABLE_AVATAR)
        avox_copy_assets("models/avatar" "${AVATAR_MODELS}")
    endif()
    # inpaint 模型由 plugins/avox_cv 独立管理, 此处不再复制
endif()
