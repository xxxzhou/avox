# 查找FREETYPE的头文件目录
find_path(FREETYPE_INCLUDE_DIR
    NAMES freetype/freetype.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/freetype/include)  
set(FREETYPE_INCLUDE_DIRS ${FREETYPE_INCLUDE_DIR})
message(STATUS "FREETYPE_INCLUDE_DIR: ${FREETYPE_INCLUDE_DIR}")
set(DLLNAME freetype)
# if(AVOX_DEBUG)
#     set(DLLNAME freetyped)
# endif()
# 设置FREETYPE库文件所在目录
if(WIN32)
    set(FREETYPE_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/freetype/${CMAKE_BUILD_TYPE})
elseif(IOS)
    set(FREETYPE_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/freetype/${CMAKE_BUILD_TYPE}-iphoneos)      
elseif(ANDROID OR UNIX)
    set(FREETYPE_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/freetype)
endif()

message(STATUS "FIND FREETYPE_LIB_DIR: ${FREETYPE_LIB_DIR}")
# 如果是
# 查找FREETYPE库和mk_api库
find_library(FREETYPE_LIBRARY
    NAMES ${DLLNAME}
    NAMES_PER_DIR    
    PATHS ${FREETYPE_LIB_DIR}
    PATH_SUFFIXES lib  # 添加子目录搜索
    NO_DEFAULT_PATH # 禁止搜索系统路径
    )
set(FREETYPE_LIBRARYS ${FREETYPE_LIBRARY})  
message(STATUS "FREETYPE_LIBRARY: ${FREETYPE_LIBRARY}")
# FREETYPE_FOUND变量
include(FindPackageHandleStandardArgs)
# 需要注意FREETYPE和文件FindFREETYPE.cmake要一致，大小写一致
find_package_handle_standard_args(FREETYPE DEFAULT_MSG FREETYPE_LIBRARYS FREETYPE_INCLUDE_DIRS)

if(FREETYPE_FOUND)    
    if(WIN32)        
        find_file(FREETYPE_API_DLLS
            NAMES ${DLLNAME}.dll
            PATHS ${FREETYPE_LIB_DIR})             
        # avox_run_module_copy("${FREETYPE_API_DLLS}")
    else()
        avox_run_module_copy("${FREETYPE_LIBRARY}")
    endif()
endif()
