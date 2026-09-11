# macOS 播放回归矩阵 runner: 无头离屏, 跑完退出码 0=全过
# (画面: shot/frame 用例落 PNG 到产物目录; 活窗口 --win 目前仅 Windows 宿主实现)
#
# 宿主源码与用例表都在 tests/playmatrix (与 Windows/Android 同一份):
#   PlayMatrix.hpp  用例表 + 判定逻辑
#   HostMain.cpp    命令行宿主 (参数解析 + 资源定位)
# 跑法 (拉局域网 ZLM):
#   ./playtest --host=192.168.68.245
# 日志: 产物目录 pm_log.txt (stdout 全量镜像, 事后可复判)

add_executable(playtest ${CMAKE_SOURCE_DIR}/tests/playmatrix/HostMain.cpp)

target_include_directories(playtest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  # tests/ 本身作为根, 代码里以 playmatrix/PlayMatrix.hpp 引用
  ${CMAKE_SOURCE_DIR}/tests
)

target_link_libraries(playtest avox)

avox_output(playtest)
