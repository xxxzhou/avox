set(FAAC_INC_SEARCH_PATH ${AVOX_TRDPARTY_LIBRAY}/faac/include)

if(WIN32)
    set(FAAC_LIB_SEARCH_PATH ${AVOX_TRDPARTY_LIBRAY}/faac/bin)
elseif(APPLE OR ANDROID)
    set(FAAC_LIB_SEARCH_PATH ${AVOX_TRDPARTY_LIBRAY}/faac/lib)
endif()

find_path(FAAC_INCLUDE_DIR
    NAMES faac.h
    PATHS ${FAAC_INC_SEARCH_PATH})
set(FAAC_INCLUDE_DIRS ${FAAC_INCLUDE_DIR})

# 使用静态链接
find_library(FAAC_LIBRARY
    NAMES faac
    PATHS ${FAAC_LIB_SEARCH_PATH})
set(FAAC_LIBRARYS ${FAAC_LIBRARY})

message(STATUS "FAAC_INCLUDE_DIRS: ${FAAC_INCLUDE_DIRS}")
message(STATUS "FAAC_LIBRARYS: ${FAAC_LIBRARYS}")

# FAAC_FOUND变量
include(FindPackageHandleStandardArgs)

# 需要注意FAAC和文件FindFAAD2.cmake要一致，大小写一致
find_package_handle_standard_args(FAAC DEFAULT_MSG FAAC_LIBRARYS FAAC_INCLUDE_DIRS)