using System;
using System.Text;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace Avox
{
    /// <summary>
    /// 播放器控制面板 (uGUI, 运行时自建层级, 无需预制体) —— 对标 godot demo 的
    /// local_player: 进度拖动/播放暂停/停止/地址打开/音量/倍速/硬解/录制/字幕/
    /// 状态与媒体信息。挂到场景任意物体即可: 自动找场景里的 AvoxPlayer,
    /// 自动补 EventSystem。视频画面本身由 AvoxPlayer 渲染 (Quad/外部纹理均可)。
    /// </summary>
    [AddComponentMenu("Avox/Avox Player UI")]
    public class AvoxPlayerUI : MonoBehaviour
    {
        public AvoxPlayer player;   // 空则在 Awake 里自动查找
        public bool showInfoOnStart = false;

        // 主题 (与 godot demo 一致)
        static readonly Color ColBg = Hex(0x0b0f16ee);
        static readonly Color ColPanel = Hex(0x131a26ee);
        static readonly Color ColAccent = Hex(0x3b82f6ff);
        static readonly Color ColText = Hex(0xe5e9f0ff);
        static readonly Color ColDim = Hex(0x8b93a3ff);
        static readonly Color ColError = Hex(0xef4444ff);
        static readonly Color ColBtn = Hex(0x1c2536ee);

        static readonly string[] StateNames = { "空闲", "打开中", "就绪", "播放中", "暂停", "跳转", "缓冲", "已停止", "播放完成" };
        static readonly float[] Speeds = { 0.5f, 1.0f, 1.5f, 2.0f };

        Text _status, _timeLabel, _infoLabel, _topRight;
        Slider _seek, _volume;
        Button _playBtn, _recordBtn;
        Text _playLabel, _recordLabel;
        Dropdown _speed;
        Toggle _hardToggle, _infoToggle;
        InputField _url;
        GameObject _infoPanel;
        ScrubGuard _scrub;
        bool _uiBuilt;
        string _errorText = "";
        float _errorUntil;

        GameObject _canvasGo;

        static Color Hex(long rgba)
        {
            float a = (rgba & 0xff) / 255f, b = ((rgba >> 8) & 0xff) / 255f;
            float g = ((rgba >> 16) & 0xff) / 255f, r = ((rgba >> 24) & 0xff) / 255f;
            return new Color(r, g, b, a);
        }

        void Awake()
        {
            if (player == null) player = FindObjectOfType<AvoxPlayer>();
            if (FindObjectOfType<EventSystem>() == null)
            {
                new GameObject("EventSystem", typeof(EventSystem), typeof(StandaloneInputModule));
            }
            BuildUI();
        }

        void OnDestroy()
        {
            if (_canvasGo != null) Destroy(_canvasGo);
        }

        Font UiFont()
        {
            string[] prefer = { "Microsoft YaHei UI", "Microsoft YaHei", "微软雅黑", "SimHei", "黑体", "PingFang SC", "Noto Sans CJK SC" };
            try
            {
                var installed = Font.GetOSInstalledFontNames();
                foreach (var p in prefer)
                    foreach (var name in installed)
                        if (string.Equals(name, p, StringComparison.OrdinalIgnoreCase))
                            return Font.CreateDynamicFontFromOSFont(name, 14);
            }
            catch { /* 无系统字体枚举时回落内建 */ }
            return Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        }

        void BuildUI()
        {
            if (_uiBuilt || player == null) return;
            var font = UiFont();
            var root = new GameObject("AvoxPlayerUICanvas", typeof(Canvas), typeof(CanvasScaler), typeof(GraphicRaycaster));
            _canvasGo = root;
            var canvas = root.GetComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.sortingOrder = 100;
            var scaler = root.GetComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1280, 720);
            scaler.matchWidthOrHeight = 0.5f;

            // 背景条 (顶部状态 + 底部控制, 中间留给画面)
            var topBar = Panel(root.transform, "TopBar", ColBg, new Vector2(0, 1), new Vector2(1, 1),
                               new Vector2(0, -30), new Vector2(0, 30));
            _status = Text(topBar.transform, "Status", "空闲", font, 15, ColAccent,
                           TextAnchor.MiddleLeft, new Vector2(12, 0), new Vector2(620, 28));
            Left(_status);
            _topRight = Text(topBar.transform, "TopRight", "", font, 13, ColDim,
                             TextAnchor.MiddleRight, new Vector2(-12, 0), new Vector2(500, 28));
            Right(_topRight);

            var bottom = Panel(root.transform, "BottomPanel", ColBg, new Vector2(0, 0), new Vector2(1, 0),
                               new Vector2(0, 0), new Vector2(0, 128));
            var inner = Panel(bottom.transform, "Inner", new Color(0, 0, 0, 0), Vector2.zero, Vector2.one,
                              new Vector2(12, 8), new Vector2(-12, -8));

            // ── 行 1 (y=+38): 进度条 + 时间 (拖动预览, 松手 Seek) ──
            _seek = Slider(inner.transform, "Seek", font, new Vector2(0, 38), new Vector2(1120, 16));
            Left(_seek.gameObject);
            _timeLabel = Text(inner.transform, "Time", "00:00 / 00:00", font, 13, ColDim,
                              TextAnchor.MiddleRight, new Vector2(-4, 38), new Vector2(120, 22));
            Right(_timeLabel.gameObject);
            _seek.interactable = false;
            _scrub = _seek.gameObject.AddComponent<ScrubGuard>();
            _seek.onValueChanged.AddListener(v =>
            {
                if (!_scrub.down) return;
                var dur = player != null ? player.DurationMs : 0;
                if (dur > 0) _timeLabel.text = $"{FmtMs((long)(v / 1000f * dur))} / {FmtMs(dur)}";
            });
            _scrub.onSeekEnd = v =>
            {
                if (player == null) return;
                var dur = player.DurationMs;
                if (dur > 0) player.Seek((long)(v / 1000f * dur));
            };

            // ── 行 2 (y=0): 播放/停止/地址/打开/浏览 ──
            _playBtn = Button(inner.transform, "Play", "播放", font, new Vector2(0, 0), new Vector2(76, 30), ColAccent);
            _playLabel = _playBtn.GetComponentInChildren<Text>();
            Left(_playBtn.gameObject);
            _playBtn.onClick.AddListener(TogglePlay);
            var stopBtn = Button(inner.transform, "Stop", "停止", font, new Vector2(84, 0), new Vector2(60, 30), ColBtn);
            Left(stopBtn.gameObject);
            stopBtn.onClick.AddListener(() => { if (player != null) player.Close(); });
            _url = Input(inner.transform, "Url", font, new Vector2(152, 0), new Vector2(790, 30),
                         player != null ? player.url : "");
            var openBtn = Button(inner.transform, "Open", "打开", font, new Vector2(950, 0), new Vector2(60, 30), ColBtn);
            Left(openBtn.gameObject);
            openBtn.onClick.AddListener(() =>
            {
                if (player == null) return;
                player.url = _url.text;
                player.Open(player.url);
            });
#if UNITY_EDITOR
            var browseBtn = Button(inner.transform, "Browse", "浏览", font, new Vector2(1018, 0), new Vector2(60, 30), ColBtn);
            Left(browseBtn.gameObject);
            browseBtn.onClick.AddListener(() =>
            {
                var p = UnityEditor.EditorUtility.OpenFilePanel("选择视频文件", "", "mp4,mkv,flv,mov,ts,avi;所有文件,*");
                if (string.IsNullOrEmpty(p)) return;
                _url.text = p;
                if (player != null) { player.url = p; player.Open(p); }
            });
#endif

            // ── 行 3 (y=-38): 音量/倍速/硬解/录制/字幕/信息 ──
            var volLabel = Text(inner.transform, "VolLabel", "音量", font, 13, ColDim,
                                TextAnchor.MiddleLeft, new Vector2(0, -38), new Vector2(36, 22));
            Left(volLabel.gameObject);
            _volume = Slider(inner.transform, "Volume", font, new Vector2(44, -38), new Vector2(150, 14));
            Left(_volume.gameObject);
            _volume.maxValue = 1f;
            _volume.value = player != null ? player.volume : 1f;
            _volume.onValueChanged.AddListener(v => { if (player != null) player.SetVolume(v); });
            _speed = Dropdown(inner.transform, "Speed", font, new Vector2(214, -38), new Vector2(96, 26));
            Left(_speed.gameObject);
            _speed.onValueChanged.AddListener(i => { if (player != null) player.SetSpeed(Speeds[i]); });
            _hardToggle = Toggle(inner.transform, "HardDec", font, "硬解", new Vector2(330, -38), ColText);
            _hardToggle.isOn = player != null && player.hardDecode;
            _hardToggle.onValueChanged.AddListener(on =>
            {
                if (player != null) player.hardDecode = on;
            });
            _recordBtn = Button(inner.transform, "Record", "● 录制", font, new Vector2(420, -38), new Vector2(86, 28), ColBtn);
            _recordLabel = _recordBtn.GetComponentInChildren<Text>();
            Left(_recordBtn.gameObject);
            _recordBtn.onClick.AddListener(ToggleRecord);
#if UNITY_EDITOR
            var srtBtn = Button(inner.transform, "Srt", "字幕", font, new Vector2(514, -38), new Vector2(60, 28), ColBtn);
            Left(srtBtn.gameObject);
            srtBtn.onClick.AddListener(() =>
            {
                if (player == null) return;
                var p = UnityEditor.EditorUtility.OpenFilePanel("选择 SRT 字幕", "", "srt");
                if (!string.IsNullOrEmpty(p)) player.LoadSrt(p);
            });
#endif
            _infoToggle = Toggle(inner.transform, "InfoTgl", font, "信息", new Vector2(600, -38), ColText);
            Left(_infoToggle.gameObject);
            _infoToggle.isOn = showInfoOnStart;

            // ── 信息面板 ──
            var infoBg = Panel(root.transform, "InfoPanel", ColPanel, new Vector2(1, 1), new Vector2(1, 1),
                               new Vector2(-12, -40), new Vector2(300, 160));
            infoBg.GetComponent<RectTransform>().pivot = new Vector2(1, 1);
            infoBg.SetActive(showInfoOnStart);
            _infoPanel = infoBg;
            _infoLabel = Text(infoBg.transform, "Info", "", font, 12, ColText,
                              TextAnchor.UpperLeft, new Vector2(8, -8), new Vector2(-16, -16));
            _infoToggle.onValueChanged.AddListener(on => { if (_infoPanel != null) _infoPanel.SetActive(on); });
            _infoPanel.SetActive(_infoToggle.isOn);

            player.onError.AddListener((code, msg) =>
            {
                _errorText = $"[{code}] {msg}";
                _errorUntil = Time.time + 4f;
            });
            _uiBuilt = true;
        }

        void TogglePlay()
        {
            if (player == null) return;
            var s = player.State;
            if (s == AvoxPlayerState.Pause) player.Resume();
            else if (s == AvoxPlayerState.Playing) player.Pause();
            else if (s == AvoxPlayerState.None || s == AvoxPlayerState.Stopped || s == AvoxPlayerState.Completed)
            {
                player.url = _url.text;
                player.Open(player.url);
            }
        }

        void ToggleRecord()
        {
            if (player == null) return;
            if (player.RecordState == 2) { player.StopRecord(); return; }
            var path = $"record_{DateTime.Now:yyyyMMdd_HHmmss}.mp4";
            player.StartRecord(path);
        }

        void Update()
        {
            if (!_uiBuilt || player == null) return;
            var st = player.State;
            // 状态栏 (错误优先显示)
            if (_errorText != "" && Time.time < _errorUntil)
            {
                _status.text = _errorText;
                _status.color = ColError;
            }
            else
            {
                _status.text = StateNames[(int)Mathf.Clamp((float)st, 0, 8)] + (player.RecordState == 2 ? "　● 录制中" : "");
                _status.color = ColAccent;
            }
            // 右上: 分辨率 + GPU 模式
            var tex = player.VideoTexture;
            var gpu = player.GpuPassthrough ? (player.GpuFlavor == 2 ? "GPU直通 D3D11" : "GPU直通 Vulkan") : "CPU回退";
            _topRight.text = (tex != null ? $"{tex.width}x{tex.height}　" : "") + gpu;
            // 时间与进度
            var dur = player.DurationMs;
            var pos = player.PositionMs;
            _timeLabel.text = $"{FmtMs(pos)} / {FmtMs(dur)}";
            if (!_scrub.down && dur > 0) _seek.value = (float)(pos / (double)dur) * 1000f; // 程序赋值, 回调有 down 守卫不会回环
            _seek.interactable = dur > 0;
            // 播放键文案
            var playText = st == AvoxPlayerState.Pause ? "继续" :
                           (st == AvoxPlayerState.Playing ? "暂停" : "播放");
            if (_playLabel.text != playText) _playLabel.text = playText;
            // 录制键文案
            var recText = player.RecordState == 2 ? "■ 停止" : "● 录制";
            if (_recordLabel.text != recText) _recordLabel.text = recText;
            // 信息面板
            if (_infoPanel.activeSelf)
            {
                var sb = new StringBuilder();
                sb.AppendLine($"状态: {StateNames[(int)Mathf.Clamp((float)st, 0, 8)]}");
                sb.AppendLine($"地址: {player.url}");
                if (tex != null) sb.AppendLine($"分辨率: {tex.width}x{tex.height}");
                sb.AppendLine($"时长: {FmtMs(dur)}　进度: {player.Progress:P1}");
                sb.AppendLine($"输出: {(player.GpuPassthrough ? $"GPU直通 (flavor={player.GpuFlavor})" : "CPU回退")}");
                sb.AppendLine($"录制: {RecordName(player.RecordState)}　倍速: {Speeds[_speed.value]:0.0#}");
                _infoLabel.text = sb.ToString();
            }
        }

        static string RecordName(int s) => s == 0 ? "空闲" : s == 1 ? "打开" : s == 2 ? "录制中" : s == 3 ? "完成" : s.ToString();

        static string FmtMs(long ms)
        {
            if (ms <= 0) return "00:00";
            var t = TimeSpan.FromMilliseconds(ms);
            return t.TotalHours >= 1 ? $"{(int)t.TotalHours}:{t.Minutes:D2}:{t.Seconds:D2}" : $"{t.Minutes:D2}:{t.Seconds:D2}";
        }

        // ── uGUI 构建辅助 ──

        // 子元素锚定: 左/右 (逻辑 1280x720 绝对布局用)
        static void Left(GameObject go)
        {
            var rt = (RectTransform)go.transform;
            rt.anchorMin = rt.anchorMax = rt.pivot = new Vector2(0, 0.5f);
        }

        static void Right(GameObject go)
        {
            var rt = (RectTransform)go.transform;
            rt.anchorMin = rt.anchorMax = new Vector2(1, 0.5f);
            rt.pivot = new Vector2(1, 0.5f);
        }

        static void Left(Component c) => Left(c.gameObject);

        static void Right(Component c) => Right(c.gameObject);

        static GameObject Panel(Transform parent, string name, Color color, Vector2 anchorMin,
                                Vector2 anchorMax, Vector2 offset, Vector2 size)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image));
            var rt = (RectTransform)go.transform;
            rt.SetParent(parent, false);
            rt.anchorMin = anchorMin;
            rt.anchorMax = anchorMax;
            rt.sizeDelta = size;
            rt.anchoredPosition = offset;
            go.GetComponent<Image>().color = color;
            return go;
        }

        static Text Text(Transform parent, string name, string content, Font font, int fontSize,
                         Color color, TextAnchor anchor, Vector2 pos, Vector2 size)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Text));
            var rt = (RectTransform)go.transform;
            rt.SetParent(parent, false);
            rt.sizeDelta = size;
            rt.anchoredPosition = pos;
            var t = go.GetComponent<Text>();
            t.text = content;
            t.font = font;
            t.fontSize = fontSize;
            t.color = color;
            t.alignment = anchor;
            t.horizontalOverflow = HorizontalWrapMode.Overflow;
            t.raycastTarget = false;
            return t;
        }

        static Button Button(Transform parent, string name, string label, Font font,
                             Vector2 pos, Vector2 size, Color color)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image), typeof(Button));
            var rt = (RectTransform)go.transform;
            rt.SetParent(parent, false);
            rt.sizeDelta = size;
            rt.anchoredPosition = pos;
            go.GetComponent<Image>().color = color;
            var t = Text(go.transform, "Label", label, font, 14, ColText, TextAnchor.MiddleCenter,
                         Vector2.zero, size);
            var trt = (RectTransform)t.transform;
            trt.anchorMin = Vector2.zero;
            trt.anchorMax = Vector2.one;
            trt.sizeDelta = Vector2.zero;
            trt.anchoredPosition = Vector2.zero;
            return go.GetComponent<Button>();
        }

        static Slider Slider(Transform parent, string name, Font font, Vector2 pos, Vector2 size)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Slider));
            var rt = (RectTransform)go.transform;
            rt.SetParent(parent, false);
            rt.sizeDelta = size;
            rt.anchoredPosition = pos;
            var bg = Panel(go.transform, "Background", Hex(0x2a3346ff), Vector2.zero, Vector2.one, Vector2.zero, Vector2.zero);
            Stretch(bg);
            var fillArea = Panel(go.transform, "Fill Area", new Color(0, 0, 0, 0), new Vector2(0, 0.25f), new Vector2(1, 0.75f), new Vector2(4, 0), new Vector2(-8, 0));
            Stretch(fillArea);
            var fill = Panel(fillArea.transform, "Fill", ColAccent, Vector2.zero, Vector2.one, Vector2.zero, Vector2.zero);
            Stretch(fill);
            var handleArea = Panel(go.transform, "Handle Slide Area", new Color(0, 0, 0, 0), Vector2.zero, Vector2.one, new Vector2(8, 0), new Vector2(-16, 0));
            Stretch(handleArea);
            var handle = Panel(handleArea.transform, "Handle", Hex(0xe5e9f0ff), new Vector2(0, 0.2f), new Vector2(0.08f, 0.8f), Vector2.zero, Vector2.zero);
            var s = go.GetComponent<Slider>();
            s.fillRect = (RectTransform)fill.transform;
            s.handleRect = (RectTransform)handle.transform;
            s.targetGraphic = handle.GetComponent<Image>();
            s.direction = UnityEngine.UI.Slider.Direction.LeftToRight;
            s.minValue = 0;
            s.maxValue = 1000;
            s.wholeNumbers = false;
            return s;
        }

        static void Stretch(GameObject go)
        {
            var rt = (RectTransform)go.transform;
            rt.anchorMin = Vector2.zero;
            rt.anchorMax = Vector2.one;
            rt.sizeDelta = Vector2.zero;
            rt.anchoredPosition = Vector2.zero;
        }

        static Dropdown Dropdown(Transform parent, string name, Font font, Vector2 pos, Vector2 size)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image), typeof(Dropdown));
            var rt = (RectTransform)go.transform;
            rt.SetParent(parent, false);
            rt.sizeDelta = size;
            rt.anchoredPosition = pos;
            go.GetComponent<Image>().color = Hex(0x1c2536ee);
            var label = Text(go.transform, "Label", "1.0x", font, 13, ColText, TextAnchor.MiddleLeft,
                             new Vector2(-8, 0), new Vector2(-24, 0));
            var lrt = (RectTransform)label.transform;
            lrt.anchorMin = Vector2.zero;
            lrt.anchorMax = Vector2.one;
            var arrow = Text(go.transform, "Arrow", "▼", font, 12, ColDim, TextAnchor.MiddleRight,
                             new Vector2(-6, 0), new Vector2(16, 0));
            var art = (RectTransform)arrow.transform;
            art.anchorMin = new Vector2(1, 0);
            art.anchorMax = new Vector2(1, 1);
            // 模板
            var tpl = Panel(go.transform, "Template", Hex(0x131a26ff), new Vector2(0, 0), new Vector2(1, 0),
                            new Vector2(0, 2), new Vector2(0, 110));
            var vp = Panel(tpl.transform, "Viewport", new Color(0, 0, 0, 0), Vector2.zero, Vector2.one, Vector2.zero, Vector2.zero);
            var content = Panel(vp.transform, "Content", new Color(0, 0, 0, 0), new Vector2(0.5f, 1), new Vector2(0.5f, 1), Vector2.zero, new Vector2(0, 28));
            var item = Panel(content.transform, "Item", Hex(0x1c2536ff), new Vector2(0, 0.5f), new Vector2(1, 0.5f), Vector2.zero, new Vector2(0, 26));
            var itemLabel = Text(item.transform, "Item Label", "1.0x", font, 13, ColText, TextAnchor.MiddleLeft, new Vector2(-8, 0), new Vector2(-24, 0));
            var ilt = (RectTransform)itemLabel.transform;
            ilt.anchorMin = Vector2.zero;
            ilt.anchorMax = Vector2.one;
            var d = go.GetComponent<Dropdown>();
            d.captionText = label;
            d.itemText = itemLabel;
            d.template = (RectTransform)tpl.transform;
            d.targetGraphic = go.GetComponent<Image>();
            d.options.Clear();
            foreach (var v in new[] { "0.5x", "1.0x", "1.5x", "2.0x" }) d.options.Add(new Dropdown.OptionData(v));
            d.value = 1;
            d.RefreshShownValue();
            tpl.SetActive(false);
            return d;
        }

        static Toggle Toggle(Transform parent, string name, Font font, string label, Vector2 pos, Color color)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Toggle));
            var rt = (RectTransform)go.transform;
            rt.SetParent(parent, false);
            rt.sizeDelta = new Vector2(66, 26);
            rt.anchoredPosition = pos;
            var box = Panel(go.transform, "Background", Hex(0x2a3346ff), new Vector2(0, 0.5f), new Vector2(0, 0.5f),
                            new Vector2(10, 0), new Vector2(20, 20));
            var check = Panel(box.transform, "Checkmark", ColAccent, Vector2.zero, Vector2.one, Vector2.zero, Vector2.zero);
            var t = Text(go.transform, "Label", label, font, 13, color, TextAnchor.MiddleLeft,
                         new Vector2(14, 0), new Vector2(52, 22));
            var trt = (RectTransform)t.transform;
            trt.anchorMin = new Vector2(0, 0);
            trt.anchorMax = new Vector2(1, 1);
            var tg = go.GetComponent<Toggle>();
            tg.graphic = check.GetComponent<Image>();
            tg.targetGraphic = box.GetComponent<Image>();
            return tg;
        }

        static InputField Input(Transform parent, string name, Font font, Vector2 pos, Vector2 size, string value)
        {
            var go = new GameObject(name, typeof(RectTransform), typeof(Image), typeof(InputField));
            var rt = (RectTransform)go.transform;
            rt.SetParent(parent, false);
            rt.sizeDelta = size;
            rt.anchoredPosition = pos;
            go.GetComponent<Image>().color = Hex(0x0b0f16ff);
            var text = Text(go.transform, "Text", "", font, 13, ColText, TextAnchor.MiddleLeft, new Vector2(-8, 0), new Vector2(-16, 0));
            var trt = (RectTransform)text.transform;
            trt.anchorMin = Vector2.zero;
            trt.anchorMax = Vector2.one;
            var holder = Text(go.transform, "Placeholder", "rtmp/rtsp/http/本地路径…", font, 12, ColDim,
                              TextAnchor.MiddleLeft, new Vector2(-8, 0), new Vector2(-16, 0));
            var hrt = (RectTransform)holder.transform;
            hrt.anchorMin = Vector2.zero;
            hrt.anchorMax = Vector2.one;
            var f = go.GetComponent<InputField>();
            f.textComponent = text;
            f.text = value;
            f.placeholder = holder;
            f.targetGraphic = go.GetComponent<Image>();
            return f;
        }

        // 进度条按下拖动防回跳, 松手触发 Seek
        class ScrubGuard : MonoBehaviour, IPointerDownHandler, IPointerUpHandler
        {
            public bool down;
            public Action<float> onSeekEnd;
            Slider _slider;

            public void OnPointerDown(PointerEventData e) => down = true;

            public void OnPointerUp(PointerEventData e)
            {
                down = false;
                if (onSeekEnd != null && _slider != null) onSeekEnd(_slider.value);
            }

            void Start() => _slider = GetComponent<Slider>();
        }
    }
}
