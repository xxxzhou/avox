using System;
using System.Text;
using UnityEngine;
using UnityEngine.Events;
using UnityEngine.Rendering;

namespace Avox
{
    public enum AvoxPlayerState
    {
        None = 0, Opening = 1, Ready = 2, Playing = 3, Pause = 4,
        Seek = 5, Buffering = 6, Stopped = 7, Completed = 8
    }

    public enum AvoxIoPlan
    {
        None = 0, Zlmediakit = 1, Ffmpeg = 2, Torrent = 3
    }

    [Serializable] public class AvoxStateEvent : UnityEvent<AvoxPlayerState> { }
    [Serializable] public class AvoxErrorEvent : UnityEvent<int, string> { }
    [Serializable] public class AvoxTextureEvent : UnityEvent<Texture2D> { }

    /// <summary>
    /// avox 媒体播放器 (对应 godot 插件 MediaPlayer / UE 插件 AvoxMediaPlayerComponent)
    /// 播放 rtmp/rtsp/http/本地文件/torrent 等地址; 视频帧输出到 VideoTexture
    /// GPU 直通: Unity Vulkan 后端零拷贝 (avox 共享纹理导入)
    /// CPU 回退: 其他图形后端, 渲染线程经 IssuePluginCustomTextureUpdateV2 上传
    /// 音频由 avox 内置渲染直出 (Windows WASAPI), 不经 Unity AudioMixer
    /// </summary>
    [AddComponentMenu("Avox/Avox Media Player")]
    [DisallowMultipleComponent]
    public class AvoxPlayer : MonoBehaviour
    {
        [Tooltip("播放地址: rtmp/rtsp/http/本地路径/torrent 等")]
        public string url = "";
        public bool hardDecode = true;
        [Range(0f, 1f)] public float volume = 1f;
        [Tooltip("Start 时自动 Open")]
        public bool autoPlay = false;
        public AvoxIoPlan ioPlan = AvoxIoPlan.None;
        [Tooltip("视频纹理自动绑到该 Renderer 的 _MainTex/_BaseMap (空则取自身 Renderer)")]
        public Renderer targetRenderer;

        public AvoxStateEvent onStateChanged;
        public UnityEvent onReady;
        public UnityEvent onComplete;
        public AvoxErrorEvent onError;
        [Tooltip("视频纹理创建/重建时触发 (首帧到达或分辨率变化)")]
        public AvoxTextureEvent onTextureCreated;

        public Texture2D VideoTexture => _texture;
        public AvoxPlayerState State => (AvoxPlayerState)AvoxNative.avoxPlayerGetState(_player);
        public bool IsPlaying => State == AvoxPlayerState.Playing;
        public long DurationMs => AvoxNative.avoxPlayerGetDuration(_player);
        public long PositionMs => AvoxNative.avoxPlayerGetPosition(_player);
        public double Progress => AvoxNative.avoxPlayerGetProgress(_player);
        // 是否已走 GPU 直通 (Unity Vulkan 后端); 否则为 CPU 回退
        public bool GpuPassthrough => AvoxNative.avoxPlayerIsGpuMode(_player) != 0;
        // 0 无 1 Vulkan 导入 (外部纹理) 2 D3D11 拷贝 (普通纹理+渲染事件)
        public int GpuFlavor => AvoxNative.avoxGetGpuFlavor();
        // 原生播放器实例 id (渲染事件 eventId)
        public int GetId() => _player != IntPtr.Zero ? (int)AvoxNative.avoxPlayerGetId(_player) : 0;
        // D3D11 拷贝链路诊断: 渲染事件数/实际拷贝数/目标缺失数/最近 fence 值/打开次数
        public string GetDx11Debug()
        {
            if (_player == IntPtr.Zero) return "player=null";
            AvoxNative.avoxPlayerGetDx11Debug(_player, out int ev, out int cp, out int tn, out long fv, out int op);
            return $"events={ev} copies={cp} targetNull={tn} fence={fv} opens={op}";
        }

        IntPtr _player = IntPtr.Zero;
        Texture2D _texture;
        IntPtr _wrappedNative = IntPtr.Zero;  // 外部纹理包裹的原生指针 (重绑检测)
        CommandBuffer _command;
        MaterialPropertyBlock _mpb;
        int _texW;
        int _texH;

