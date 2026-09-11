# 标准测试源 (ffmpeg lavfi 生成)

`assets/video` 根目录的两个样本 (avox_electron/webrtc_pull) 是**屏幕录制内容**,
用于贴近真实使用; 本目录是**标准测试图案**源, 专门用于解码/渲染/对齐回归。
全部 12s、带烧录时间码与运动元素的 testsrc2 图案 (彩条文件为 SMPTE bars),
单文件 ≤3MB。命名 `test_<视频编码>_<音频>_<宽>x<高>`。

| 文件 | 视频编码 | 音频 | 宽x高 | 覆盖点 |
|------|---------|------|-------|--------|
| test_h264_aac_320x240.mp4 | h264 | aac | 320x240 | 小尺寸基线 |
| test_h264_aac_640x360.mp4 | h264 | aac | 640x360 | 标准规格 |
| test_h264_mp3_638x360.mp4 | h264 | **mp3** | **638**x360 | mp3 音频 + 非 16 对齐宽 (638%16=14) |
| test_h264_nosound_482x362.mp4 | h264 | **无音轨** | **482x362** | 无音频 + 宽高均非 16 对齐 |
| test_h264_opus_640x360.mp4 | h264 | **opus**(mp4) | 640x360 | opus-in-mp4 容器组合 |
| test_h264_smpte_640x360.mp4 | h264 | aac(1kHz) | 640x360 | SMPTE 彩条, 人工校色用 |
| test_h265_aac_960x540.mp4 | **h265** | aac | 960x540 | h265 标准规格 (hvc1 tag) |
| test_h265_mp3_640x360.mp4 | **h265** | **mp3** | 640x360 | h265 + mp3 组合 |
| test_mpeg4_odd_639x360.mp4 | **mpeg4** | aac | **639x360 真奇数宽** | 奇数宽 + 旧编解码器, 软解专用 |
| test_vp9_opus_640x360.webm | **vp9** | **opus**(**webm容器**) | 640x360 | webm 容器 + vp9/opus |

## 关于"奇数宽"

h264/h265 的 4:2:0 色度采样在编码层面就要求宽高为偶数 (x264/x265/libvpx 全部会
直接舍掉奇数列), 无法产出真奇数宽文件; 真·奇数宽只能走 4:4:4/不采样色度的编码,
本目录用 mpeg4 (位流允许奇数宽, ffprobe 实测 639)。日常对齐回归用 638/482x362
这类"偶数但非 16 对齐"文件已能覆盖 stride/pitch 风险, 奇数文件是极端值补充。

## 用法

- 手动: 直接喂给播放器/`playtest --file-h264=<路径>` 单独验证。
- 回归: `play_regress.py`/用例表暂未纳入本目录, 需要时在 PlayMatrix.hpp 加用例或
  `push_streams.py --all` 推流 (它只扫 assets/video 根目录, 不含本目录)。
