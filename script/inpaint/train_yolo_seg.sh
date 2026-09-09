#!/bin/bash
# YOLO26-Seg 水印分割模型训练脚本
# 支持: WSL2+ROCm, Apple Silicon (MPS), CUDA
#
# 用法:
#   bash train_yolo_seg_rocm.sh [mini|base|high] [resume]
#
# 参数:
#   第一个参数: 模型等级 (mini/base/high, 默认 base)
#   第二个参数: resume - 从断点恢复训练
#
# ModelLevel 映射:
#   mini  -> yolo26n-seg (快速，效果一般)
#   base  -> yolo26m-seg (平衡，推荐)
#   high  -> yolo26x-seg (慢，效果好)
#
# 示例:
#   bash train_yolo_seg_rocm.sh base          # 从头训练 base 模型
#   bash train_yolo_seg_rocm.sh base resume   # 恢复 base 模型训练
#   bash train_yolo_seg_rocm.sh high          # 训练 high 模型

echo "=================================================="
echo "YOLO26-Seg 水印分割模型训练"
echo "=================================================="

# 模型等级参数
MODEL_LEVEL=${1:-base}
RESUME=${2:-""}

case $MODEL_LEVEL in
  mini)  MODEL_NAME="yolo26n-seg.pt" ;;
  base)  MODEL_NAME="yolo26m-seg.pt" ;;
  high)  MODEL_NAME="yolo26x-seg.pt" ;;
  *)     echo "错误: 未知的模型等级 '$MODEL_LEVEL'"; echo "用法: $0 [mini|base|high] [resume]"; exit 1 ;;
esac

# 检测操作系统和平台
detect_platform() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        if [[ $(uname -m) == "arm64" ]]; then
            echo "macos-arm64"
        else
            echo "macos-x64"
        fi
    elif [[ -f /proc/driver/nvidia/version ]]; then
        echo "linux-nvidia"
    elif python3 -c "import torch; exit(0 if torch.cuda.is_available() else 1)" 2>/dev/null; then
        echo "linux-amd"
    else
        echo "linux-cpu"
    fi
}

PLATFORM=$(detect_platform)
echo "检测到平台: $PLATFORM"

# 根据平台设置路径和设备
case $PLATFORM in
    macos-arm64|macos-x64)
        # macOS - 检查外挂硬盘
        PROJECT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
        if [ -d "/Volumes/PSSD/work/data" ]; then
            DATA_DIR="/Volumes/PSSD/work/data/inpaint_data"
        else
            DATA_DIR="/Users/$(whoami)/data/inpaint_data"
        fi
        DEVICE="mps"  # Apple Metal Performance Shaders
        ;;
    linux-nvidia)
        # Linux + NVIDIA GPU (WSL2)
        PROJECT_DIR="/mnt/d/Work/github/avox"
        DATA_DIR="/mnt/d/Work/data/inpaint_data"
        DEVICE="0"
        ;;
    linux-amd)
        # Linux + AMD GPU (ROCm)
        PROJECT_DIR="/mnt/d/Work/github/avox"
        DATA_DIR="/mnt/d/Work/data/inpaint_data"
        DEVICE="0"
        ;;
    *)
        # 默认/CPU
        PROJECT_DIR="/mnt/d/Work/github/avox"
        DATA_DIR="/mnt/d/Work/data/inpaint_data"
        DEVICE="cpu"
        ;;
esac

# 允许环境变量覆盖
if [ -n "$PROJECT_DIR_ENV" ]; then PROJECT_DIR="$PROJECT_DIR_ENV"; fi
if [ -n "$DATA_DIR_ENV" ]; then DATA_DIR="$DATA_DIR_ENV"; fi
if [ -n "$DEVICE_ENV" ]; then DEVICE="$DEVICE_ENV"; fi

MODEL_PATH="$PROJECT_DIR/assets/models/inpaint/$MODEL_NAME"
DATASET_YAML="$DATA_DIR/dataset.yaml"
RUN_DIR="$DATA_DIR/runs/watermark_seg_${MODEL_LEVEL}"

echo "项目目录: $PROJECT_DIR"
echo "数据目录: $DATA_DIR"
echo "设备: $DEVICE"

# 检查环境
echo ""
echo "[检查环境]"

# 检查 Python
if ! command -v python3 &> /dev/null; then
    echo "错误: Python3 未安装"
    exit 1
fi

# 检查 PyTorch
python3 -c "import torch; print(f'PyTorch: {torch.__version__}')" 2>/dev/null || {
    echo "错误: PyTorch 未安装"
    if [[ "$PLATFORM" == "macos-arm64" ]]; then
        echo "请运行: pip3 install torch torchvision"
    else
        echo "请运行: pip3 install torch torchvision"
    fi
    exit 1
}

# 检查设备可用性
python3 -c "
import torch
if torch.cuda.is_available():
    print(f'CUDA: {torch.cuda.get_device_name(0)}')
elif torch.backends.mps.is_available():
    print('MPS: Apple Silicon GPU')
else:
    print('CPU only')
" 2>/dev/null

# 检查 ultralytics
python3 -c "import ultralytics; print(f'Ultralytics: {ultralytics.__version__}')" 2>/dev/null || {
    echo "安装 ultralytics..."
    pip3 install ultralytics
}

# 检查数据集
if [ ! -f "$DATASET_YAML" ]; then
    echo "错误: 数据集配置不存在: $DATASET_YAML"
    echo "请先运行: python script/inpaint/download_watermark_images.py"
    exit 1
fi
echo "数据集: $DATASET_YAML"

