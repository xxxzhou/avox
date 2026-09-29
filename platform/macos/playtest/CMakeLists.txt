# macOS 播放回归矩阵 runner: 缺省无头离屏, 跑完退出码 0=全过
# --win: AppKit 窗口出画面 + 判定横幅 (WinHost.mm), 日志照旧落盘
#
# 宿主源码与用例表都在 avox-test 仓的 l1_avox/playmatrix (与 Windows/Android 同一份):
#   PlayMatrix.hpp  用例表 + 判定逻辑
#   HostMain.cpp    命令行宿主 (参数解析 + 资源定位)
# 跑法 (拉局域网 ZLM):
#   ./playtest --host=192.168.3.31            # 无头, 产物目录落 PNG/pm_log.txt
#   ./playtest --host=192.168.3.31 --win      # 出窗口看画面, 横幅滚动判定行
# 日志: 产物目录 pm_log.txt (stdout 全量镜像, 事后可复判)

add_executable(playtest
  ${AVOX_TEST_PLAYMATRIX_DIR}/HostMain.cpp
  WinHost.mm
)

# WinHost.mm 按 ARC 语义编写 (CMake 生成工程默认 MRC)
set_source_files_properties(WinHost.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

target_include_directories(playtest PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  # l1_avox/ 作为根, 代码里以 playmatrix/PlayMatrix.hpp 引用
  ${AVOX_TEST_ROOT}/l1_avox
)

# whole-archive + 关 dead_strip: 模块注册 (AVOX_REGISTER_MODULE/ZLM 事件名) 靠静态
# 初始化, Xcode Release 默认死代码剥离会把它们剥掉 → createWebRtcPlayer 返回 null
target_link_libraries(playtest "$<LINK_LIBRARY:WHOLE_ARCHIVE,avox>")
set_target_properties(playtest PROPERTIES XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING "NO")

avox_output(playtest)
avox_check_no_openssl(playtest)

# 动态插件可见性(2026-09-29 实证): 引擎按「可执行文件旁 plugins/」解析(dladdr→静态链
# 时即主可执行文件), .app 内可执行在 Contents/MacOS → 建链接指回 install 的 plugins/,
# 否则动态插件全静默缺失(createRemoteSource 返回 null); 另导出引擎符号供插件
# dlopen(RTLD_NOW) 解析(含仅被插件引用的类, 如 BaseSource::disableAudio)
add_custom_command(TARGET playtest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink "../../../plugins"
          "$<TARGET_FILE_DIR:playtest>/plugins"
  COMMENT "playtest: link bundle plugins -> install plugins")
target_link_options(playtest PRIVATE "-Wl,-export_dynamic")
