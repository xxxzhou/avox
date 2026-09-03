# 查找 sentencepiece 的头文件目录
find_path(SPM_INCLUDE_DIR
    NAMES sentencepiece_processor.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/sentencepiece/src)
set(SPM_INCLUDE_DIRS ${SPM_INCLUDE_DIR})
message(STATUS "SPM_INCLUDE_DIRS: ${SPM_INCLUDE_DIRS}")

# 设置 sentencepiece 库文件所在目录
if(WIN32)
    set(SPM_LIB_DIR ${CMAKE_SOURCE_DIR}/build/windows/sentencepiece/src/${CMAKE_BUILD_TYPE})
elseif(IOS)
    set(SPM_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sentencepiece/src/${CMAKE_BUILD_TYPE}-iphoneos)
elseif(ANDROID OR UNIX)
    set(SPM_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/sentencepiece/src)
endif()

message(STATUS "SPM_LIB_DIR: ${SPM_LIB_DIR}")

# 辅助函数：查找库
macro(spm_find_lib VAR NAME LIB_DIR)
    find_library(${VAR}
        NAMES ${NAME}
        NAMES_PER_DIR
        PATHS ${LIB_DIR}
        NO_DEFAULT_PATH)
endmacro()

# 查找 sentencepiece 主库
spm_find_lib(SPM_LIBRARY sentencepiece ${SPM_LIB_DIR})

# 组合所有库
set(SPM_LIBRARIES ${SPM_LIBRARY})

# Windows 需要额外链接系统库
if(WIN32)
    set(SPM_LIBRARIES ${SPM_LIBRARIES} shlwapi)
endif()

message(STATUS "SPM_LIBRARIES: ${SPM_LIBRARIES}")

# sentencepiece_FOUND 变量
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SentencePiece DEFAULT_MSG SPM_LIBRARY SPM_INCLUDE_DIRS)

if(SentencePiece_FOUND)
    # 在Windows平台下，如果找到SDL库
    if(ANDROID)
        avox_run_module_copy("${SPM_LIBRARIES}")
    endif()
endif()