# 自动检测 GPU 内存并设置合适的 batch
detect_auto_batch() {
    local default_batch=16

    # 使用 Python 检测 GPU 显存
    local gpu_memory_mb=$(python3 -c "
import torch
if torch.cuda.is_available():
    print(int(torch.cuda.get_device_properties(0).total_memory / 1024 / 1024))
elif torch.backends.mps.is_available():
    # MPS: 通过查询设备属性估算 (Apple Silicon 通常 8-24GB)
    import subprocess
    try:
        # 尝试从 system_profiler 获取
        result = subprocess.run(['system_profiler', 'SPMemoryDataType'], capture_output=True, text=True)
        for line in result.stdout.split('\n'):
            if 'Total Slots:' in line or 'RAM:' in line:
                # 粗略估算: Apple Silicon 统一内存，PyTorch 可用约 70%
                print(16384)  # 默认 16GB
                break
        else:
            print(16384)  # 默认 16GB
    except:
        print(16384)
else:
    print(0)
" 2>/dev/null)

    if [ -z "$gpu_memory_mb" ] || [ "$gpu_memory_mb" = "0" ]; then
        echo 2
        return
    fi

    echo "检测到 GPU 显存: ${gpu_memory_mb}MB" >&2

    # 根据显存选择 batch
    if [ "$gpu_memory_mb" -ge 24000 ]; then
        echo 16   # 24GB+ (如 RTX 4090)
    elif [ "$gpu_memory_mb" -ge 16000 ]; then
        echo 8   # 16GB (如 RTX 4080, Apple Silicon M3 Max)
    elif [ "$gpu_memory_mb" -ge 12000 ]; then
        echo 4   # 12GB (如 RTX 4070, Apple Silicon M2 Pro)
    else
        echo 2    # 小于 6GB
    fi
}

# 训练参数
EPOCHS=${EPOCHS:-50}
BATCH=${BATCH:-8}
echo "Batch: $BATCH"
IMGSZ=${IMGSZ:-640}
# 每 5 个 epoch 保存一次
SAVE_PERIOD=${SAVE_PERIOD:-5}

echo ""
echo "=================================================="
echo "开始训练"
echo "=================================================="
echo "ModelLevel: $MODEL_LEVEL"
echo "Model: $MODEL_NAME"
echo "Epochs: $EPOCHS"
echo "Batch: $BATCH"
echo "Image size: $IMGSZ"
echo "Save period: 每 $SAVE_PERIOD epochs 保存一次"
echo "Resume: $([ "$RESUME" == "resume" ] && echo "是" || echo "否")"
echo "=================================================="

# 运行训练
cd "$PROJECT_DIR"

# 构建训练命令
TRAIN_CMD="yolo segment train \
    model=$MODEL_NAME \
    data=$DATASET_YAML \
    epochs=$EPOCHS \
    batch=$BATCH \
    imgsz=$IMGSZ \
    device=$DEVICE \
    project=$DATA_DIR/runs \
    name=watermark_seg_${MODEL_LEVEL} \
    exist_ok=True \
    plots=True \
    val=True \
    save=True \
    save_period=$SAVE_PERIOD"

# 如果是恢复训练
if [ "$RESUME" == "resume" ]; then
    if [ -f "$RUN_DIR/weights/last.pt" ]; then
        echo "从断点恢复: $RUN_DIR/weights/last.pt"
        TRAIN_CMD="yolo segment train \
            model=$RUN_DIR/weights/last.pt \
            data=$DATASET_YAML \
            epochs=$EPOCHS \
            batch=$BATCH \
            imgsz=$IMGSZ \
            device=$DEVICE \
            project=$DATA_DIR/runs \
            name=watermark_seg_${MODEL_LEVEL} \
            exist_ok=True \
            plots=True \
            val=True \
            save=True \
            save_period=$SAVE_PERIOD \
            resume=True"
    else
        echo "警告: 未找到断点文件 $RUN_DIR/weights/last.pt"
        echo "将从头开始训练"
    fi
fi

# 执行训练
eval $TRAIN_CMD

# 检查训练结果
if [ $? -ne 0 ]; then
    echo ""
    echo "=================================================="
    echo "训练中断或失败"
    echo "可以使用以下命令恢复训练:"
    echo "  bash $0 $MODEL_LEVEL resume"
    echo "=================================================="
    exit 1
fi

# 导出 ONNX
OUTPUT_DIR="$RUN_DIR/weights"
if [ -f "$OUTPUT_DIR/best.pt" ]; then
    echo ""
    echo "=================================================="
    echo "导出 ONNX 模型"
    echo "=================================================="

    # 根据模型等级确定 ONNX 文件名
    case $MODEL_LEVEL in
        mini)  ONNX_NAME="yolo11n-seg_watermark.onnx" ;;
        base)  ONNX_NAME="yolo11m-seg_watermark.onnx" ;;
        high)  ONNX_NAME="yolo11x-seg_watermark.onnx" ;;
    esac

    yolo export model="$OUTPUT_DIR/best.pt" format=onnx

    # 复制到 models 目录
    mkdir -p "$PROJECT_DIR/assets/models/inpaint"
    if [ -f "$OUTPUT_DIR/best.onnx" ]; then
        cp "$OUTPUT_DIR/best.onnx" "$PROJECT_DIR/assets/models/inpaint/$ONNX_NAME"
        echo "ONNX 模型已保存: assets/models/inpaint/$ONNX_NAME"
    fi
fi

echo ""
echo "=================================================="
echo "训练完成!"
echo "结果保存在: $RUN_DIR/"
echo ""
echo "检查点文件:"
echo "  - $RUN_DIR/weights/last.pt   (最新检查点)"
echo "  - $RUN_DIR/weights/best.pt   (最佳模型)"
echo ""
echo "恢复训练命令:"
echo "  bash $0 $MODEL_LEVEL resume"
echo "=================================================="
