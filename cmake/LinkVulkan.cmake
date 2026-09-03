# 所有平台改成动态加载vulkan库
# INTERFACE 接口库，不编译，只包含头文件
add_library(vulkan INTERFACE)
set(VULKAN_INCLUDE_DIR ${KHRONOS_DIR}/vulkan)
target_sources(vulkan INTERFACE ${VULKAN_INCLUDE_DIR})
target_include_directories(vulkan SYSTEM INTERFACE ${VULKAN_INCLUDE_DIR})
# vulkan头文件指明动态加载，避免重复定义VK函数
target_compile_definitions(vulkan INTERFACE VK_NO_PROTOTYPES)

# 先检查是否配置了 VULKAN_SDK 环境变量
if(APPLE AND NOT DEFINED ENV{VULKAN_SDK})
    if(IOS)
        set(PLATFORM_SUFFIX "iOS")
    else()
        set(PLATFORM_SUFFIX "macOS")
    endif()
    # 获取用户主目录
    get_filename_component(USER_HOME "$ENV{HOME}" ABSOLUTE)
    # 全局搜索 VulkanSDK 目录
    file(GLOB VULKAN_SDK_DIRS "${USER_HOME}/VulkanSDK/*/${PLATFORM_SUFFIX}")
    set(MAX_DIR "")
    message(STATUS "VULKAN_SDK_DIRS: ${VULKAN_SDK_DIRS}")
    foreach(VULKAN_DIR ${VULKAN_SDK_DIRS})
        if("${MAX_DIR}" STREQUAL "" OR "${VULKAN_DIR}" GREATER "${MAX_DIR}")
            set(MAX_DIR ${VULKAN_DIR})
        endif()
    endforeach()
    if(MAX_DIR)
        set(ENV{VULKAN_SDK} "${MAX_DIR}")
        message(STATUS "Automatically selected Vulkan SDK: ${MAX_DIR}")
    else()
        message(FATAL_ERROR "Could not find any Vulkan SDK directories in ${USER_HOME}/VulkanSDK/")
    endif()
endif()
# Apple平台专用配置macOS/iOS(主要考虑VULKAN_SAMPLES bldsys/cmake/global_options.cmake)
if(APPLE)   
    # # 修改此处，正确获取环境变量的值
    message(STATUS "VULKAN_SDK: $ENV{VULKAN_SDK}")
    # CMake has a bug in 3.28 that doesn't handle xcframeworks.  Do it by hand for now.
    if(IOS)        
        set(VULKAN_MVK_PATH $ENV{VULKAN_SDK}/lib/MoltenVK.xcframework/ios-arm64)       
    else()
        set(VULKAN_MVK_PATH $ENV{VULKAN_SDK}/lib/MoltenVK.xcframework/macos-arm64)
    endif()
    message(STATUS "VULKAN_MVK_PATH: ${VULKAN_MVK_PATH}")   
    find_library(Vulkan_MoltenVK_LIBRARY NAMES MoltenVK HINTS ${VULKAN_MVK_PATH})
    find_path(Vulkan_MoltenVK_INCLUDE_DIR NAMES MoltenVK/mvk_vulkan.h HINTS $ENV{VULKAN_SDK}/include)
    message(STATUS "Vulkan_MoltenVK_LIBRARY: ${Vulkan_MoltenVK_LIBRARY}")
    message(STATUS "Vulkan_MoltenVK_INCLUDE_DIR: ${Vulkan_MoltenVK_INCLUDE_DIR}")
    if(Vulkan_MoltenVK_LIBRARY AND Vulkan_MoltenVK_INCLUDE_DIR)
        target_link_libraries(vulkan INTERFACE ${Vulkan_MoltenVK_LIBRARY})
        target_include_directories(vulkan SYSTEM INTERFACE ${Vulkan_MoltenVK_INCLUDE_DIR})
        # 复制 MoltenVK 库到构建目录
        # /Users/zhouxin/VulkanSDK/1.4.313.0/iOS/lib/MoltenVK.xcframework
        file(COPY ${Vulkan_MoltenVK_LIBRARY} DESTINATION "${CMAKE_INSTALL_PREFIX}")
    else()
        message(FATAL_ERROR "Could not find MoltenVK")
    endif()
endif()

if(ANDROID)
    target_compile_definitions(vulkan INTERFACE VK_USE_PLATFORM_ANDROID_KHR)
elseif(WIN32)
    target_compile_definitions(vulkan INTERFACE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    target_compile_definitions(vulkan INTERFACE VK_USE_PLATFORM_METAL_EXT)
elseif(ONLY_LINUX)
    # X11
    target_compile_definitions(vulkan INTERFACE VK_USE_PLATFORM_XLIB_KHR)
endif()

# volk
set(VOLK_DIR "${AVOX_TRDPARTY}/volk")
set(VOLK_FILES "${VOLK_DIR}/volk.c" "${VOLK_DIR}/volk.h")
# 创建静态库 volk（Vulkan 加载库），使用volk.c/volk.h源码
add_library(volk STATIC ${VOLK_FILES})
# 新增输出路径设置
avox_output(volk)
# 启用位置无关代码（Position Independent Code），便于静态库被动态库引用
# set_target_properties(volk PROPERTIES POSITION_INDEPENDENT_CODE ON)
# 链接vulkan接口库（传递头文件路径和编译定义）
target_link_libraries(volk PUBLIC vulkan)
# 添加volk头文件目录（SYSTEM标识表示系统头文件，抑制编译器警告）
target_include_directories(volk SYSTEM PUBLIC ${VOLK_DIR})
# 在IDE中将volk目标归类到"3rdparty"文件夹，便于项目管理
set_property(TARGET volk PROPERTY FOLDER "3rdparty")
