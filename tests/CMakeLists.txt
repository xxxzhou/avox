# avox 单元测试 (doctest, 单头文件 vendored 于 3rdparty/doctest)
# 原则: 只测无重依赖的纯逻辑, 被测 .cpp 直接编进测试目标 (AVOX_EXPORT_DEFINE 自包含),
#      不链接 avox.dll —— 保持测试秒级构建、任何机器可跑。

set(AVOX_TESTS_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(AVOX_SRC ${CMAKE_SOURCE_DIR}/src/avox)

add_executable(avox_tests
  ${AVOX_TESTS_SRC}/test_main.cpp
  ${AVOX_TESTS_SRC}/test_math.cpp
  ${AVOX_TESTS_SRC}/test_h26x.cpp
  ${AVOX_TESTS_SRC}/log_sink.cpp
  # 被测源码 (自包含编译)
  ${AVOX_SRC}/module/AvoxMath.cpp
  ${AVOX_SRC}/codec/H26XHelper.cpp
  ${AVOX_SRC}/codec/H264Parse.cpp
  ${AVOX_SRC}/codec/H265Parse.cpp
  # 被测代码的依赖 TU (PacketBuf::form / checkAvccPacket / H26XNalReader)
  ${AVOX_SRC}/source/PacketBuf.cpp
  ${AVOX_SRC}/codec/H26XNalReader.cpp
)

target_include_directories(avox_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${AVOX_SRC}
  ${CMAKE_SOURCE_DIR}/3rdparty/doctest
)

# AVOX_EXPORT_DEFINE: 被编入测试目标的源码按 dllexport 导出, 目标自包含
target_compile_definitions(avox_tests PRIVATE AVOX_EXPORT_DEFINE)

if(MSVC)
  # 源码含 UTF-8 中文注释, 与主工程保持一致
  target_compile_options(avox_tests PRIVATE /utf-8)
endif()

add_test(NAME avox_tests COMMAND avox_tests)
