include(${SWIG_USE_FILE})
include(AVOXHelper)

# 设置 SWIG CMake policies
cmake_policy(SET CMP0078 NEW)  # UseSWIG generates standard target names
cmake_policy(SET CMP0086 NEW)  # UseSWIG honors SWIG_MODULE_NAME via -module
cmake_policy(SET CMP0122 NEW)  # UseSWIG use standard library name conventions for csharp

include_directories("../src")
set(WRAPPERLIST "${CMAKE_CURRENT_SOURCE_DIR}/common.i")

get_directory_property(definitions COMPILE_DEFINITIONS)
# 遍历定义并添加 -D 前缀（SWIG 需要显式 -D 标志）
foreach(definition ${definitions}) 
    list(APPEND SWIG_DEFINITIONS "-D${definition}")  
endforeach() 
message(STATUS "swig definitons:" ${SWIG_DEFINITIONS})     

# windows/android/ios都生成C++/C#绑定给avalonia使用
if(AVOX_ENABLE_SWIG_CSHARP)
    message(STATUS "SWIG C# enabled, generating C# bindings...")
    set(CMAKE_SWIG_OUTDIR "${CMAKE_CURRENT_SOURCE_DIR}/csharp/files")
    set(CMAKE_SWIG_FLAGS -O ${SWIG_DEFINITIONS} -namespace AvoxNet)
    if(APPLE)
        # 静态库,需要从AvoxWrapper改成__Internal
        list(APPEND CMAKE_SWIG_FLAGS "-dllimport" "__Internal")
    else()
        # Windows 和 Android 统一使用 AvoxWrapper (与 common.i 中的模块名一致)
        # Windows 会找 AvoxWrapper.dll
        # Android 会自动找 libAvoxWrapper.so
        list(APPEND CMAKE_SWIG_FLAGS "-dllimport" "AvoxWrapper")
    endif()
    set_source_files_properties(${WRAPPERLIST} PROPERTIES CPLUSPLUS ON)
    # list(APPEND CMAKE_SWIG_FLAGS "-includeall")
    if(APPLE)
        swig_add_library(AvoxWrapper LANGUAGE csharp TYPE STATIC SOURCES ${WRAPPERLIST})
    else()
        swig_add_library(AvoxWrapper LANGUAGE csharp SOURCES ${WRAPPERLIST})
    endif()
    target_link_libraries(AvoxWrapper PRIVATE avox)
    if(APPLE)
        # 针对 iOS 强制指定静态库属性，防止其被识别为 dylib/bundle
        set_target_properties(AvoxWrapper PROPERTIES
            POSITION_INDEPENDENT_CODE ON PREFIX "lib" SUFFIX ".a"
            MACH_O_TYPE staticlib)
    endif()
    if(ANDROID)
        # 前缀为lib
        set_target_properties(AvoxWrapper PROPERTIES PREFIX "lib")
    endif()
    avox_output(AvoxWrapper)     
    # avalonia的AvoxCommon直接引用产生的cs文件链接
    # 这个工程只给winfrom使用
    if(EXISTS "${CMAKE_SWIG_OUTDIR}" AND WIN32)        
        # add_subdirectory(csharp) 
    endif()
endif()

