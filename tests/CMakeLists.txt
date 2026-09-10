# avox 单元测试 (doctest, 单头文件 vendored 于 3rdparty/doctest)
#
# 两个目标, 分界线是「被测代码能不能自包含编译」:
#   avox_tests       纯逻辑: 被测 .cpp 直接编进目标 (AVOX_EXPORT_DEFINE 自包含), 不链接 avox,
#                    秒级构建、任何机器可跑。
#   avox_agent_tests avox_agent core/team: 这些是 DLL 内部设施, 未从 avox.dll 导出, 必须同编;
#                    但其中 image/JSON 等落到 avox.dll 里的符号必须链 avox 才能解析
#                    (core 大量接口用 std::string / vector, avox 用静态 CRT /MT,
#                     跨 DLL 边界传它们会跨堆析构 —— 所以「同编 core + 链 avox」缺一不可)。

set(AVOX_TESTS_SRC ${CMAKE_CURRENT_SOURCE_DIR})
set(AVOX_SRC ${CMAKE_SOURCE_DIR}/src/avox)
set(AVOX_AGENT ${CMAKE_SOURCE_DIR}/src/avox_agent)

# ---------------------------------------------------------------- 纯逻辑单测
add_executable(avox_tests
  ${AVOX_TESTS_SRC}/test_main.cpp
  ${AVOX_TESTS_SRC}/test_math.cpp
  ${AVOX_TESTS_SRC}/test_h26x.cpp
  ${AVOX_TESTS_SRC}/test_videobuffer.cpp
  ${AVOX_TESTS_SRC}/test_json.cpp
  ${AVOX_TESTS_SRC}/test_ringbuffer.cpp
  ${AVOX_TESTS_SRC}/test_observer.cpp
  ${AVOX_TESTS_SRC}/test_time.cpp
  ${AVOX_TESTS_SRC}/test_threadsync.cpp
  ${AVOX_TESTS_SRC}/test_pipenode.cpp
  ${AVOX_TESTS_SRC}/log_sink.cpp
  # 被测源码 (自包含编译)
  ${AVOX_SRC}/module/AvoxMath.cpp
  ${AVOX_SRC}/codec/H26XHelper.cpp
  ${AVOX_SRC}/codec/H264Parse.cpp
  ${AVOX_SRC}/codec/H265Parse.cpp
  # 被测代码的依赖 TU (PacketBuf::form / checkAvccPacket / H26XNalReader)
  ${AVOX_SRC}/source/PacketBuf.cpp
  ${AVOX_SRC}/codec/H26XNalReader.cpp
  # 布局契约被测 TU (form/to/image2SplitYUVFrame/copyPlaneYUV2TightlyBuffer)
  ${AVOX_SRC}/video/Video.cpp
  ${AVOX_SRC}/video/VideoBuffer.cpp
  ${AVOX_SRC}/video/ImageBuffer.cpp
  ${AVOX_SRC}/module/Json.cpp   # parserJson
  ${AVOX_SRC}/module/Time.cpp   # timeStampMS / localTimeStampMS / timeTick
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

if(APPLE)
  # VideoBuffer.cpp 的 Apple 分支用 CoreVideo (CVBufferRelease), 直编进测试需链其 framework
  target_link_libraries(avox_tests PRIVATE "-framework CoreVideo")
endif()

add_test(NAME avox_tests COMMAND avox_tests)

# ------------------------------------------------- avox_agent core/team 契约单测
add_executable(avox_agent_tests
  ${AVOX_TESTS_SRC}/test_main.cpp
  ${AVOX_TESTS_SRC}/test_session.cpp
  ${AVOX_TESTS_SRC}/test_team.cpp
  ${AVOX_TESTS_SRC}/log_sink.cpp
  # core/team 被测 TU: 均为 DLL 内部设施, 未导出, 同编
  ${AVOX_AGENT}/core/SessionTypes.cpp
  ${AVOX_AGENT}/core/Surface.cpp
  ${AVOX_AGENT}/core/Session.cpp
  ${AVOX_AGENT}/core/SessionCodec.cpp
  ${AVOX_AGENT}/core/SessionPersistence.cpp
  ${AVOX_AGENT}/core/DshLayout.cpp
  ${AVOX_AGENT}/core/Llm.cpp
  ${AVOX_AGENT}/core/Dispatch.cpp        # reportDispatchError (Notify<> 错误上报)
  ${AVOX_AGENT}/core/Inbox.cpp           # ReactLoopAgent 的收件箱
  ${AVOX_AGENT}/core/SystemPrompt.cpp
  ${AVOX_AGENT}/core/ToolRuntime.cpp
  ${AVOX_AGENT}/core/ReactLoopAgent.cpp  # teamtest 的 RecoveryRig/ spawn 要真起一个 agent
  ${AVOX_AGENT}/adapter/DshAttachmentStore.cpp
  ${AVOX_AGENT}/provider/ChatProvider.cpp
  ${AVOX_AGENT}/policy/ModelRoutePolicy.cpp
  ${AVOX_SRC}/module/Sha256.cpp
  ${AVOX_SRC}/module/Utf8.cpp
  # teamtest 此前未在任何 CMakeLists 注册过, 从未编译, 本次一并纳入
  ${AVOX_AGENT}/team/TeamFold.cpp
  ${AVOX_AGENT}/team/TeamService.cpp
  ${AVOX_AGENT}/team/TeamValidation.cpp
  ${AVOX_AGENT}/team/TeamActivity.cpp
  ${AVOX_AGENT}/team/TeamJournal.cpp
  ${AVOX_AGENT}/team/TeamMailbox.cpp
  ${AVOX_AGENT}/team/TeamRoster.cpp
  ${AVOX_AGENT}/team/TeamTaskBoard.cpp
  ${AVOX_AGENT}/team/TeamTaskGraph.cpp
)

target_include_directories(avox_agent_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${AVOX_SRC}
  ${CMAKE_SOURCE_DIR}/3rdparty/doctest
)

target_compile_definitions(avox_agent_tests PRIVATE AVOX_EXPORT_DEFINE)

if(MSVC)
  target_compile_options(avox_agent_tests PRIVATE /utf-8)
endif()

# parserJson / saveImagePath-loadImagePath 等只存在于 avox 侧
target_link_libraries(avox_agent_tests PRIVATE avox)

# 链了 avox.dll: 输出到 avox 同目录 (那里还有 ffmpeg/OpenSSL 等一整串依赖 dll),
# 否则运行时加载不到 avox 的依赖, 进程直接 127 且无任何输出。
avox_output(avox_agent_tests)

add_test(NAME avox_agent_tests COMMAND avox_agent_tests)