        void Start()
        {
            _command = new CommandBuffer();
            _mpb = new MaterialPropertyBlock();
            _player = AvoxNative.avoxPlayerCreate();
            if (targetRenderer == null) targetRenderer = GetComponent<Renderer>();
            ApplyConfig();
            if (autoPlay && !string.IsNullOrEmpty(url)) Open(url);
        }

        void Update()
        {
            if (_player == IntPtr.Zero) return;
            PollEvents();
            // GPU 直通: 主线程处理 enableVkOutput + NT句柄导入 (非Vulkan后端快速返回)
            AvoxNative.avoxPlayerUpdateGpu(_player);
            EnsureTexture();
            if (!GpuPassthrough && _texture != null)
            {
                // CPU 回退: 渲染线程经回调取帧, GPU 上传由 Unity 完成
                _command.IssuePluginCustomTextureUpdateV2(
                    AvoxNative.avoxGetTextureUpdateCallback(), _texture, AvoxNative.avoxPlayerGetId(_player));
                Graphics.ExecuteCommandBuffer(_command);
                _command.Clear();
            }
            else if (GpuPassthrough && GpuFlavor == 2)
            {
                // D3D11 拷贝模式: 渲染线程把 avox 共享纹理 CopyResource 到插件自建
                // 目标纹理 (fence 去重)。纹理未就绪也要发: 首次事件负责打开共享纹理、
                // 建目标纹理并回填尺寸, C# 据此 CreateExternalTexture
                GL.IssuePluginEvent(AvoxNative.avoxGetRenderEventFunc(),
                                    (int)AvoxNative.avoxPlayerGetId(_player));
            }
        }

        void OnDestroy()
        {
            // 先销毁引用 GPU 导入资源的纹理, 再释放原生播放器
            DestroyTexture();
            if (_command != null)
            {
                _command.Dispose();
                _command = null;
            }
            if (_player != IntPtr.Zero)
            {
                AvoxNative.avoxPlayerDestroy(_player);
                _player = IntPtr.Zero;
            }
        }

        // ── 控制 ──

        public void Open()
        {
            Open(url);
        }

        public void Open(string urlToOpen)
        {
            if (_player == IntPtr.Zero || string.IsNullOrEmpty(urlToOpen))
            {
                Debug.LogWarning("[AvoxPlayer] Open: player 未创建或 url 为空", this);
                return;
            }
            url = urlToOpen;
            ApplyConfig();
            AvoxNative.avoxPlayerOpen(_player, url);
        }

        public void Close() => AvoxNative.avoxPlayerClose(_player);

        public void Pause() => AvoxNative.avoxPlayerPause(_player);

        public void Resume() => AvoxNative.avoxPlayerResume(_player);

        public void Seek(long positionMs) => AvoxNative.avoxPlayerSeek(_player, positionMs);

        public void SetSpeed(double speed) => AvoxNative.avoxPlayerSetSpeed(_player, speed);

        public void SetVolume(float v)
        {
            volume = Mathf.Clamp01(v);
            AvoxNative.avoxPlayerSetVolume(_player, volume);
        }

        // ── Option (键值参数透传, 具体可用 key 见 avox 文档) ──

        public bool SetOptionBool(string key, bool value) => _player != IntPtr.Zero && AvoxNative.avoxPlayerSetOptionBool(_player, key, value ? 1 : 0) != 0;
        public bool SetOptionInt(string key, long value) => _player != IntPtr.Zero && AvoxNative.avoxPlayerSetOptionInt(_player, key, value) != 0;
        public bool SetOptionNumber(string key, double value) => _player != IntPtr.Zero && AvoxNative.avoxPlayerSetOptionNumber(_player, key, value) != 0;
        public bool SetOptionString(string key, string value) => _player != IntPtr.Zero && AvoxNative.avoxPlayerSetOptionString(_player, key, value) != 0;
        public long GetOptionInt(string key) => AvoxNative.avoxPlayerGetOptionInt(_player, key);
        public double GetOptionNumber(string key) => AvoxNative.avoxPlayerGetOptionNumber(_player, key);

