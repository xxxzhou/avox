
# 添加 git 版本信息
set(AVOX_COMMIT_HASH "Git_Unkown_commit")
set(AVOX_COMMIT_TIME "Git_Unkown_time")
set(AVOX_BRANCH_NAME "Git_Unkown_branch")
set(AVOX_BUILD_TIME "")
set(AVOX_COMMIT_VERSION "1.0.0000")

string(TIMESTAMP AVOX_BUILD_TIME "%Y-%m-%d %H:%M:%S")

# === 版本基线 (人工维护,大版本/特殊节点时改这里;此文件进 git) ===
# 默认全 0:版本号 = 1.0.<commit总数>,从仓库第一个 commit 起算 (commit 1 -> 1.0.0001)
#
# 手动跳版本示例:
#   configure 时打印 "AVOX version is 1.1.3212 (commit count: 13212)"
#   想让版本从 1.2.0001 重新起算,改成下面四行并提交:
#     set(AVOX_VERSION_MAJOR      1)
#     set(AVOX_BASE_MINOR         2)
#     set(AVOX_BASE_PATCH         1)
#     set(AVOX_BASE_COMMIT_COUNT  13212)   # 设基线时的 commit 总数
#   之后: 13212->1.2.0001, 13213->1.2.0002, ..., 23212->1.3.0001
set(AVOX_VERSION_MAJOR      1)
set(AVOX_BASE_MINOR         0)
set(AVOX_BASE_PATCH         0)
set(AVOX_BASE_COMMIT_COUNT  0)

find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
    OUTPUT_VARIABLE AVOX_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  execute_process(
    COMMAND ${GIT_EXECUTABLE} symbolic-ref --short -q HEAD
    OUTPUT_VARIABLE AVOX_BRANCH_NAME
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

  execute_process(
    COMMAND ${GIT_EXECUTABLE} log --format=format:%aI -1
    OUTPUT_VARIABLE AVOX_COMMIT_TIME
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

  # commit 总数:版本号递增的唯一驱动,任何人 clone 都得到相同值
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
    OUTPUT_VARIABLE _AVOX_COMMIT_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

# 无 git 或失败时回退到基线本身 (1.0.0000)
if(NOT DEFINED _AVOX_COMMIT_COUNT OR _AVOX_COMMIT_COUNT STREQUAL "")
  set(_AVOX_COMMIT_COUNT ${AVOX_BASE_COMMIT_COUNT})
endif()

# 版本 = 基线 + (当前 commit 数 - 基线 commit 数)
# 折算成连续 patch 计数,满 10000 进位到 minor
math(EXPR _AVOX_BASE_TOTAL "${AVOX_BASE_MINOR} * 10000 + ${AVOX_BASE_PATCH}")
math(EXPR _AVOX_DELTA "${_AVOX_COMMIT_COUNT} - ${AVOX_BASE_COMMIT_COUNT}")
if(_AVOX_DELTA LESS 0)
  message(WARNING "AVOX_BASE_COMMIT_COUNT(${AVOX_BASE_COMMIT_COUNT}) > 当前 commit 数(${_AVOX_COMMIT_COUNT}),版本号将回退到基线")
  set(_AVOX_DELTA 0)
endif()
math(EXPR _AVOX_TOTAL "${_AVOX_BASE_TOTAL} + ${_AVOX_DELTA}")

math(EXPR AVOX_VERSION_MINOR "${_AVOX_TOTAL} / 10000")
math(EXPR _AVOX_VERSION_PATCH "${_AVOX_TOTAL} % 10000")

# 数字版 patch (无前导零),供 AvoxVersion.h 的 AVOX_VER_PATCH / .rc FILEVERSION 使用
set(AVOX_VERSION_PATCH_NUM "${_AVOX_VERSION_PATCH}")

# 格式化补零四位 (math(EXPR) 结果为纯数字,无前导零)
string(LENGTH "${_AVOX_VERSION_PATCH}" _AVOX_PATCH_LEN)
if(_AVOX_PATCH_LEN EQUAL 1)
  string(CONCAT _AVOX_VERSION_PATCH "000" "${_AVOX_VERSION_PATCH}")
elseif(_AVOX_PATCH_LEN EQUAL 2)
  string(CONCAT _AVOX_VERSION_PATCH "00" "${_AVOX_VERSION_PATCH}")
elseif(_AVOX_PATCH_LEN EQUAL 3)
  string(CONCAT _AVOX_VERSION_PATCH "0" "${_AVOX_VERSION_PATCH}")
endif()

set(AVOX_COMMIT_VERSION "${AVOX_VERSION_MAJOR}.${AVOX_VERSION_MINOR}.${_AVOX_VERSION_PATCH}")

configure_file(
  ${PROJECT_SOURCE_DIR}/cmake/AvoxVersion.h.in
  ${PROJECT_SOURCE_DIR}/src/avox/AvoxVersion.h
  @ONLY)

message(STATUS "Git version is ${AVOX_BRANCH_NAME} ${AVOX_COMMIT_HASH}/${AVOX_COMMIT_TIME} ${AVOX_BUILD_TIME}")
message(STATUS "AVOX version is ${AVOX_COMMIT_VERSION} (commit count: ${_AVOX_COMMIT_COUNT})")
