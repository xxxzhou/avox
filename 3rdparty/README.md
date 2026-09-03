# 引入第三方库

本项目使用的第三方库下载脚本和放置路径。

大部分简单并且自带多平台CMake构建的会使用git submodule方式引入.

如下是比较特殊的,主要在3rdparty/libray各平台目录下.

---

## 1. ONNX Runtime

ONNX 推理引擎库，用于 AI 模型推理。

### 下载脚本

```bash
# Windows CPU 版本
python script/onnx/down_onnxruntime_windows.py

# Windows GPU 版本
python script/onnx/down_onnxruntime_windows.py --gpu

# iOS 版本
python script/onnx/down_onnxruntime_ios.py
```

### 放置路径

| 平台 | 路径 |
|------|------|
| Windows | `3rdparty/library/windows/onnxruntime/` |
| Android | `3rdparty/library/android/onnxruntime/` |

---

## 2. OpenCV

图像处理库，用于图像加载、预处理等。

### 下载脚本

```bash
# Windows
python script/opencv/download_opencv.py --platform windows

# Android
python script/opencv/download_opencv.py --platform android

# iOS
python script/opencv/download_opencv.py --platform ios

# Linux
python script/opencv/download_opencv.py --platform linux
```

### 放置路径

| 平台 | 路径 |
|------|------|
| Windows | `3rdparty/library/windows/opencv/` |
| Android | `3rdparty/library/android/opencv/` |
| iOS | `3rdparty/library/ios/opencv/` |
| Linux | `3rdparty/library/linux/opencv/` |

---

## 3. 依赖安装 (Python)

YOLO 转 ONNX 工具依赖：

```bash
pip install torch ultralytics onnx onnxruntime
```