        public string GetOptionString(string key)
        {
            if (_player == IntPtr.Zero) return null;
            var type = AvoxNative.avoxPlayerGetOptionType(_player, key);
            if (type != AvoxNative.ArgTypeString) return null;
            var buf = new byte[512];
            int n = AvoxNative.avoxPlayerGetOptionString(_player, key, buf, buf.Length);
            return n < 0 ? null : Encoding.UTF8.GetString(buf, 0, n);
        }

        // ── 录制 (ffmpeg 封装; 录制中再次 StartRecord 会先停旧再开新) ──

        public bool StartRecord(string path, bool transcode = false) =>
            _player != IntPtr.Zero && AvoxNative.avoxPlayerStartRecord(_player, path, transcode ? 1 : 0) != 0;

        public void StopRecord() => AvoxNative.avoxPlayerStopRecord(_player);

        // 0 none 1 opening 2 recording 3 completed
        public int RecordState => AvoxNative.avoxPlayerGetRecordState(_player);

        // ── 字幕 ──

        public bool LoadSrt(string path) => _player != IntPtr.Zero && AvoxNative.avoxPlayerLoadSrt(_player, path) != 0;

        public void CloseSubtitle() => AvoxNative.avoxPlayerCloseSubtitle(_player);

        void ApplyConfig()
        {
            AvoxNative.avoxPlayerSetHardDecode(_player, hardDecode ? 1 : 0);
            AvoxNative.avoxPlayerSetVolume(_player, volume);
            AvoxNative.avoxPlayerSetIoPlan(_player, (int)ioPlan);
        }

        // ── 内部 ──

        void PollEvents()
        {
            var e = new AvoxNative.NativeEvent();
            while (AvoxNative.avoxPlayerPollEvent(_player, ref e) != 0)
            {
                switch (e.type)
                {
                    case AvoxNative.EventTypeState:
                        onStateChanged?.Invoke((AvoxPlayerState)e.state);
                        break;
                    case AvoxNative.EventTypeReady:
                        onReady?.Invoke();
                        break;
                    case AvoxNative.EventTypeComplete:
                        onComplete?.Invoke();
                        break;
                    case AvoxNative.EventTypeError:
                        onError?.Invoke(e.code, e.Message);
                        break;
                }
            }
        }

        void EnsureTexture()
        {
            if (AvoxNative.avoxPlayerGetFrameInfo(_player, out int w, out int h) == 0) return;
            IntPtr nativeTex = IntPtr.Zero;
            if (GpuPassthrough)
            {
                nativeTex = (IntPtr)AvoxNative.avoxPlayerGetExternalTexture(_player);
                if (nativeTex == IntPtr.Zero) return;
            }
            // 首帧/分辨率变化/原生纹理重建(重绑) 时重建包裹
            if (_texture != null && w == _texW && h == _texH && nativeTex == _wrappedNative) return;
            DestroyTexture();
            if (GpuPassthrough)
            {
                // GPU 模式统一走外部纹理: VK=导入的 VkImage / D3D11=插件自建目标纹理
                // D3D11 共享纹理是 R8G8B8A8_UNORM, 必须用 RGBA32 包裹
                // (BGRA32 在 Linear 项目映射 B8G8R8A8_TYPELESS, SRV 创建失败采样全黑)
                _texture = Texture2D.CreateExternalTexture(w, h, TextureFormat.RGBA32,
                                                           false, false, nativeTex);
                _wrappedNative = nativeTex;
            }
            else
            {
                _texture = new Texture2D(w, h, TextureFormat.BGRA32, false, false);
                _texture.wrapMode = TextureWrapMode.Clamp;
            }
            _texW = w;
            _texH = h;
            BindToRenderer();
            onTextureCreated?.Invoke(_texture);
            Debug.Log($"[AvoxPlayer] 视频纹理就绪 {w}x{h} ({(GpuPassthrough ? "GPU直通" : "CPU回退")})", this);
        }

        void BindToRenderer()
        {
            if (targetRenderer == null || _texture == null) return;
            _mpb.Clear();
            // 内置管线 _MainTex / URP _BaseMap 双绑
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
            _wrappedNative = IntPtr.Zero;
            _texW = 0;
            _texH = 0;
        }
    }
}
