# 查找FDKAAC的头文件目录
find_path(FDKAAC_ENC_INCLUDE_DIR
    NAMES aacenc_lib.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/fdk-aac/libAACenc/include)
find_path(FDKAAC_DEC_INCLUDE_DIR
    NAMES aacdecoder_lib.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/fdk-aac/libAACdec/include)    
find_path(FDKMACHINE_INCLUDE_DIR
    NAMES machine_type.h
    PATHS ${PROJECT_SOURCE_DIR}/3rdparty/fdk-aac/libSYS/include)   
set(FDKAAC_INCLUDE_DIRS ${FDKAAC_ENC_INCLUDE_DIR} ${FDKAAC_DEC_INCLUDE_DIR} ${FDKMACHINE_INCLUDE_DIR})
message(STATUS "FDKAAC_INCLUDE_DIRS: ${FDKAAC_INCLUDE_DIRS}")  
# 设置FDKAAC库文件所在目录
if(WIN32)
    set(FDKAAC_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/fdk-aac/${CMAKE_BUILD_TYPE}) 
elseif(IOS)
    set(FDKAAC_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/fdk-aac/${CMAKE_BUILD_TYPE}-iphoneos)          
elseif(ANDROID OR UNIX)
    set(FDKAAC_LIB_DIR ${AVOX_MOEDULE_BUILD_DIR}/fdk-aac)
endif()

message(STATUS "FIND FDKAAC_LIB_DIR: ${FDKAAC_LIB_DIR}")
# 查找FDKAAC库和mk_api库
find_library(FDKAAC_LIBRARY
    NAMES fdk-aac
    NAMES_PER_DIR    # 在每个目录下尝试所有名称组合
    PATHS ${FDKAAC_LIB_DIR}
    PATH_SUFFIXES lib  # 添加子目录搜索
    NO_DEFAULT_PATH # 禁止搜索系统路径
    )
set(FDKAAC_LIBRARYS ${FDKAAC_LIBRARY})  
message(STATUS "FDKAAC_LIBRARY: ${FDKAAC_LIBRARY}")
# FDKAAC_FOUND变量
include(FindPackageHandleStandardArgs)
# 需要注意FDKAAC和文件FindFDKAAC.cmake要一致，大小写一致
find_package_handle_standard_args(FDKAAC DEFAULT_MSG FDKAAC_LIBRARYS FDKAAC_INCLUDE_DIRS)

if(FDKAAC_FOUND)
    # 在Windows平台下，如果找到FDKAAC库
    if(WIN32)
        # 查找fdk-aac.dll文件
        find_file(FDKAAC_API_DLLS
            NAMES fdk-aac.dll
            PATHS ${FDKAAC_LIB_DIR})             
        avox_run_module_copy("${FDKAAC_API_DLLS}")
    else()
        avox_run_module_copy("${FDKAAC_LIBRARY}")
    endif()
endif()