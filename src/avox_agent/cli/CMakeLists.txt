# avox_agent — agent 对话工具 (薄壳)
# 交互逻辑折进 avox.dll (AgentShell 经 add_sub_path), 本程序经 C 导出入口 agentShellRun 调用。
add_executable(avox_agent main.cpp)
if(WIN32)
  target_sources(avox_agent PRIVATE avox_agent.rc)
endif()
target_compile_options(avox_agent PRIVATE ${COMPILE_OPTIONS_DEFAULT})
target_link_libraries(avox_agent PRIVATE avox)
avox_output(avox_agent)
