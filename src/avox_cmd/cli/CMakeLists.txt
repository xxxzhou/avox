# avox_cli — avox 命令行工具 (独立可执行, 薄壳)
# 命令逻辑折进 avox.dll (avox_cmd 经 add_sub_path), 本程序经 C 导出入口 cmdExecute 调用。
add_executable(avox_cli main.cpp)
if(WIN32)
  target_sources(avox_cli PRIVATE avox_cli.rc)
endif()
target_compile_options(avox_cli PRIVATE ${COMPILE_OPTIONS_DEFAULT})
target_link_libraries(avox_cli PRIVATE avox)
avox_output(avox_cli)
