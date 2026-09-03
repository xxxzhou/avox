"""
导出 Real-ESRGAN general-x4v3 为 ONNX (动态输入分辨率)
复刻 realesrgan.archs.srvgg_arch.SRVGGNetCompact (nearest 上采样残差)

用法:
  python script/realesrgan/export_onnx.py                       # FP32 (默认)
  python script/realesrgan/export_onnx.py --quantize int8        # INT8 静态量化 (QDQ)
  python script/realesrgan/export_onnx.py --denoise-strength 0.5

输出:
  weights:   script/realesrgan/realesr-general-x4v3.pth (自动下载)
  FP32:      assets/models/quality/realesrgan-general-x4v3.onnx
  INT8:      assets/models/quality/realesrgan-general-x4v3_int8.onnx (--quantize int8)

INT8 量化说明:
  静态量化 (QDQ 格式), 仅量化 Conv (per-channel), PReLU 保留 FP32。
  Conv 是 SRVGGNetCompact 的算力大头 → 拿到 Intel CPU VNNI 加速;
  PReLU 保留 FP32 → 避免参数化激活量化掉精度。
  Intel CPU (AVX-512 VNNI / AVX2 VNNI) 上 FP32→INT8 常见 2-4x 加速。
"""
import torch
import torch.nn as nn
import torch.nn.functional as F
import argparse
import os
import urllib.request


# ── 模型定义 (复刻官方 srvgg_arch.SRVGGNetCompact) ──

class SRVGGNetCompact(nn.Module):
    def __init__(self, num_in_ch=3, num_out_ch=3, num_feat=64, num_conv=32,
                 upscale=4, act_type='prelu'):
        super().__init__()
        self.upscale = upscale
        self.body = nn.ModuleList()
        self.body.append(nn.Conv2d(num_in_ch, num_feat, 3, 1, 1))
        self.body.append(nn.PReLU(num_parameters=num_feat))
        for _ in range(num_conv):
            self.body.append(nn.Conv2d(num_feat, num_feat, 3, 1, 1))
            self.body.append(nn.PReLU(num_parameters=num_feat))
        self.body.append(nn.Conv2d(num_feat, num_out_ch * upscale * upscale, 3, 1, 1))
        self.upsampler = nn.PixelShuffle(upscale)

    def forward(self, x):
        out = x
        for layer in self.body:
            out = layer(out)
        out = self.upsampler(out)
        base = F.interpolate(x, scale_factor=self.upscale, mode='nearest')
        out += base
        return out


# ── 下载权重 ──

WEIGHTS_DIR = os.path.join(os.path.dirname(__file__), "weights")
X4V3_URL = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth"
WDN_URL = "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-wdn-x4v3.pth"
ONNX_OUTPUT = os.path.join(os.path.dirname(__file__), "..", "..", "assets", "models", "quality", "realesrgan-general-x4v3.onnx")
INT8_OUTPUT = ONNX_OUTPUT.replace(".onnx", "_int8.onnx")


def download(url, path):
    if os.path.exists(path):
        print(f"Already exists: {path}")
        return
    print(f"Downloading: {url}")
    urllib.request.urlretrieve(url, path)
    print(f"Saved: {path} ({os.path.getsize(path) / 1024 / 1024:.1f} MB)")


def load_weights(pth_path, denoise_strength=1.0):
    state = torch.load(pth_path, map_location=torch.device("cpu"))
    if denoise_strength != 1.0:
        wdn_path = os.path.join(WEIGHTS_DIR, "realesr-general-wdn-x4v3.pth")
        download(WDN_URL, wdn_path)
        state_wdn = torch.load(wdn_path, map_location=torch.device("cpu"))
        key = "params_ema" if "params_ema" in state else "params"
        s1, s2 = state[key], state_wdn[key]
        interp = {k: denoise_strength * s1[k] + (1 - denoise_strength) * s2[k] for k in s1}
        print(f"DNI interpolation: strength={denoise_strength}")
        return interp
    key = "params_ema" if "params_ema" in state else "params"
    if key in state:
        return state[key]
    return state


