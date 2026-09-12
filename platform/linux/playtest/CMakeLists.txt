# Linux 播放回归矩阵 runner: 无头离屏 console exe, 跑完退出码 0=全过
# (窗口模式待 X11 宿主补充, 与 platform/macos/playtest 的 WinHost 同模式)
#
# 宿主源码与用例表都在 tests/playmatrix (与 Windows/Android/Apple 同一份):
#   PlayMatrix.hpp  用例表 + 判定逻辑
#   HostMain.cpp    命令行宿主 (参数解析 + 资源定位)
# 跑法 (WSL/桌面 Linux, 拉局域网 ZLM):
#   ./playtest --host=192.168.x.x
# 离线子集 (仅本地文件用例, CI 用): script/testenv/play_regress.py --offline

add_executable(playtest ${CMAKE_SOURCE_DIR}/tests/playmatrix/HostMain.cpp)

target_include_directories(playtest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  # tests/ 本身作为根, 代码里以 playmatrix/PlayMatrix.hpp 引用
  ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(playtest avox)

avox_output(playtest)
