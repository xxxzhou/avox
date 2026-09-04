# DeckLink(Blackmagic)采集卡动态插件, 仅Windows
if(NOT WIN32)
  return()
endif()
if(NOT AVOX_ENABLE_DECKLINK)
  message(STATUS "avox_decklink plugin: AVOX_ENABLE_DECKLINK 未启用, 跳过")
  return()
endif()

# SDK只有IDL, 构建期用midl生成DeckLinkAPI.h/DeckLinkAPI_i.c
# (运行期COM类由Desktop Video驱动注册, 无需链接任何DeckLink库)
set(DECKLINK_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/decklink_sdk)
set(DECKLINK_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/decklink_gen)
file(MAKE_DIRECTORY ${DECKLINK_GEN_DIR})
add_custom_command(
  OUTPUT ${DECKLINK_GEN_DIR}/DeckLinkAPI.h ${DECKLINK_GEN_DIR}/DeckLinkAPI_i.c
  COMMAND midl /h DeckLinkAPI.h /iid DeckLinkAPI_i.c /notlb /I ${DECKLINK_SDK_DIR} ${DECKLINK_SDK_DIR}/DeckLinkAPI.idl
  WORKING_DIRECTORY ${DECKLINK_GEN_DIR}
  DEPENDS ${DECKLINK_SDK_DIR}/DeckLinkAPI.idl
  COMMENT "midl: 生成 DeckLinkAPI.h/DeckLinkAPI_i.c"
  VERBATIM)

include_directories(${DECKLINK_GEN_DIR})

register_plugin(avox_decklink
  DYNAMIC
  SOURCES DeckLinkModule.cpp DeckLinkSource.cpp ${DECKLINK_GEN_DIR}/DeckLinkAPI_i.c)
