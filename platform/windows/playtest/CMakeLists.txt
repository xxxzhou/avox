# Windows 播放回归矩阵 runner: 无头离屏 console exe, 跑完退出码 0=全过
#
# 宿主源码与用例表都在 avox-test 仓的 l1_avox/playmatrix (与 Android/Linux/Apple
# 同一份; 本仓不再留副本, 免得两边手工同步):
#   PlayMatrix.hpp  用例表 + 判定逻辑
#   HostMain.cpp    命令行宿主 (参数解析 + 资源定位)
# 跑法: avox-test 仓 script/testenv/play_regress.py (推流 → 跑本程序 → 汇总判定行)

add_executable(playtest ${AVOX_TEST_PLAYMATRIX_DIR}/HostMain.cpp)

target_include_directories(playtest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  # l1_avox/ 作为根, 代码里以 playmatrix/PlayMatrix.hpp 引用
  ${AVOX_TEST_ROOT}/l1_avox
)

if(MSVC)
  # 源码含 UTF-8 中文注释, 与主工程保持一致
  target_compile_options(playtest PRIVATE /utf-8)
  # fopen/getenv 等 CRT 安全告警与主工程口径无关, 屏蔽以免噪音
  target_compile_definitions(playtest PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(playtest avox)

avox_output(playtest)
