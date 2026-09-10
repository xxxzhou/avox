
# 统一以src为根目录引用头文件
include_directories("../src/")

# 单个文件测试
function(generatetest target)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/${target}.cpp)
    # Apple 窗口样例要 ObjC++ (NSWindow/CAMetalLayer), 同名 .mm 优先
    if(APPLE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${target}.mm)
        set(source ${CMAKE_CURRENT_SOURCE_DIR}/${target}.mm)
    endif()
    message(STATUS "generate test: ${source}")
    add_executable(${target} ${source})
    # generate传入后续参数全为相应需要链接的库
    # Apple 是静态 libavox.a: 模块注册靠归档成员里的文件级静态对象(AVOX_REGISTER_MODULE),
    # 无外部符号引用会被 ld 整个成员丢弃 (WebrtcModule.o 被丢 → createWebRtcPlayer 返回 null 崩),
    # 故整档 force_load; 不能手写 -Wl,-force_load 再叠一次 avox —— 新版 ld 去重同路径归档会
    # 丢掉后出现的那次 (ignoring duplicate libraries), force_load 静默失效
    if(APPLE)
        target_link_libraries(${target} "$<LINK_LIBRARY:WHOLE_ARCHIVE,avox>" ${ARGN})
    else()
        target_link_libraries(${target} avox ${ARGN})
    endif()
    avox_output(${target})
endfunction() 

function(generateWtest target)
    if(WIN32)
        set(source ${CMAKE_CURRENT_SOURCE_DIR}/${target}.cpp)
        message(STATUS "generate test: ${source}")
        add_executable(${target} WIN32 ${source})
        # generate传入后续参数全为相应需要链接的库
        target_link_libraries(${target} avox ${ARGN})
        avox_output(${target})
    endif()
endfunction()

# macOS 跳过整套 functest (一堆 Windows 专属依赖: agent/cli/录制等), 只出跨平台帧契约运行时验证
if(NOT APPLE)
    add_subdirectory(functest)
else()
    add_executable(yuvouttest functest/yuvouttest.cpp)
    target_link_libraries(yuvouttest "$<LINK_LIBRARY:WHOLE_ARCHIVE,avox>")
    avox_output(yuvouttest)
endif()
add_subdirectory(vulkantest)


