using System;
using System.Text;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.Rendering;

namespace Avox
{
    [Serializable] public class AvoxSourceStateEvent : UnityEvent<AvoxPlayerState> { }

    /// <summary>
    /// 设备源采集 (摄像头/采集卡) —— 对应 AvoxPlayer 的设备版:
    /// 枚举 win_mf 摄像头 → ISourcePlayer 打开 → CPU 帧槽经
    /// IssuePluginCustomTextureUpdateV2 上传整帧 NV12 到 R8 纹理,
    /// 再由 shader Blit 成 RGB (VideoTexture 是转换后的 RenderTexture)。
    /// 音频由 avox 内置 WASAPI 直出麦克风。
    /// </summary>
    [AddComponentMenu("Avox/Avox Camera Capture")]
    [DisallowMultipleComponent]
    public class AvoxCameraCapture : MonoBehaviour
    {
        [Tooltip("设备索引 (AvailDevices 数组下标), -1 = 第一个")]
        public int deviceIndex = -1;
        [Tooltip("Start 时自动打开")]
        public bool autoOpen = true;
        [Tooltip("视频纹理自动绑到该 Renderer 的 _MainTex/_BaseMap (空则取自身 Renderer)")]
        public Renderer targetRenderer;
        [Tooltip("画面上下颠倒时勾上")]
        public bool flipY = false;

        public AvoxSourceStateEvent onStateChanged;

        // YUV shader 转换后的 RenderTexture
        public Texture VideoTexture => _yuv.Output;
        public AvoxPlayerState State => (AvoxPlayerState)AvoxNative.avoxSourceGetState(_source);
        public bool IsPlaying => State == AvoxPlayerState.Playing;

        IntPtr _source = IntPtr.Zero;
        readonly AvoxYuvBlitter _yuv = new AvoxYuvBlitter();
        CommandBuffer _command;
        MaterialPropertyBlock _mpb;
        AvoxPlayerState _lastState = AvoxPlayerState.None;
        int _texW;
        int _texH;

        /// <summary>摄像头设备名列表 (每次调用刷新)</summary>
        public static string[] AvailDevices()
        {
            int n = AvoxNative.avoxVideoDeviceCount();
            var list = new string[Mathf.Max(n, 0)];
            var buf = new byte[512];
            for (int i = 0; i < n; i++)
            {
                int len = AvoxNative.avoxVideoDeviceName(i, buf, buf.Length);
                list[i] = len > 0 ? Encoding.UTF8.GetString(buf, 0, Math.Min(len - 1, buf.Length)) : $"设备 {i}";
            }
            return list;
        }

        void Start()
        {
            _command = new CommandBuffer();
            _mpb = new MaterialPropertyBlock();
            if (targetRenderer == null) targetRenderer = GetComponent<Renderer>();
            if (autoOpen) Open(deviceIndex);
        }

        void Update()
        {
            if (_source == IntPtr.Zero) return;
            var st = State;
            if (st != _lastState)
            {
                _lastState = st;
                onStateChanged?.Invoke(st);
            }
            if (AvoxNative.avoxSourceGetFrameInfo(_source, out int w, out int h) == 0) return;
            _yuv.FlipY = flipY;
            _yuv.SetColorSpace(AvoxNative.avoxSourceGetColorSpace(_source));
            if (_yuv.Ensure(w, h))
            {
                _texW = _yuv.Width;
                _texH = _yuv.Height;
                BindToRenderer();
            }
            if (!_yuv.IsReady) return;
            // CPU 帧槽 → 渲染线程回调上传整帧 NV12 到 R8 纹理
            // (userData = source id, 与 AvoxPlayer 共用回调), 再 shader 转 RGB
            _command.IssuePluginCustomTextureUpdateV2(
                AvoxNative.avoxGetTextureUpdateCallback(), _yuv.YuvTexture, AvoxNative.avoxSourceGetId(_source));
            Graphics.ExecuteCommandBuffer(_command);
            _command.Clear();
            _yuv.Blit();
        }

        void OnDestroy()
        {
            _yuv.DisposeAll();
            if (_command != null)
            {
                _command.Dispose();
                _command = null;
            }
            if (_source != IntPtr.Zero)
            {
                AvoxNative.avoxSourceDestroy(_source);
                _source = IntPtr.Zero;
            }
        }

        // ── 控制 ──

        public bool Open(int index)
        {
            deviceIndex = index;
            if (_source == IntPtr.Zero) _source = AvoxNative.avoxSourceCreate();
            AvoxNative.avoxSourceSetDevice(_source, index);
            return AvoxNative.avoxSourceOpen(_source) != 0;
        }

        public void Close()
        {
            DestroyTexture();
            if (_source != IntPtr.Zero) AvoxNative.avoxSourceClose(_source);
        }

        // ── 内部 ──

        void BindToRenderer()
        {
            if (targetRenderer == null || _yuv.Output == null) return;
            _mpb.Clear();
            _mpb.SetTexture("_MainTex", _yuv.Output);
            _mpb.SetTexture("_BaseMap", _yuv.Output);
            targetRenderer.SetPropertyBlock(_mpb);
        }

        void DestroyTexture()
        {
            _yuv.Dispose();
            _texW = 0;
            _texH = 0;
        }
    }
}
