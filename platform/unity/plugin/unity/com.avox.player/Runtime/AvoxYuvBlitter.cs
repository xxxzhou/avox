using System;
using UnityEngine;

namespace Avox
{
    /// <summary>
    /// CPU 回退路径的 YUV→RGB 转换器 (AvoxPlayer / AvoxCameraCapture / RTC 共用)。
    ///
    /// 原生侧把整帧打包成紧凑 NV12, 这里当成一张 R8 的 w × h*3/2 纹理经
    /// IssuePluginCustomTextureUpdateV2 上传 (上传量 1.5 字节/像素, 而非 RGBA 的 4),
    /// 再用 shader Blit 到 RenderTexture, 对外仍是一张普通 Texture。
    /// 逐像素色转全在 GPU, 主线程/渲染线程都不做乘加。
    ///
    /// sRGB 约定 (与原先直传 RGBA32 sRGB 纹理的行为一致):
    ///   YuvTexture 建成 linear (Y/UV 字节不能被 sRGB 解码);
    ///   Output 建成 sRGB, Blit 时关 GL.sRGBWrite 让 shader 结果原样落盘,
    ///   消费端采样时再由硬件 sRGB→linear。
    /// </summary>
    internal class AvoxYuvBlitter : IDisposable
    {
        // 上传目标: R8, w × h*3/2, 整帧 NV12 (交给 IssuePluginCustomTextureUpdateV2)
        public Texture2D YuvTexture => _yuvTex;
        // 对外输出 (RGB)
        public RenderTexture Output => _rt;
        public int Width => _w;
        public int Height => _h;
        // 画面上下颠倒时置 true (某些图形后端的行序约定差异)
        public bool FlipY { get => _flipY; set { if (_flipY != value) { _flipY = value; ApplySize(); } } }

        Texture2D _yuvTex;
        RenderTexture _rt;
        Material _mat;
        int _w;
        int _h;
        bool _flipY;
        int _csCode = int.MinValue;

        public bool IsReady => _yuvTex != null && _rt != null && _mat != null;

        /// <summary>按视频尺寸建/重建资源; 返回 true 表示本次发生了重建。</summary>
        public bool Ensure(int w, int h)
        {
            if (w <= 0 || h <= 0) return false;
            if (IsReady && w == _w && h == _h) return false;
            Dispose();
            if (_mat == null)
            {
                var shader = Resources.Load<Shader>("AvoxYuvToRgb") ?? Shader.Find("Hidden/Avox/YuvToRgb");
                if (shader == null)
                {
                    Debug.LogError("[Avox] 找不到 YUV shader (Resources/AvoxYuvToRgb), CPU 回退无法出画面");
                    return false;
                }
                _mat = new Material(shader) { hideFlags = HideFlags.HideAndDontSave };
            }
            // NV12 要求偶数高; 原生侧已保证, 这里向下取偶避免尺寸不符导致回调补黑
            _w = w & ~1;
            _h = h & ~1;
            int texH = _h + _h / 2;
            // linear:true —— Y/UV 是数据不是颜色, 不能走 sRGB 解码
            _yuvTex = new Texture2D(_w, texH, TextureFormat.R8, false, true)
            {
                filterMode = FilterMode.Point,
                wrapMode = TextureWrapMode.Clamp,
                hideFlags = HideFlags.HideAndDontSave
            };
            _rt = new RenderTexture(_w, _h, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB)
            {
                filterMode = FilterMode.Bilinear,
                wrapMode = TextureWrapMode.Clamp,
                hideFlags = HideFlags.HideAndDontSave
            };
            _rt.Create();
            ApplySize();
            // 尺寸变了要重推矩阵 (Material 是新的)
            int cs = _csCode;
            _csCode = int.MinValue;
            SetColorSpace(cs == int.MinValue ? -1 : cs);
            return true;
        }

        void ApplySize()
        {
            if (_mat == null || _yuvTex == null) return;
            _mat.SetTexture("_MainTex", _yuvTex);
            _mat.SetVector("_AvoxTexSize",
                new Vector4(_yuvTex.width, _yuvTex.height, _h, _flipY ? 1f : 0f));
        }

        /// <summary>色彩空间编码 (standard | range&lt;&lt;8), -1 = 未知则用 BT.601 full。</summary>
        public void SetColorSpace(int code)
        {
            if (_mat == null || code == _csCode) return;
            _csCode = code;
            int standard = code < 0 ? 0 : (code & 0xFF);
            bool limited = code >= 0 && ((code >> 8) & 0xFF) == 1;
            // 与原生 CPU 版 pickMatrix 同参 (Q10 定点 / 1024)
            Vector4 coef;
            switch (standard)
            {
                case 1: coef = new Vector4(1613f, 192f, 479f, 1900f); break;   // bt709
                case 2: coef = new Vector4(1510f, 168f, 585f, 1926f); break;   // bt2020
                default: coef = new Vector4(1436f, 352f, 731f, 1815f); break;  // bt601
            }
            coef /= 1024f;
            // limited(MPEG 16~235/240) → full: y=(y-16/255)*1.164062, c=(c-128/255)*1.138672
            var range = limited ? new Vector4(16f / 255f, 1.164062f, 1.138672f, 0f)
                                : new Vector4(0f, 1f, 1f, 0f);
            _mat.SetVector("_AvoxCoef", coef);
            _mat.SetVector("_AvoxRange", range);
        }

        /// <summary>把已上传的 YUV 纹理转换到 Output (每帧调一次, 在上传命令之后)。</summary>
        public void Blit()
        {
            if (!IsReady) return;
            // shader 输出的是 sRGB 编码值本身, 不能再被硬件二次编码
            bool prev = GL.sRGBWrite;
            GL.sRGBWrite = false;
            Graphics.Blit(_yuvTex, _rt, _mat);
            GL.sRGBWrite = prev;
        }

        public void Dispose()
        {
            if (_rt != null)
            {
                _rt.Release();
                UnityEngine.Object.Destroy(_rt);
                _rt = null;
            }
            if (_yuvTex != null)
            {
                UnityEngine.Object.Destroy(_yuvTex);
                _yuvTex = null;
            }
            _w = 0;
            _h = 0;
        }

        /// <summary>连 Material 一起释放 (组件 OnDestroy 用)。</summary>
        public void DisposeAll()
        {
            Dispose();
            if (_mat != null)
            {
                UnityEngine.Object.Destroy(_mat);
                _mat = null;
            }
            _csCode = int.MinValue;
        }
    }
}
