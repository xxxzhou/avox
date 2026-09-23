# FindRealEsrgan.cmake
# 查找 Real-ESRGAN 模型权重(VkQEnhanceLayer 画质增强; 数据文件, 无头文件/库)
#
# 权重权威位置: 本仓 assets/models/quality/(平台无关, 大文件暂走宿主注入,
# 未进 LFS); ../avox_library/assets/models 是历史路径, 仅作迁移期兜底
# 用法:
#   find_package(RealEsrgan QUIET)
#
# 定义变量:
#   RealEsrgan_FOUND        - 是否找到
#   REALESRGAN_MODEL_FILE   - 权重文件全路径
#   REALESRGAN_MODEL_DIR    - 所在目录(quality/, 供 assets 拷贝通道整目录取用)
#
# 运行期: 构建拷到 <avox.dll>/assets/models/quality/ 后由
# getModelFilePath("quality/realesrgan-general-x4v3.onnx") 解析;
# 宿主注入 modelsRoot 时优先生效, 构建缺权重不致命(降级为仅宿主注入可用)

include(FindPackageHandleStandardArgs)

set(REALESRGAN_MODEL_NAME "realesrgan-general-x4v3.onnx")

# 仓内 assets 优先; 外部库仓路径是权重入库前的旧位置, 留作兜底
if(DEFINED AVOX_EXTERNAL_LIBRARY_DIR)
    set(RealEsrgan_SEARCH_PATHS
        ${PROJECT_SOURCE_DIR}/assets/models
        ${AVOX_EXTERNAL_LIBRARY_DIR}/assets/models
        ${PROJECT_SOURCE_DIR}/../avox_library/assets/models
    )
else()
    set(RealEsrgan_SEARCH_PATHS
        ${PROJECT_SOURCE_DIR}/assets/models
        ${PROJECT_SOURCE_DIR}/../avox_library/assets/models
    )
endif()

set(REALESRGAN_MODEL_FILE "")
foreach(search_path ${RealEsrgan_SEARCH_PATHS})
    if(EXISTS "${search_path}/quality/${REALESRGAN_MODEL_NAME}")
        set(REALESRGAN_MODEL_FILE "${search_path}/quality/${REALESRGAN_MODEL_NAME}")
        break()
    endif()
endforeach()

if(REALESRGAN_MODEL_FILE)
    get_filename_component(REALESRGAN_MODEL_DIR "${REALESRGAN_MODEL_FILE}" DIRECTORY)
endif()

find_package_handle_standard_args(RealEsrgan
    REQUIRED_VARS REALESRGAN_MODEL_FILE
)

if(RealEsrgan_FOUND)
    message(STATUS "Real-ESRGAN model: ${REALESRGAN_MODEL_FILE}")
else()
    message(STATUS "Real-ESRGAN model not found")
    message(STATUS "  权重缺位: 画质增强需宿主注入 modelsRoot(quality/realesrgan-general-x4v3.onnx)")
endif()

mark_as_advanced(REALESRGAN_MODEL_FILE REALESRGAN_MODEL_DIR)
