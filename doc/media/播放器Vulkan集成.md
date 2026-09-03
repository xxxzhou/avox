# 播放器多平台Vulkan集成

Vulkan在当前项目做为Windows/Android/IOS的通用渲染层，承接所有平台软解后的渲染，有完善的滤境处理图[Vulkan滤镜处理管线](https://zhuanlan.zhihu.com/p/388055520)，其windows平台硬解的dx11纹理可以直接通过GPU对接到vulkan中，而Android/IOS平台的硬解数据除了对接opengles/metal的渲染外，还对接硬解数据map下来后到vulkan渲染管线中，在这修正下，已完成[Android硬解对接Vulkan](../android/Android硬解Vulkan.md)/[IOS硬解对接Vulkan](../ios/IOS硬解Vulkan.md)，意思所有平台的硬解都能高效直接到vulkan中。可以看到Vulkan在当前项目使用非常广泛。并且vulkan已经有个各平台的统一硬解方案，这样后续可以得到一个多平台统一vulkan硬解+渲染的高性方案。

当前项目先实现的windows平台，引入vulkan的方案就是在cmake使用find_library引入vulkan的库，然后在代码中使用vulkan的函数，后面在android和ios平台接入vulkan，都是动态加载库的方式，所以为了统一所有平台vulkan的函数调用，都使用了动态加载库的方式。

## Volk库引入 

这个项目代码不多，就是一个多平台动态加载vulkan的封装。

方便所有平台统一引用相应源码，使用gitsubmodule方式引入，

``` git
[submodule "3rdparty/volk"]
	path = 3rdparty/volk
	url = git@github.com:zeux/volk.git
```

使用Cmake引入。

``` cmake
# 所有平台改成动态加载vulkan库
# INTERFACE 接口库，不编译，只包含头文件
add_library(vulkan INTERFACE)
set(VULKAN_INCLUDE_DIR ${KHRONOS_DIR}/vulkan)
target_sources(vulkan INTERFACE ${VULKAN_INCLUDE_DIR})
target_include_directories(vulkan SYSTEM INTERFACE ${VULKAN_INCLUDE_DIR})
# vulkan头文件指明动态加载，避免重复定义VK函数
target_compile_definitions(vulkan INTERFACE VK_NO_PROTOTYPES)

if(ANDROID)
    target_compile_definitions(vulkan INTERFACE VK_USE_PLATFORM_ANDROID_KHR)
elseif(WIN32)
    target_compile_definitions(vulkan INTERFACE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    target_compile_definitions(vulkan INTERFACE VK_USE_PLATFORM_METAL_EXT)
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
```

在这基本windows/android平台就可以加载到volk库，动态调用vulkan的函数，而IOS平台有些特殊，IOS平台由MoltenVK把metal的函数封装到vulkan的函数中，所以IOS平台使用vulkan,需要先加载MoltenVK库。

``` CMake
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
```

动态加载Vulkan,需要在引用其头文件前，指定VK_NO_PROTOTYPES为1，这样避免重复定义VK函数。

``` C++
// 动态加载<vulkan/vulkan.h>里的函数
#define VK_NO_PROTOTYPES 1
#include <volk.h>

#ifdef AVOX_ENABLE_VULKAN_DECODE
#include <vk_video/vulkan_video_codec_h264std_decode.h>
#include <vk_video/vulkan_video_codec_h265std_decode.h>
#endif
```

## 加载着色器字节码

因为滤境很多，我这边有一百多个相应SPV文件，如何管理这些字节码，先看下各平台加载SPV的代码实现。

```C++
void VkShader::loadShaderModule( std::string path,
                                    VkShaderStageFlagBits shaderFlag) {
  release();
#ifdef __ANDROID__
  AAssetManager* assetManager = AvoxManager::Get().getAppEnv().assetManager;
  assert(assetManager != nullptr);
  shaderModule = loadShader(assetManager, path.c_str(), vkDevice);
#elif __APPLE__
  // 获取应用的主 bundle
  const char *iospath = getShaderPath(path.c_str());
  if (iospath) {
    shaderModule = loadShader(iospath, vkDevice);
  }else{
    log(LogLevel::warn, "file: " + path + " load shader failed");
  }  
#else
  std::string fullPath = getAvoxPath() + "/" + path;
  shaderModule = loadShader(fullPath.c_str(), vkDevice);
#endif
  LOGASSERT(shaderModule != VK_NULL_HANDLE,
            "file: " + path + " load shader failed");
  shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStage.stage = shaderFlag;
  shaderStage.pName = "main";
  shaderStage.module = shaderModule;
}
// IOS
const char *getShaderPath(const char *spvPath) {
  // 获取 avox.bundle 的路径
  NSString *bundleName = @"avox.bundle";
  NSString *avoxBundlePath = [[NSBundle mainBundle] pathForResource:bundleName
                                                            ofType:nil];
  if (!avoxBundlePath) {
    return nullptr;
  }
  // 创建 avox.bundle 的 NSBundle 实例
  NSBundle *avoxBundle = [NSBundle bundleWithPath:avoxBundlePath];
  if (!avoxBundle) {
    return nullptr;
  }
  // 从 avox.bundle 中查找资源
  NSString *resourceName = [NSString stringWithUTF8String:spvPath];
  NSString *bundlePath = [avoxBundle pathForResource:resourceName ofType:nil];
  if (bundlePath) {
    const char *fullPath = [bundlePath UTF8String];
    return fullPath;
  } else {
    return nullptr;
  }
}
```

### IOS平台

IOS平台会把CMake编译项目时，把所有SPV打包到avox.bundle目录。

``` cmake
# 查找glsl/target目录下的所有.spv 文件并复制到各平台封装目录
# 文件夹创建可能需要相应权限，以管理员身份运行CMake或者手动创建目录
file(GLOB_RECURSE SPV_FILES "${CMAKE_SOURCE_DIR}/glsl/target/*.spv")
if(APPLE)
    message(STATUS "Copy SPV files to IOS bundle")
    # 创建 bundle 目标
    add_custom_target(AvoxBundle ALL)
    # 创建 bundle 目录结构
    set(BUNDLE_DIR "${CMAKE_INSTALL_PREFIX}/avox.bundle")
    set(RESOURCES_DIR "${BUNDLE_DIR}/glsl")
    # 创建资源目录
    add_custom_command(TARGET AvoxBundle PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RESOURCES_DIR}")
    # 复制 .spv 文件到 bundle 的资源目录
    foreach(SPV_FILE ${SPV_FILES})
        get_filename_component(FILENAME ${SPV_FILE} NAME)
        add_custom_command(TARGET AvoxBundle POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${SPV_FILE}" "${RESOURCES_DIR}/${FILENAME}" ${SPV_FILE})
    endforeach()  
    # 确保在构建可执行文件前先构建bundle
    add_dependencies(avox AvoxBundle)
#endif    
```

### Android平台

android把相应的spv文件复制到封装库里的assets目录下就行，一样在cmake编译项目下进行。

``` cmake
if(ANDROID)
    message(STATUS "Copy SPV files to Android assets")
    # 设置 Android assets 目录
    set(ANDROID_ASSETS_DIR "${CMAKE_SOURCE_DIR}/platform/android/AvoxJava/avox/assets/glsl")
    # 创建 Android assets 下的 glsl 目录，添加错误处理
    add_custom_command(TARGET avox PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${ANDROID_ASSETS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Created directory: ${ANDROID_ASSETS_DIR}"
        COMMAND_EXPAND_LISTS VERBATIM)
    # 批量复制 .spv 文件到 Android assets 下的 glsl 目录
    add_custom_command(TARGET avox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Copying all SPV files to ${ANDROID_ASSETS_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy ${SPV_FILES} ${ANDROID_ASSETS_DIR}
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Copied all SPV files to ${ANDROID_ASSETS_DIR}"
        COMMAND_EXPAND_LISTS VERBATIM)
endif()        
```

### windows平台

windows平台就更简单了，复制到运行目录下即可，后面看看改进打成一个包。

``` cmake
if(WIN32) 
    # 复制glsl文件到bin目录
    # 注意：这里已经用 SPV_FILES 查找过文件，无需重新查找
    message(STATUS "Copy SPV files to Windows build directory")
    # 设置 Windows 目标目录
    set(WINDOWS_DEST_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/glsl")
    # 创建 Windows 目标目录，添加错误处理
    add_custom_command(TARGET avox PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${WINDOWS_DEST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Created directory: ${WINDOWS_DEST_DIR}"
        COMMAND_EXPAND_LISTS VERBATIM)
    # 批量复制 .spv 文件到 Windows 目标目录
    add_custom_command(TARGET avox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Copying all SPV files to ${WINDOWS_DEST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy ${SPV_FILES} ${WINDOWS_DEST_DIR}
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --green "Copied all SPV files to ${WINDOWS_DEST_DIR}"
        COMMAND_EXPAND_LISTS VERBATIM)
endif()
```


