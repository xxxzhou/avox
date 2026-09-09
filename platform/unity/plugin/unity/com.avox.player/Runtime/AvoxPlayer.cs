using System;
using System.Collections.Generic;
using System.IO;
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
    // Texture 而非 Texture2D: CPU 回退输出的是 shader 转换后的 RenderTexture
    [Serializable] public class AvoxTextureEvent : UnityEvent<Texture> { }

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
        [Tooltip("CPU 回退画面上下颠倒时勾上 (仅影响 CPU 回退, GPU 直通不受此项影响)")]
        public bool cpuFlipY = false;

        public AvoxStateEvent onStateChanged;
        public UnityEvent onReady;
        public UnityEvent onComplete;
        public AvoxErrorEvent onError;
        [Tooltip("视频纹理创建/重建时触发 (首帧到达或分辨率变化)")]
        public AvoxTextureEvent onTextureCreated;

        // GPU 直通时是包裹原生资源的 Texture2D; CPU 回退时是 YUV shader 转换后的 RenderTexture
        public Texture VideoTexture => _outTexture;
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
        // D3D11 拷贝链路诊断: 渲染事件数/实际拷贝数/目标缺失数/最近 fence 值/打开次数;
        // D3D12 模式追加: 目标未解析/无命令列表/拷贝/打开
        public string GetDx11Debug()
        {
            if (_player == IntPtr.Zero) return "player=null";
            AvoxNative.avoxPlayerGetDx11Debug(_player, out int ev, out int cp, out int tn, out long fv, out int op);
            var s = $"events={ev} copies={cp} targetNull={tn} fence={fv} opens={op}";
            if (GpuFlavor == 3)
            {
                AvoxNative.avoxPlayerGetDx12Debug(_player, out int nt, out int ncl, out int c12, out int o12);
                s += $" | dx12: noTarget={nt} noCl={ncl} copies={c12} opens={o12}";
            }
            return s;
        }

        IntPtr _player = IntPtr.Zero;
        Texture2D _texture;                   // GPU 直通/D3D12 拷贝的纹理 (CPU 回退下为 null)
        Texture _outTexture;                  // 对外输出 (GPU=_texture, CPU=_yuv.Output)
        readonly AvoxYuvBlitter _yuv = new AvoxYuvBlitter();  // CPU 回退: YUV 上传 + shader 转换
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
            // 待加载字幕: 等就绪/播放中再挂 (open 后 subtitle 才有效)
            if (_pendingSrt != null &&
                (State == AvoxPlayerState.Ready || State == AvoxPlayerState.Playing))
            {
                var sub = _pendingSrt;
                _pendingSrt = null;
                SubtitlePath = sub;
                if (LoadSrt(sub)) Debug.Log($"[AvoxPlayer] 字幕已加载: {sub}", this);
            }
            // GPU 直通: 主线程处理 enableVkOutput + NT句柄导入 (非Vulkan后端快速返回)
            AvoxNative.avoxPlayerUpdateGpu(_player);
            EnsureTexture();
            if (!GpuPassthrough && _yuv.IsReady)
            {
                // CPU 回退: 渲染线程经回调把整帧 NV12 上传到 R8 纹理 (Unity 完成上传),
                // 再用 shader Blit 成 RGB —— 逐像素色转全在 GPU
                _command.IssuePluginCustomTextureUpdateV2(
                    AvoxNative.avoxGetTextureUpdateCallback(), _yuv.YuvTexture, AvoxNative.avoxPlayerGetId(_player));
                Graphics.ExecuteCommandBuffer(_command);
                _command.Clear();
                _yuv.Blit();
            }
            else if (GpuPassthrough && GpuFlavor >= 2)
            {
                // D3D11/D3D12 拷贝模式: 渲染线程打开 avox 共享纹理 (D3D11 CopyResource /
                // D3D12 录 Unity 命令列表拷贝), fence 去重。纹理未就绪也要发: 首次事件
                // 负责打开共享纹理并回填尺寸, C# 据此 CreateExternalTexture; 事件同时
                // 驱动管线重建后的句柄轮询自愈
                if (GpuFlavor == 3 && _texture != null)
                {
                    // Unity 6 资源池可能迁移 native 指针, 每帧刷新拷贝目的
                    AvoxNative.avoxPlayerSetDx12Target(_player, _texture.GetNativeTexturePtr());
                }
                GL.IssuePluginEvent(AvoxNative.avoxGetRenderEventFunc(),
                                    (int)AvoxNative.avoxPlayerGetId(_player));
            }
        }

        void OnDestroy()
        {
            // 先销毁引用 GPU 导入资源的纹理, 再释放原生播放器
            DestroyTexture();
            _yuv.DisposeAll();
            _outTexture = null;
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
            // 同目录字幕自动发现 (精确同名/唯一候选自动加载, 多候选由 UI 下拉选择)
            var subs = FindSubtitles(url);
            SubtitlePath = null;
            _pendingSrt = null;
            if (subs.Length > 0)
            {
                var exact = Path.GetFileNameWithoutExtension(url);
                var isExact = Path.GetFileName(subs[0]).Equals(exact + ".srt", StringComparison.OrdinalIgnoreCase);
                if (isExact || subs.Length == 1) _pendingSrt = subs[0];
            }
        }

        string _pendingSrt;

        /// <summary>当前(或待加载)的字幕文件路径, 无则 null</summary>
        public string SubtitlePath { get; private set; }

        // 同目录字幕发现: 与视频同名/同前缀的 .srt (精确同名排最前); 网络流返回空
        public static string[] FindSubtitles(string videoPath)
        {
            try
            {
                if (string.IsNullOrEmpty(videoPath)) return Array.Empty<string>();
                var p = videoPath;
                if (p.StartsWith("file://", StringComparison.OrdinalIgnoreCase)) p = new Uri(p).LocalPath;
                if (p.Contains("://")) return Array.Empty<string>();
                var dir = Path.GetDirectoryName(p);
                var baseName = Path.GetFileNameWithoutExtension(p);
                if (string.IsNullOrEmpty(dir) || string.IsNullOrEmpty(baseName)) return Array.Empty<string>();
                var files = Directory.GetFiles(dir, baseName + "*.srt");
                Array.Sort(files, StringComparer.OrdinalIgnoreCase);
                var exact = Path.Combine(dir, baseName + ".srt");
                if (Array.IndexOf(files, exact) > 0)
                {
                    var list = new List<string>(files.Length) { exact };
                    foreach (var f in files)
                        if (!f.Equals(exact, StringComparison.OrdinalIgnoreCase))
                            list.Add(f);
                    return list.ToArray();
                }
                return files;
            }
            catch
            {
                return Array.Empty<string>();
            }
        }

        public void Close()
        {
            _pendingSrt = null;
            SubtitlePath = null;
            AvoxNative.avoxPlayerClose(_player);
        }

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
            if (!GpuPassthrough)
            {
                // CPU 回退: R8 纹理装整帧 NV12 + shader Blit → RenderTexture 对外
                _yuv.FlipY = cpuFlipY;
                _yuv.SetColorSpace(AvoxNative.avoxPlayerGetColorSpace(_player));
                if (!_yuv.Ensure(w, h)) return;
                DestroyTexture();
                _outTexture = _yuv.Output;
                _texW = _yuv.Width;
                _texH = _yuv.Height;
                BindToRenderer();
                onTextureCreated?.Invoke(_outTexture);
                Debug.Log($"[AvoxPlayer] 视频纹理就绪 {_texW}x{_texH} (CPU回退, YUV shader)", this);
                return;
            }
            // D3D12 拷贝模式: C# 普通纹理作拷贝目的 (插件每帧 CopyResource 进来)
            bool dx12Mode = GpuFlavor == 3;
            IntPtr nativeTex = IntPtr.Zero;
            if (!dx12Mode)
            {
                nativeTex = (IntPtr)AvoxNative.avoxPlayerGetExternalTexture(_player);
                if (nativeTex == IntPtr.Zero) return;
            }
            // 首帧/分辨率变化/原生纹理重建(重绑) 时重建包裹
            if (_texture != null && w == _texW && h == _texH && nativeTex == _wrappedNative) return;
            DestroyTexture();
            if (!dx12Mode)
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
                // D3D12 拷贝模式: sRGB 标志与 D3D11 外部纹理路径一致 (视频 RGB 为
                // sRGB 编码, Linear 项目按 linear 采样会发灰发淡)
                _texture = new Texture2D(w, h, TextureFormat.RGBA32, false, false);
                _texture.wrapMode = TextureWrapMode.Clamp;
                AvoxNative.avoxPlayerSetDx12Target(_player, _texture.GetNativeTexturePtr());
            }
            _outTexture = _texture;
            _texW = w;
            _texH = h;
            BindToRenderer();
            onTextureCreated?.Invoke(_outTexture);
            Debug.Log($"[AvoxPlayer] 视频纹理就绪 {w}x{h} (GPU直通{(dx12Mode ? "(D3D12拷贝)" : "")})", this);
        }

        void BindToRenderer()
        {
            if (targetRenderer == null || _outTexture == null) return;
            _mpb.Clear();
            _mpb.SetTexture("_MainTex", _outTexture);
            _mpb.SetTexture("_BaseMap", _outTexture);
            // D3D 拷贝模式: 原生 CopyResource 填充的纹理是 top-down 行序, Unity 采样约定
            // 是 bottom-up, 标准 Quad 上会上下颠倒 —— 用 ST 翻转 V 轴 (v' = 1 - v)。
            // CPU 回退不走 CopyResource, 行序由 shader 的 cpuFlipY 控制, 不在此翻。
            if (GpuPassthrough && GpuFlavor >= 2)
            {
                _mpb.SetVector("_MainTex_ST", new Vector4(1, -1, 0, 1));
                _mpb.SetVector("_BaseMap_ST", new Vector4(1, -1, 0, 1));
            }
            targetRenderer.SetPropertyBlock(_mpb);
        }

        // 只销毁 GPU 模式的包裹纹理; CPU 回退资源由 _yuv 自己管 (Ensure 重建/DisposeAll)
        void DestroyTexture()
        {
            if (_texture != null)
            {
                if (ReferenceEquals(_outTexture, _texture)) _outTexture = null;
                Destroy(_texture);
                _texture = null;
            }
            _wrappedNative = IntPtr.Zero;
            _texW = 0;
            _texH = 0;
        }
    }
}
