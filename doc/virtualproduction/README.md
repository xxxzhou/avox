# 虚拟制片 (Virtual Production / XR) 文档

> 整理同步自 aocec(aoce) 仓库的 `doc/virtualproduction/`、`doc/ue4/`、`doc/math/` 中与虚拟制片相关的部分, 2026-09 同步。
> 原仓库已冻结归档, 这些文档是现场项目(坪山 LED 虚拟拍摄、XR 演播)沉淀的一手经验, 标定相关算法细节与踩坑记录直接服务于 `plugins/avox_calib` 的移植整合(标定移植方案已随 avox_calib 落地)。
> 原文引用的配图(约 85MB 现场截图/动图)未同步, 需要看图请到 aocec 仓库 `assets/images/` 对应路径; 文中指向 aocec 代码的相对路径同理。

## 阅读顺序建议

1. [00-虚拟制片XR演播技术整理](00-虚拟制片XR演播技术整理.md) — 全景总览: 追踪/标定/图优化/GPGPU/传输/工具一条线
2. 标定主线: [相机标定方案](calib/相机标定方案.md) → [手眼标定算法改进](calib/手眼标定算法改进.md) → [标定开发踩坑记录](calib/标定开发踩坑记录.md)
3. 按需查阅其余各篇

## 目录

### 总览

| 文档 | 说明 |
|------|------|
| [00-虚拟制片XR演播技术整理](00-虚拟制片XR演播技术整理.md) | XR 演播系统全景: Redspy/MoSys 硬件、手眼标定、g2o 优化、变焦标定、延迟对齐、GPGPU 框架、畸变混合、LUT、外内视锥、图像源与传输、FBX/LiveLink 工具链 |

### calib/ — 标定与追踪

| 文档 | 说明 |
|------|------|
| [相机标定方案](calib/相机标定方案.md) | 标定做啥、五步流程、坐标系统约定、手眼 scale 列改进推导、LED 当标定板、Aruco 幕墙标定、FBX/LiveLink 坑 |
| [标定开发踩坑记录](calib/标定开发踩坑记录.md) | 一手开发日志(去重整理): **Redspy 欧拉角 YZ 取负的发现过程**、放弃先壤格式的教训、序列化精度劣化的根因与修法 |
| [手眼标定算法改进](calib/手眼标定算法改进.md) | Tsai 位移方程加 scale 列的完整推导(4 元 SVD), Redspy 位移缩放问题的解法 |
| [镜头标定与追踪器偏移](calib/镜头标定与追踪器偏移.md) | 张正友内参、UE4 内置标定插件、AX=XB 手眼标定与 OpenCV calibrateHandEye 源码解析 |
| [图优化手眼标定结果](calib/图优化手眼标定结果.md) | g2o 手眼再优化(HandEyeOptimizer)的模型构建与现场结果 |
| [LED虚拟拍摄-跟踪算法](calib/LED虚拟拍摄-跟踪算法.md) | 标定流程/手眼改进/图优化结果/变焦标定的算法综述 |
| [手眼标定精度记录](calib/手眼标定精度记录.md) | 精度提升实验的现象与数据记录 |
| [虚拟与现实图像重合](calib/虚拟与现实图像重合.md) | XR 虚实图像像素级对位: 中心偏移量化分析、getOptimalNewCameraMatrix 取中心区域、UV 映射去畸变 + compute shader |
| [时间对齐与时间码](calib/时间对齐与时间码.md) | 相机/追踪器四种丢帧延迟场景实测、丢帧时间码、Redspy 时码去重、延迟对齐实现要点 |
| [摄像机畸变校正基础](calib/摄像机畸变校正基础.md) | 针孔模型/内参/畸变模型基础与四个坐标系 |
| [先壤对接验收](calib/先壤对接验收.md) | 供应商(先壤)标定代码与 LUT 生成工具的验收笔记 |
| [g2o笔记](calib/g2o笔记.md) | g2o 顶点/边/雅可比 API 速记(M2 引入 g2o 时参考) |
| [时间序列与时空校准](calib/时间序列与时空校准.md) | DTW/卡尔曼/时空校准论文索引 + hecoos LED 格子二进制索引标定法 |

### xr/ — 渲染与传输

| 文档 | 说明 |
|------|------|
| [XR虚实相机混合](xr/XR虚实相机混合.md) | 真实相机画面与 UE 虚拟画面 GPU 混合: 畸变校正 shader、像素偏移 |
| [XR视锥渲染与nDisplay](xr/XR视锥渲染与nDisplay.md) | 外视锥/内视锥原理、nDisplay 代码跟踪方法与 NV SwapLock 同步 |
| [Switchboard状态同步解析](xr/Switchboard状态同步解析.md) | UE Switchboard 多机状态同步协议与实现解析 |
| [YUV10B与RGBA互转GPGPU](xr/YUV10B与RGBA互转GPGPU.md) | 10bit YUV ↔ RGBA 的 Vulkan compute shader 实现(16bit 存储) |
| [Rivermax笔记](xr/Rivermax笔记.md) | NVIDIA Rivermax(SMPTE ST 2110) 开发笔记与实现分析(含 GPU RDMA, 现场遗留撕裂问题, 留档参考) |

### ue/ — UE 引擎对接

| 文档 | 说明 |
|------|------|
| [UE4-LiveLink](ue/UE4-LiveLink.md) | LiveLink 类解析/延迟平滑/使用流程(自研 LiveLinkTvp 发送端的依据) |
| [UE4-镜头校正](ue/UE4-镜头校正.md) | UE 镜头校正插件改进、内外参、镜头节点偏移(WITH_OPENCV 代码) |
| [UE4-图像转虚拟相机](ue/UE4-图像转虚拟相机.md) | UE 画面转系统虚拟相机的流程方案(vcam) |
| [UE4插件与纹理对接](ue/UE4插件与纹理对接.md) | UE 插件模块化设计(UBT/UHT/延迟加载 dll)、Vulkan/DX11/DX12 纹理交互注意点 |

### 根目录

| 文档 | 说明 |
|------|------|
| [基础概念整理](基础概念整理.md) | 镜头/曝光/焦距、追踪器(Redspy/Vive)、算法杂记(DTW/三角测量/SVD)、知乎系列文章索引 |

## 未同步部分 (仍在 aocec, 按需再取)

- `ue4/` 通用 UE 学习笔记(UE4整理/UI/RHI/渲染顺序/骨骼动画/骨骼/读取模型数据/编辑器扩展) — 与虚拟制片无直接关系
- `math/` 通用数学学习笔记(矩阵学习/矩阵转换/深度卷积神经网络/相机/资料)
- 原文指向 aocec 代码/资产的相对链接保留原样, 指向 `Q:\Work\github\aocec` 仓库对应文件(见各篇头部说明)
