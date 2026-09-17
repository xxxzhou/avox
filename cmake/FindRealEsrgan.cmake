# FindRealEsrgan.cmake
# 查找 Real-ESRGAN 模型权重(VkQEnhanceLayer 画质增强; 数据文件, 无头文件/库)
#
# 权重预置在库仓 assets/models/quality/(平台无关, 见库仓 README 溯源与许可)
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

# 默认搜索路径: AVOX_EXTERNAL_LIBRARY_DIR 优先(同 FindONNX), 库仓/工程内兜底
if(DEFINED AVOX_EXTERNAL_LIBRARY_DIR)
    set(RealEsrgan_SEARCH_PATHS
        ${AVOX_EXTERNAL_LIBRARY_DIR}/assets/models
        ${PROJECT_SOURCE_DIR}/../avc_library/assets/models
        ${PROJECT_SOURCE_DIR}/assets/models
    )
else()
    set(RealEsrgan_SEARCH_PATHS
        ${PROJECT_SOURCE_DIR}/../avc_library/assets/models
        ${PROJECT_SOURCE_DIR}/assets/models
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
