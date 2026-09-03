
# 把.cs文件生成C#dll
project(AvoxSharp VERSION 0.1.0 LANGUAGES CSharp)

# include(CSharpUtilities)
# include(AVOXHelper) 

# # wrapper文件夹里文件有改动,需要刷新当前CMakeLists.txt
file(GLOB SHARP_FILES "${CMAKE_CURRENT_SOURCE_DIR}/files/*.cs")       
# message(STATUS "SHARP_FILES: " ${SHARP_FILES})  
add_library(AvoxSharp SHARED ${SHARP_FILES})                           
set_property(TARGET AvoxSharp PROPERTY DOTNET_TARGET_FRAMEWORK "net8.0") 
# set_property(TARGET AvoxSharp PROPERTY DOTNET_TARGET_FRAMEWORK_VERSION "net8.0")   
# 明确设置64位平台，AnyCPU x64
set_property(TARGET AvoxSharp PROPERTY DOTNET_PLATFORM "AnyCPU")
set_property(TARGET AvoxSharp PROPERTY DOTNET_SDK "Microsoft.NET.Sdk")
set_property(TARGET AvoxSharp PROPERTY DOTNET_PROJECT_FORMAT "sdk")
# 设置现代项目属性
set_property(TARGET AvoxSharp PROPERTY DOTNET_IMPLICIT_USINGS "enable")
set_property(TARGET AvoxSharp PROPERTY DOTNET_NULLABLE "enable")
# 移除ZERO_CHECK依赖,避免使用net build时会找ZERO_CHECK而失败
# 但是注意,没有这个依赖,改变CMakeLists.txt后,不会自动更新AvoxSharp.csproj
set_property(TARGET AvoxSharp PROPERTY VS_GLOBAL_PROJECT_REGENERATION_ENABLED "false")
set_property(TARGET AvoxSharp PROPERTY VS_NO_SOLUTIONSOURCES TRUE)
# debug/release,开启C#本地代码调试
# debug/release配置
if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")             
    set_property(TARGET AvoxSharp PROPERTY VS_GLOBAL_EnableUnmanagedDebugging "true")
    set_property(TARGET AvoxSharp PROPERTY DefineConstants "TRACE;DEBUG")
    set_property(TARGET AvoxSharp PROPERTY Optimize "false")
else()
    set_property(TARGET AvoxSharp PROPERTY Optimize "true")
endif()     
# 设置目标平台版本
if(WIN32)
    set_property(TARGET AvoxSharp PROPERTY TargetPlatformVersion "10.0")
    set_property(TARGET AvoxSharp PROPERTY TargetPlatformMinVersion "10.0")
endif()
avox_output(AvoxSharp)  

# if(EXISTS "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/avox_csharp.dll")
#     file(COPY "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/avox_csharp.dll" 
#          DESTINATION "${AVOX_PLATFORM_PATH}/CSharp/${CMAKE_BUILD_TYPE}") 
# else()
#     message(WARNING "源文件不存在: ${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/avox_csharp.dll") 
# endif()

# if(EXISTS "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/AvoxSharp.dll")
#     file(COPY "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/AvoxSharp.dll" 
#          DESTINATION "${AVOX_PLATFORM_PATH}/CSharp/${CMAKE_BUILD_TYPE}")
# else()
#     message(WARNING "源文件不存在: ${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/AvoxSharp.dll")
# endif()