# Windows 播放回归矩阵 runner: 无头离屏 console exe, 跑完退出码 0=全过
#
# 宿主源码与用例表都在 tests/playmatrix (与 Android/Linux/Apple 同一份):
#   PlayMatrix.hpp  用例表 + 判定逻辑
#   HostMain.cpp    命令行宿主 (参数解析 + 资源定位)
# 跑法: script/testenv/play_regress.py (推流 → 跑本程序 → 汇总判定行)

add_executable(playtest ${CMAKE_SOURCE_DIR}/tests/playmatrix/HostMain.cpp)

target_include_directories(playtest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  # tests/ 本身作为根, 代码里以 playmatrix/PlayMatrix.hpp 引用
  ${CMAKE_SOURCE_DIR}/tests
)

if(MSVC)
  # 源码含 UTF-8 中文注释, 与主工程保持一致
  target_compile_options(playtest PRIVATE /utf-8)
  # fopen/getenv 等 CRT 安全告警与主工程口径无关, 屏蔽以免噪音
  target_compile_definitions(playtest PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(playtest avox)

avox_output(playtest)