def export_fp32(denoise_strength=1.0, opset=14):
    # 下载权重
    os.makedirs(WEIGHTS_DIR, exist_ok=True)
    pth_path = os.path.join(WEIGHTS_DIR, "realesr-general-x4v3.pth")
    download(X4V3_URL, pth_path)

    # 构建模型
    model = SRVGGNetCompact(num_in_ch=3, num_out_ch=3, num_feat=64,
                            num_conv=32, upscale=4, act_type='prelu')
    model.load_state_dict(load_weights(pth_path, denoise_strength))
    model.cpu().eval()

    # 验证
    with torch.no_grad():
        t = model(torch.full((1, 3, 64, 64), 0.5))
        print(f"PyTorch test: input=[0.5] output=[{t.min():.3f}, {t.max():.3f}] mean={t.mean():.3f}")

    # 导出 ONNX (动态 H/W)
    onnx_path = os.path.normpath(ONNX_OUTPUT)
    os.makedirs(os.path.dirname(onnx_path), exist_ok=True)
    torch.onnx.export(
        model, torch.rand(1, 3, 64, 64), onnx_path,
        input_names=["image"], output_names=["output"],
        opset_version=opset,
        dynamic_axes={"image": {0: "batch", 2: "height", 3: "width"},
                      "output": {0: "batch", 2: "height", 3: "width"}},
    )
    print(f"Exported FP32: {onnx_path} ({os.path.getsize(onnx_path) / 1024 / 1024:.1f} MB)")
    return onnx_path


# ── INT8 静态量化 ──

def quantize_int8(fp32_path):
    """FP32 → INT8 静态量化 (QDQ 格式)。
    仅量化 Conv (per-channel), PReLU 保留 FP32: Conv 吃 VNNI 加速, PReLU 保精度。
    校准数据用 [0,1] 范围合成图 (对齐 preprocess shader: RGBA→NCHW 无归一化, 纹理值天然 [0,1])。"""
    import numpy as np
    from onnxruntime.quantization import (
        quantize_static, CalibrationDataReader,
        QuantType, QuantFormat, CalibrationMethod,
    )

    # 推理分辨率 640×360 (Upscale2x 1280×720 → 输出 2560×1440 → 推理 640×360)
    calib_w, calib_h = 640, 360
    count = 16

    def _make_sample(dark):
        # [0,1] 均匀基础, 监控帧分布: 白天偏亮均匀 / 夜视偏暗 + 高斯噪声
        base = np.random.rand(1, 3, calib_h, calib_w).astype(np.float32)
        if dark:
            base = base * 0.40 + 0.02
        else:
            base = base * 0.85 + 0.05
        base += np.random.normal(0, 0.02, base.shape).astype(np.float32)  # 监控噪声
        return np.clip(base, 0.0, 1.0)

    np.random.seed(0)  # 可复现
    samples = [{"image": _make_sample(dark=(i % 2 == 1))} for i in range(count)]

    class _Reader(CalibrationDataReader):
        def __init__(self, data):
            self._iter = iter(data)
        def get_next(self):
            return next(self._iter, None)

    int8_path = os.path.normpath(INT8_OUTPUT)
    os.makedirs(os.path.dirname(int8_path), exist_ok=True)
    print(f"Quantizing INT8 (QDQ, conv-only per-channel, MinMax, {count} samples {calib_w}x{calib_h})...")
    quantize_static(
        model_input=fp32_path,
        model_output=int8_path,
        calibration_data_reader=_Reader(samples),
        quant_format=QuantFormat.QDQ,
        activation_type=QuantType.QUInt8,
        weight_type=QuantType.QInt8,
        calibrate_method=CalibrationMethod.MinMax,
        per_channel=True,
        op_types_to_quantize=["Conv"],
    )
    print(f"Exported INT8: {int8_path} ({os.path.getsize(int8_path) / 1024 / 1024:.1f} MB)")
    return int8_path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Export Real-ESRGAN general-x4v3 to ONNX")
    parser.add_argument("--denoise-strength", type=float, default=1.0,
                        help="DNI strength (1.0=x4v3, 0.0=wdn, 中间=插值)")
    parser.add_argument("--opset", type=int, default=14)
    parser.add_argument("--quantize", choices=["none", "int8"], default="none",
                        help="none=FP32(默认), int8=静态量化(QDQ, conv-only)")
    args = parser.parse_args()

    fp32_path = export_fp32(args.denoise_strength, args.opset)
    if args.quantize == "int8":
        quantize_int8(fp32_path)
