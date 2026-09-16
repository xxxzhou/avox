# A-11 超分/插帧产品化

> 状态: 进行中 · 上次核对: 2026-09-16 · 权威源: -


优先级 P1 · 里程碑 M4 · 计划状态:就绪(底子完整,差「产品化三件套」)
Anime4K 已整合 → 运行时开关 + 质量分档 + GPU 能力探测自动降级;RIFE 插帧可行性评估。

## 出口判据

1. Auto 档:按 GPU 能力/实测耗时自动选档,弱 iGPU 不超时不炸。
2. 分档真实生效:S/M/L(或收口后的档位)画质差异可见、耗时可测。
3. 运行时开关生命周期稳(切档/开关热重建无黑屏),与 panvox P-17 画质 UI 对接顺畅。
4. RIFE 评估文档一份(做不做的结论 + 依据),不承诺实现。

## 现状(代码落点)

- **Anime4K 完整整合**:`src/avox_vulkan/quality/VkAnime4KLayer.hpp:10-151`
  (8 个子层:Restore + Upscale + Clamp Highlights),运行时开关公开 API
  (`src/avox/AvoxLayer.h:368`),graph 插入(`VkVideoRender.cpp:379-381,439-442`),
  与 Real-ESRGAN/FSR 互斥管理(:256-299),参数热重建(`VkAnime4KLayer.cpp:203-209`)。
- **分档 API 是空壳**:`AvoxLayer.h:229-239` ModeA/B/C + Variant S/M/L(注释有耗时预估),
  但 glsl 20 个 anime4k comp **全是 Medium 变体**,`VkAnime4KLayer.cpp:224-225` 硬编码
  `anime4k_restore_m_`——**切 S/L 会加载失败**(样例 anime4ktest.cpp 允许切,隐患已在)。
- **GPU 能力探测基本没有**:只有设备枚举/格式查询(`VkContext.cpp:194-216`、
  `VkCommon.cpp:338,392`);无 compute/存储图像特性探测、无耗时基准、无自动降档逻辑。
  Real-ESRGAN 的 Auto 档只按分辨率(`AvoxLayer.h:263`)不按 GPU。
- **RIFE 零代码零依赖**:无光流/多帧输入管线(现管线单帧 in/out),
  仅调研文档提及算子兼容隐患(`doc/plan/ai/监控画质增强方案调研.md:65`)。
- 另两档超分参照:FSR1.0(`quality/VkFSRLayer.*`)、Real-ESRGAN(ONNX 走 ovEngine +
  Vulkan 前后处理,跨平台可用性存疑)。

## 任务拆解

- [ ] T1 堵 S/L 空壳坑(先行):两选一——补 S/L 变体 comp(Anime4K 官方有 S/M/L 全套),
      或收口为 M 单档、API 档位语义改为「关/标准(Auto 内部 M)/强(待补)」。
      建议先收口语义防加载失败,再补资源。
- [ ] T2 能力探测 + Auto 档:启动时探测(compute 支持/时间戳/限制)+ 首帧实测基准
      (单帧耗时 > 预算即降档);Auto 逻辑放引擎层,产品只读结果。
- [ ] T3 产品化口径:开关/档位/状态查询 API 盘点(现有哪些已暴露、缺什么),
      与 panvox P-17 的 UI 需求对齐(开关持久化在产品侧);输出一份 FFI 对接说明。
- [ ] T4 RIFE 评估(文档):模型 ONNX 化可行性、多帧输入管线改造点
      (VkLayer 单帧约定 → 参考帧缓存)、4K/1080p 实时性预估;结论写 doc,不动代码。
- [ ] T5 用例:vulkantest anime4ktest 扩展档位矩阵 + avox-test 归档各档截图/耗时表
      (顺带是 P-20 演示视频素材)。

## 验收

- 弱 GPU(核显)设备 Auto 档不超时;S/M/L 生效差异截图对比;开关循环 100 次无泄漏无黑屏。

## 风险与开放问题

- S/L 全套 shader 编译产物体积增长(现在 177 个 comp),包体积口径要过一下。
- 「耗时预估」数字(注释 ~2/5/10ms)是拍的,T2 实测后回写注释。
- RIFE 大概率结论是「1080p 可试、4K 不实时」,评估文档要给明确不做线,防 P-17 UI 先画了饼。
