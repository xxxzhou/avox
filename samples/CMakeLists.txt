
# 统一以src为根目录引用头文件
include_directories("../src/")

# 单个文件测试
function(generatetest target)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/${target}.cpp)
    message(STATUS "generate test: ${source}")
    add_executable(${target} ${source})
    # generate传入后续参数全为相应需要链接的库
    target_link_libraries(${target} avox ${ARGN})
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

add_subdirectory(functest)
add_subdirectory(vulkantest)


