# Android 播放回归矩阵 runner
#
# 出的是 console 可执行, 不是 APK: 用例表与判定口径与 Windows/Linux 共用
# tests/playmatrix, stdout 直接就是 [AVOX][TEST] 判定行, 无需 logcat 解析。
# 代价是不覆盖 Java 绑定层 (那层由 platform/android/AvoxJava testbed 与
# Godot 工具箱覆盖), 换来的是"同一个 runner 契约" —— 见 tests/playmatrix/README.md。
#
# 跑法 (无设备时只做编译验证):
#   python script/testenv/play_regress.py --android --serial=<序列号>
#   等价手工流程:
#     adb push <install>/aarch64/playtest 与全部 *.so 到 /data/local/tmp/playmatrix/
#     adb shell "cd /data/local/tmp/playmatrix && LD_LIBRARY_PATH=. ./playtest --host=<LAN IP>"

add_executable(playtest ${CMAKE_SOURCE_DIR}/tests/playmatrix/HostMain.cpp)

target_include_directories(playtest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  # tests/ 本身作为根, 代码里以 playmatrix/PlayMatrix.hpp 引用
  ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(playtest avox)
# 判定行只走 stdout, 但 avox 内部有 __android_log_print, 显式链上免得靠传递依赖
target_link_libraries(playtest log)

avox_output(playtest)
