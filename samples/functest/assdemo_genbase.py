# assdemo 背景视频帧生成: 滚动渐变 + 弹跳方块 + 时间进度条
# 用法: python assdemo_genbase.py <out.raw> [frames=300] [w=1280] [h=720]
# 输出 rawvideo(rgb24, 30fps) 供 assdemo.exe 合成字幕后再编码
import sys
import numpy as np

W, H, FPS = 1280, 720, 30
frames = int(sys.argv[2]) if len(sys.argv) > 2 else 300
if len(sys.argv) > 4:
    W, H = int(sys.argv[3]), int(sys.argv[4])

yy, xx = np.mgrid[0:H, 0:W]
xs = xx.astype(np.int32)
ys = ys_ = yy.astype(np.int32)
out = np.empty((H, W, 3), np.uint8)

with open(sys.argv[1], "wb") as f:
    for i in range(frames):
        t = i / FPS
        # 滚动渐变底(蓝紫青), 随时间平移
        phase = (t * 60) % W
        gx = ((xs + phase) % W) / W
        out[..., 0] = (40 + 60 * gx).astype(np.uint8)          # R
        out[..., 1] = (30 + 40 * np.abs(gx - 0.5) * 2).astype(np.uint8)  # G
        out[..., 2] = (90 + 120 * (1 - gx)).astype(np.uint8)   # B
        # 弹跳方块
        bx = int(100 + (W - 240) * abs(((t * 0.7) % 2) - 1))
        by = int(120 + (H - 360) * abs(((t * 1.1) % 2) - 1))
        out[by:by + 120, bx:bx + 120] = (240, 200, 60)
        # 顶部时间进度条 + 帧刻度
        out[0:8, :] = (20, 20, 20)
        w_bar = int(W * i / frames)
        out[0:8, :w_bar] = (80, 220, 120)
        for k in range(0, frames, 30):  # 每秒一个刻度块
            out[8:20, int(W * k / frames):int(W * k / frames) + 4] = (220, 220, 220)
        f.write(out.tobytes())
print(f"base.raw: {frames} 帧 {W}x{H} @ {FPS}fps")
