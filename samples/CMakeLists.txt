
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
        avox_bundle_resources(${target})
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

# Apple 样例 .app 打包 avox.bundle(glsl spv/字体/图像): Vulkan(MoltenVK) 管线运行时从
# 主 bundle 资源定位 shader, 缺了则 shader module 为 null, vkCreateComputePipelines 段错误
function(avox_bundle_resources target)
    add_dependencies(${target} AvoxBundle)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/avox.bundle"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_INSTALL_PREFIX}/avox.bundle" "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources/avox.bundle"
    )
endfunction()

# Windows 之外跳过整套 functest (一堆 Windows 专属依赖: agent/cli/录制等),
# macOS 只出跨平台帧契约运行时验证 (yuvouttest), Linux 走 vulkantest/linuxtest + playtest
if(WIN32)
    add_subdirectory(functest)
elseif(APPLE)
    add_executable(yuvouttest functest/yuvouttest.cpp)
    target_link_libraries(yuvouttest "$<LINK_LIBRARY:WHOLE_ARCHIVE,avox>")
    # 静态核宿主导出 core 符号, 供 plugins/ 下动态插件 dlopen 回绑(AvoxManager 单例唯一)
    target_link_options(yuvouttest PRIVATE -Wl,-export_dynamic)
    avox_output(yuvouttest)
    avox_bundle_resources(yuvouttest)
    # 跨平台插件功能探针: ass(libass)/smb(libsmb2) 苹果预编译产物就位后,
    # mac 同样可跑真实渲染/源注册冒烟(无 WIN32 头, ios 需真机宿主不在此出)
    foreach(_probe asstest smbsourcetest)
        add_executable(${_probe} functest/${_probe}.cpp)
        target_link_libraries(${_probe} "$<LINK_LIBRARY:WHOLE_ARCHIVE,avox>")
        target_link_options(${_probe} PRIVATE -Wl,-export_dynamic)
        avox_output(${_probe})
        avox_bundle_resources(${_probe})
    endforeach()
endif()
add_subdirectory(vulkantest)


