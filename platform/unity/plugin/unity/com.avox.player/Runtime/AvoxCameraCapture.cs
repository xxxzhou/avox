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
    /// IssuePluginCustomTextureUpdateV2 上传到 VideoTexture (BGRA32)。
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

        public AvoxSourceStateEvent onStateChanged;

        public Texture2D VideoTexture => _texture;
        public AvoxPlayerState State => (AvoxPlayerState)AvoxNative.avoxSourceGetState(_source);
        public bool IsPlaying => State == AvoxPlayerState.Playing;

        IntPtr _source = IntPtr.Zero;
        Texture2D _texture;
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
            if (_texture == null || w != _texW || h != _texH)
            {
                DestroyTexture();
                _texture = new Texture2D(w, h, TextureFormat.BGRA32, false, false);
                _texture.wrapMode = TextureWrapMode.Clamp;
                _texW = w;
                _texH = h;
                BindToRenderer();
            }
            // CPU 帧槽 → 渲染线程回调上传 (userData = source id, 与 AvoxPlayer 共用回调)
            _command.IssuePluginCustomTextureUpdateV2(
                AvoxNative.avoxGetTextureUpdateCallback(), _texture, AvoxNative.avoxSourceGetId(_source));
            Graphics.ExecuteCommandBuffer(_command);
            _command.Clear();
        }

        void OnDestroy()
        {
            DestroyTexture();
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
            if (targetRenderer == null || _texture == null) return;
            _mpb.Clear();
            _mpb.SetTexture("_MainTex", _texture);
            _mpb.SetTexture("_BaseMap", _texture);
            targetRenderer.SetPropertyBlock(_mpb);
        }

        void DestroyTexture()
        {
            if (_texture != null)
            {
                Destroy(_texture);
                _texture = null;
            }
            _texW = 0;
            _texH = 0;
        }
    }
}
