#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""idle 待机动作离线烘焙 (M4): VAR(p) 拟合躯干微动 → 生成循环曲线 → 导出 JSON 供 idle_motion.gd 采样。

参考 AIRI motion-driver-magic (VAR order 20) 的思路, 但在离线端完成 (在线零算法依赖)。

数据源:
  --bvh <file>   真实 mocap (BVH): 提取 Spine/Spine1 关节本地欧拉角通道 (度→rad, 重采样 30fps)
  (默认)         合成训练集: OU 慢漂移 + 共模晃动 + 呼吸正弦 (量级按真人站立 idle 标定)

输出 JSON (默认 platform/godot/tools/src/avatar/idle_bake.json):
  {fps, duration, crossfade, channels: [{role: spine|mid, dof: 0|1|2, data: [millirad int]}]}
  runtime 约定: role spine=Spine/spine 骨, mid=Spine1/chest 骨; 循环末尾 crossfade 秒与开头混合。

用法:
  python script/godot/idle_bake.py                    # 合成训练集 → 烘焙
  python script/godot/idle_bake.py --bvh idle.bvh     # 真实 mocap → 烘焙
"""
import argparse
import json
import math
import os

import numpy as np

FPS = 30
ORDER = 24            # VAR 阶数 (AIRI MAGIC VAR order 20 同量级)
DURATION = 45.0       # 生成时长 (s); runtime 循环播放, 末尾 crossfade 与开头混合
ROLES = ["spine", "mid"]
LIMITS = {"spine": 0.035, "mid": 0.025}   # 每通道幅度上限 (rad, ~2°/1.4°): 超出按通道归一
BREATH = {"role": "mid", "dof": 0, "period_s": 3.6, "amp_rad": 0.012}  # 呼吸 ~0.7° 胸骨 pitch


def synth_train(seconds=180, seed=7):
    """合成训练集: 6 通道 (role-major: spine_xyz, mid_xyz), OU 慢漂移 + 共模晃动 + 呼吸。"""
    rng = np.random.default_rng(seed)
    n = int(seconds * FPS)
    t = np.arange(n) / FPS
    # OU 离散: x_t = x_{t-1}(1 - theta*dt) + sigma*sqrt(dt)*w
    theta, sigma = 0.4, 0.006
    x = np.zeros((n, len(ROLES) * 3))
    w = rng.normal(0, sigma * math.sqrt(1.0 / FPS), (n, x.shape[1]))
    x[0] = rng.normal(0, 0.005, x.shape[1])
    decay = 1.0 - theta / FPS
    for k in range(1, n):
        x[k] = x[k - 1] * decay + w[k]
    # 共模慢晃动 (重心转移, 各轴按 0.5~1 相关)
    common = (0.012 * np.sin(2 * math.pi * 0.11 * t + rng.uniform(0, 6.28))
              + 0.008 * np.sin(2 * math.pi * 0.23 * t + rng.uniform(0, 6.28)))
    for c in range(x.shape[1]):
        x[:, c] += common * (1.0 if c % 3 == 1 else 0.5)
    # 呼吸 (mid pitch) — 让 VAR 学到周期成分
    breath = BREATH["amp_rad"] * np.sin(2 * math.pi * t / BREATH["period_s"])
    x[:, len(ROLES) * 0 + BREATH["dof"] + 3] += breath  # mid 是第 2 个 role → 偏移 +3
    return x


def load_bvh(path, joints=("Spine", "Spine1")):
    """BVH → (n, 6) 训练集: 指定关节的本地欧拉角 (度→rad, 重采样到 FPS)。列序按 MOTION 声明。"""
    with open(path, encoding="utf-8", errors="ignore") as f:
        text = f.read()
    lines = [s.strip() for s in text.splitlines()]
    order, nchans, i = [], [], 0
    while i < len(lines):
        s = lines[i]
        if s.startswith(("ROOT", "JOINT")):
            name = s.split()[1]
            # 向下找 CHANNELS 行数与通道数
            while not lines[i].startswith("CHANNELS"):
                i += 1
            parts = lines[i].split()
            cnt = int(parts[1])
            order.append(name)
            nchans.append(cnt)
        if s.startswith("MOTION"):
            break
        i += 1
    frame_time = 1.0 / 30
    rows = []
    for s in lines[i:]:
        parts = s.split()
        if len(parts) >= 6:
            try:
                rows.append([float(v) for v in parts])
            except ValueError:
                continue
    motion = np.asarray(rows)
    # 列偏移: 逐关节切片
    cols, off = {}, 0
    for name, cnt in zip(order, nchans):
        cols[name] = (off, cnt)
        off += cnt
    chans = []
    for jn in joints:
        o, cnt = cols[jn]
        rot = motion[:, o + cnt - 3: o + cnt]           # 每关节末 3 列 = 旋转欧拉 (度)
        chans.append(np.deg2rad(rot))
    x = np.hstack(chans)
    # 重采样到 FPS
    src_fps = 1.0 / frame_time
    if abs(src_fps - FPS) > 0.5:
        tsrc = np.arange(motion.shape[0]) / src_fps
        tdst = np.arange(0, tsrc[-1], 1.0 / FPS)
        x = np.stack([np.interp(tdst, tsrc, x[:, c]) for c in range(x.shape[1])], axis=1)
    # 中心化 (VAR 拟合的是波动, 静态姿态由 rest 承担)
    return x - x.mean(axis=0, keepdims=True)


def fit_var(x, order=ORDER):
    """多通道 VAR(p) 最小二乘: x_t = c + Σ A_k x_{t-k} + e → (B, resid_std)。"""
    n, d = x.shape
    y = x[order:]
    terms = [np.ones((n - order, 1))]
    for k in range(1, order + 1):
        terms.append(x[order - k: n - k])
    design = np.hstack(terms)
    coef, *_ = np.linalg.lstsq(design, y, rcond=None)
    resid = y - design @ coef
    return coef, resid.std(axis=0)


def stabilize(coef, max_rho=0.97):
    """VAR 稳定化: 伴随矩阵谱半径 > max_rho 时整体缩放 AR 系数 (近单位根过程 + 高阶过拟合
    常致谱半径 >1 → 生成发散; 缩放保波形结构, 只压增长率)。"""
    p = coef.shape[0] - 1
    d = coef.shape[1]
    comp = np.zeros((p * d, p * d))
    comp[:d] = np.hstack([coef[k] for k in range(1, p + 1)])
    if p > 1:
        comp[d:, :-d] = np.eye(p * d - d)
    rho = float(np.max(np.abs(np.linalg.eigvals(comp))))
    if rho > max_rho:
        coef[1:] *= max_rho / rho
    return coef, rho


def generate(coef, resid_std, seconds, seed):
    """VAR 采样生成 seconds 秒; 从零起步 (rest 姿态淡入), 幅度超限按通道归一。"""
    rng = np.random.default_rng(seed)
    p = coef.shape[0] - 1
    d = coef.shape[1]
    n = int(seconds * FPS)
    out = np.zeros((n, d))
    hist = np.zeros((p, d))
    noise_scale = 0.9
    for t in range(n):
        v = coef[0].copy()
        for k in range(1, p + 1):
            v += coef[k] @ hist[p - k]
        v += rng.normal(0, resid_std) * noise_scale
        out[t] = v
        hist[: -1] = hist[1:]
        hist[-1] = v
    # 幅度保护: 通道峰值超限 → 整通道缩放 (保波形, 不削顶)
    for c in range(d):
        role = ROLES[c // 3]
        peak = np.abs(out[:, c]).max()
        if peak > LIMITS[role]:
            out[:, c] *= LIMITS[role] / peak
    return out


def export(x, path):
    channels = []
    for ci in range(x.shape[1]):
        role = ROLES[ci // 3]
        dof = ci % 3
        data = (np.clip(x[:, ci], -LIMITS[role], LIMITS[role]) * 1000).round().astype(int)
        channels.append({"role": role, "dof": dof, "data": data.tolist()})
    doc = {"fps": FPS, "duration": DURATION, "crossfade": 1.5, "channels": channels}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, separators=(",", ":"))
    peak = np.degrees(np.abs(x).max(axis=0))
    print(f"[idle_bake] {path} {os.path.getsize(path) // 1024}KB "
          f"channels={x.shape[1]} samples={x.shape[0]} peak_deg={peak.round(2).tolist()}")


def main():
    here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--bvh", help="真实 mocap BVH (提取 Spine/Spine1 本地欧拉角)")
    ap.add_argument("--out", default=os.path.join(
        here, "platform", "godot", "tools", "src", "avatar", "idle_bake.json"))
    ap.add_argument("--seed", type=int, default=11)
    args = ap.parse_args()
    if args.bvh:
        train = load_bvh(args.bvh)
        print(f"[idle_bake] BVH 训练集 {train.shape}")
    else:
        train = synth_train(seed=args.seed)
        print(f"[idle_bake] 合成训练集 {train.shape}")
    coef, resid = fit_var(train)
    coef, rho = stabilize(coef)
    print(f"[idle_bake] VAR({ORDER}) 拟合 resid_std={resid.round(4).tolist()} 谱半径={rho:.3f}→已稳定化")
    x = generate(coef, resid, DURATION, args.seed)
    export(x, args.out)


if __name__ == "__main__":
    main()