if(AVOX_ENABLE_SWIG_JAVA AND ANDROID)
    message(STATUS "SWIG Java enabled, generating Java bindings...")
    # 添加宏定义
    list(APPEND SWIG_DEFINITIONS -DAVOX_ANDROID)
    include_directories(${ANDROID_NDK}/sources/android/native_app_glue)
    # 设置Java包名及对应目录结构
    set(AVOX_JAVA_PACKAGE "avox.android.library.swig")
    string(REPLACE "." "/" AVOX_JAVA_SUBDIR ${AVOX_JAVA_PACKAGE})
    # 设置SWIG生成Java文件的临时输出目录
    set(AVOX_SWIG_TEMP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/java/files)
    set(CMAKE_SWIG_OUTDIR ${AVOX_SWIG_TEMP_DIR}/${AVOX_JAVA_SUBDIR})
    message(STATUS "SWIG Java output directory: ${CMAKE_SWIG_OUTDIR}")
    # 清理旧的输出目录并创建新目录
    execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SWIG_OUTDIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SWIG_OUTDIR})
    # 设置SWIG参数和属性
    set_source_files_properties(${WRAPPERLIST} PROPERTIES CPLUSPLUS ON)
    set(CMAKE_SWIG_FLAGS -c++ -package ${AVOX_JAVA_PACKAGE} -O ${SWIG_DEFINITIONS})
    # 生成SWIG包装库
    swig_add_library(avox_java LANGUAGE java SOURCES ${WRAPPERLIST})
    swig_link_libraries(avox_java avox)
    # 设置Java编译输出目录和jar包路径
    set(JAVA_CLASSES_DIR ${CMAKE_BINARY_DIR}/java_classes)
    set(JAR_FILE ${CMAKE_CURRENT_SOURCE_DIR}/java/avox_swig.jar)
    # 确保Java类输出目录存在
    execute_process(COMMAND ${CMAKE_COMMAND} -E remove_directory ${JAVA_CLASSES_DIR})
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${JAVA_CLASSES_DIR})
    # 构建阶段：生成Java文件列表并编译成class文件（改用CMake脚本生成文件列表）
    if(WIN32)
        set(JAVAC_CMD cmd /c "javac -d ${JAVA_CLASSES_DIR} @${CMAKE_SWIG_OUTDIR}/sources.txt")
    else()
        set(JAVAC_CMD javac -d ${JAVA_CLASSES_DIR} @${CMAKE_SWIG_OUTDIR}/sources.txt)
    endif()
    add_custom_command(TARGET avox_java POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Generating sources.txt for Java compilation"
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/generate_sources.cmake
        COMMAND ${JAVAC_CMD}
        WORKING_DIRECTORY ${CMAKE_SWIG_OUTDIR}
        COMMENT "Compiling SWIG generated Java sources"
        VERBATIM)
    # 生成辅助CMake脚本generate_sources.cmake，内容示例：
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generate_sources.cmake
    "file(GLOB_RECURSE JAVA_FILES RELATIVE \"${CMAKE_SWIG_OUTDIR}\" \"*.java\")\n"
    "file(WRITE \"${CMAKE_SWIG_OUTDIR}/sources.txt\" \"\")\n"
    "foreach(file IN LISTS JAVA_FILES)\n"
    "  file(APPEND \"${CMAKE_SWIG_OUTDIR}/sources.txt\" \"${CMAKE_SWIG_OUTDIR}/\${file}\\n\")\n"
    "endforeach()\n")
    # 构建阶段：打包class文件成jar包
    add_custom_command(TARGET avox_java POST_BUILD
        COMMAND jar cf ${JAR_FILE} -C ${JAVA_CLASSES_DIR} .
        COMMENT "Packaging SWIG generated Java classes into jar"    )
    # 构建阶段：复制jar包到指定目录
    add_custom_command(TARGET avox_java POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying SWIG jar to AvoxJava/avox/libs"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/../platform/android/AvoxJava/avox/libs
        COMMAND ${CMAKE_COMMAND} -E copy ${JAR_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/../platform/android/AvoxJava/avox/libs/
        COMMENT "Ensure libs directory exists and copy SWIG jar")
    # 构建阶段：复制生成的JNI库到安装目录
    add_custom_command(TARGET avox_java POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying Java native library..."
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:avox_java> ${CMAKE_INSTALL_PREFIX}
        COMMENT "Copying Java JNI library to runtime directory"
    )
endif()
 
if(AVOX_ENABLE_SWIG_NODEJS AND WIN32)
    # nodejs需要包含当前目录下的nativeOb    
    # include_directories(${CMAKE_CURRENT_SOURCE_DIR}/nodejs/nativeOb)
    message(STATUS "SWIG Node.js enabled, generating Node.js bindings...")  
    # 添加宏定义 
    list(APPEND SWIG_DEFINITIONS -DAVOX_NODEJS)       
    message(STATUS "SWIG Node.js SWIG_DEFINITIONS: ${SWIG_DEFINITIONS}")
    # 设置Node.js模块输出目录
    set(NODEJS_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nodejs/files") 
    set(CMAKE_SWIG_OUTDIR ${NODEJS_OUTPUT_DIR})        
    # 配置SWIG参数 
    set_source_files_properties(${WRAPPERLIST} PROPERTIES USE_TARGET_INCLUDE_DIRECTORIES ON PLUSPLUS ON)      
    # 清空之前的CMAKE_SWIG_FLAGS
    set(CMAKE_SWIG_FLAGS -O ${SWIG_DEFINITIONS} -napi)  
    # 生成SWIG包装库
    # swig_add_library(avox_nodejs LANGUAGE javascript SOURCES ${WRAPPERLIST})   
    # 生成包装代码
    add_custom_command( 
        OUTPUT ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx
        COMMAND ${SWIG_EXECUTABLE} -javascript -napi -c++ ${SWIG_DEFINITIONS} -I"${CMAKE_SOURCE_DIR}/src" -I"${CMAKE_SOURCE_DIR}/plugins" -o ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx ${WRAPPERLIST}
        DEPENDS ${WRAPPERLIST}
        COMMENT "Generating SWIG wrapper for Node.js")
    # 调用command
    add_custom_target(avox_nodejs_swig ALL DEPENDS ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.cxx ${NODEJS_OUTPUT_DIR}/commonJAVASCRIPT_wrap.h)
endif()

if(AVOX_ENABLE_SWIG_PYTHON AND WIN32)
    find_package(Python3 COMPONENTS Interpreter Development QUIET)
    if(NOT Python3_FOUND)
        message(STATUS "SWIG Python: Python3 (Development) 未找到, 跳过")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT Python3_LIBRARY_DEBUG)
        message(STATUS "SWIG Python: Debug 模式未找到 python3xx_d.lib, 跳过")
    else()
        message(STATUS "SWIG Python enabled, generating Python bindings...")
        set(PYTHON_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/python/files")
        set(PYTHON_WRAP_CXX "${PYTHON_OUTPUT_DIR}/commonPYTHON_wrap.cxx")
        # 手动跑 swig (不用 swig_add_library): 多个 swig_add_library 共享同一 common.i 在 CMake 4.0
        # UseSWIG (SWIG_ADD_SOURCE_TO_MODULE) 下冲突, target 创建失败。同 nodejs 分支做法。
        add_custom_command(
            OUTPUT ${PYTHON_WRAP_CXX}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${PYTHON_OUTPUT_DIR}
            COMMAND ${SWIG_EXECUTABLE} -python -c++ -py3 -threads -module AvoxWrapper ${SWIG_DEFINITIONS}
                    -I"${CMAKE_SOURCE_DIR}/src" -I"${CMAKE_SOURCE_DIR}/plugins"
                    -outdir ${PYTHON_OUTPUT_DIR} -o ${PYTHON_WRAP_CXX} ${WRAPPERLIST}
            DEPENDS ${WRAPPERLIST}
            COMMENT "Generating SWIG wrapper for Python")
        # _AvoxWrapper.pyd: python 扩展命名 (_<module>.pyd), 模块名由 -module AvoxWrapper 决定
        add_library(avox_swig_python MODULE ${PYTHON_WRAP_CXX})
        target_include_directories(avox_swig_python PRIVATE ${Python3_INCLUDE_DIRS} "${CMAKE_SOURCE_DIR}/src")
        if(AVOX_PYTHON_STABLE_ABI)
            # 稳定 ABI (abi3): Py_LIMITED_API 须先于 #include <Python.h> (命令行 -D 满足);
            # 链 python3.lib (稳定 ABI 导入库 → python3.dll), 不再链版本锁死的 python314.lib。
            # 一份 _AvoxWrapper.pyd 被 3.11+ 任意 Python 加载 (Windows 经 .pyd 后缀识别)。
            target_compile_definitions(avox_swig_python PRIVATE Py_LIMITED_API=0x030B0000)  # floor 3.11
            target_link_libraries(avox_swig_python PRIVATE avox "${Python3_LIBRARY_DIRS}/python3.lib")
        else()
            target_link_libraries(avox_swig_python PRIVATE avox ${Python3_LIBRARIES})  # 旧: python314.lib (版本锁死)
        endif()
        # 抑制 pyconfig.h 的 #pragma comment(lib,"python3xx_d.lib"): Debug 下 _DEBUG 优先于 Py_LIMITED_API,
        # 会强链标准 Python 安装不发、且 CMake 链接行里没有的 python314_d.lib 致 LNK1104。
        # CMake 已显式链 python3.lib, 按头文件建议关掉自动链接 (Release 下同样无害)。
        target_compile_definitions(avox_swig_python PRIVATE Py_NO_LINK_LIB)
        set_target_properties(avox_swig_python PROPERTIES PREFIX "" OUTPUT_NAME "_AvoxWrapper" SUFFIX ".pyd")
        # 输出到 plugins/; avox_module.cmake 提供 avox_plugin_output
        include(avox_module)
        avox_plugin_output(avox_swig_python)
        # AvoxWrapper.py (swig 胶水完整版) → plugins/ (与 _AvoxWrapper.pyd 同目录, 供内置模式 import)
        add_custom_command(TARGET avox_swig_python POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${PYTHON_OUTPUT_DIR}/AvoxWrapper.py"
                    "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/plugins")
        # AvoxWrapper 转发壳 → install root (供外部 python 从 install root import AvoxWrapper;
        # 转发壳把 plugins/ 加 sys.path 并 from AvoxWrapper import * 找完整版)
        add_custom_command(TARGET avox_swig_python POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                    "${CMAKE_CURRENT_SOURCE_DIR}/python/AvoxWrapper_forward.py"
                    "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/AvoxWrapper.py")
        # avox/ 高层封装包 → plugins/avox/ (按模块封装, 见 avox/__init__.py _SUBMODULES)
        add_custom_command(TARGET avox_swig_python POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/python/avox"
                    "${CMAKE_INSTALL_PREFIX}/${CMAKE_BUILD_TYPE}/plugins/avox")
    endif()
endif()