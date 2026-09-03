using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using AvoxNet;

namespace AvoxCommon
{
    /// <summary>
    /// 播放器配置类，包含所有配置项
    /// </summary>
    public class PlayerConfig : INotifyPropertyChanged
    {
        private PlaybackConfig playback = new PlaybackConfig();
        private EncodingConfig encoding = new EncodingConfig();
        private HistoryConfig history = new HistoryConfig();
        private RtcConfig rtc = new RtcConfig();
        private SttConfig stt = new SttConfig();        

        /// <summary>
        /// 播放配置
        /// </summary>
        public PlaybackConfig Playback
        {
            get => playback;
            set
            {
                if (playback != value)
                {
                    playback = value;
                    OnPropertyChanged(nameof(Playback));
                }
            }
        }

        /// <summary>
        /// 编码配置
        /// </summary>
        public EncodingConfig Encoding
        {
            get => encoding;
            set
            {
                if (encoding != value)
                {
                    encoding = value;
                    OnPropertyChanged(nameof(Encoding));
                }
            }
        }      

        /// <summary>
        /// 历史记录配置
        /// </summary>
        public HistoryConfig History
        {
            get => history;
            set
            {
                if (history != value)
                {
                    history = value;
                    OnPropertyChanged(nameof(History));
                }
            }
        }

        /// <summary>
        /// WebRTC配置
        /// </summary>
        public RtcConfig Rtc
        {
            get => rtc;
            set
            {
                if (rtc != value)
                {
                    rtc = value;
                    OnPropertyChanged(nameof(Rtc));
                }
            }
        }

        /// <summary>
        /// 语音识别与翻译配置
        /// </summary>
        public SttConfig Stt
        {
            get => stt;
            set
            {
                if (stt != value)
                {
                    stt = value;
                    OnPropertyChanged(nameof(Stt));
                }
            }
        }        

        /// <summary>
        /// 属性变更通知事件
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// 触发属性变更通知
        /// </summary>
        /// <param name="propertyName">属性名称</param>
        protected virtual void OnPropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        /// <summary>
        /// 克隆配置
        /// </summary>
        public PlayerConfig Clone()
        {
            var json = JsonSerializer.Serialize(this);
            return JsonSerializer.Deserialize<PlayerConfig>(json);
        }

        public void CopyFrom(PlayerConfig source)
        {
            if (source == null) return;

            // 使用 JSON 序列化实现完整复制
            var json = System.Text.Json.JsonSerializer.Serialize(source);
            var temp = System.Text.Json.JsonSerializer.Deserialize<PlayerConfig>(json);

            // 复制所有属性值
            var properties = typeof(PlayerConfig).GetProperties(
                System.Reflection.BindingFlags.Public |
                System.Reflection.BindingFlags.Instance);

            foreach (var prop in properties)
            {
                if (prop.CanRead && prop.CanWrite)
                {
                    var value = prop.GetValue(temp);
                    prop.SetValue(this, value);
                }
            }
        }
    }

    /// <summary>
    /// 配置管理器，负责保存和加载配置
    /// 实现为单例模式，所有位置都可直接通过Instance访问
    /// </summary>
    public class ConfigManager
    {
        // 单例实例
        private static ConfigManager instance;
        private static readonly object lockObj = new object();

        // 配置文件路径（跨平台兼容）
        private static string ConfigPath => GetConfigPath();

        private PlayerConfig config;

        // 单例实例属性
        public static ConfigManager Instance
        {
            get
            {
                if (instance == null)
                {
                    lock (lockObj)
                    {
                        if (instance == null)
                        {
                            instance = new ConfigManager();
                        }
                    }
                }
                return instance;
            }
        }

        /// <summary>
        /// 私有构造函数，防止外部实例化
        /// </summary>
        private ConfigManager()
        {
            LoadConfig();
        }

        /// <summary>
        /// 跨平台获取配置路径
        /// </summary>
        private static string GetConfigPath()
        {
            string basePath = Helper.GetBasePath();
            return Path.Combine(basePath, "DDPlayer", "config.json");
        }

        /// <summary>
        /// 加载配置
        /// </summary>
        public void LoadConfig()
        {
            try
            {
                if (File.Exists(ConfigPath))
                {
                    var json = File.ReadAllText(ConfigPath, Encoding.UTF8);
                    config = JsonSerializer.Deserialize<PlayerConfig>(json, new JsonSerializerOptions
                    {
                        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                        WriteIndented = true,
                        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
                    });

                    // 调试日志
                    Console.WriteLine($"[ConfigManager] 加载配置成功, 历史记录数量: {config?.History?.MediaHistory?.Count ?? 0}");
                }
                else
                {
                    config = CreateDefaultConfig();
                    SaveConfig();
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"加载配置失败: {ex.Message}\n{ex.StackTrace}");
                config = CreateDefaultConfig();
            }
        }

        /// <summary>
        /// 保存配置
        /// </summary>
        public void SaveConfig()
        {
            try
            {
                var directory = Path.GetDirectoryName(ConfigPath);
                if (!Directory.Exists(directory))
                {
                    Directory.CreateDirectory(directory);
                }

                var json = JsonSerializer.Serialize(config, new JsonSerializerOptions
                {
                    PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
                    WriteIndented = true,
                    Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
                });
                File.WriteAllText(ConfigPath, json, Encoding.UTF8);

                Console.WriteLine($"[ConfigManager] 保存配置成功");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"保存配置失败: {ex.Message}\n{ex.StackTrace}");
            }
        }

        /// <summary>
        /// 创建默认配置
        /// </summary>
        private PlayerConfig CreateDefaultConfig()
        {
            return new PlayerConfig
            {
                Playback = new PlaybackConfig
                {
                    EnableHardwareDecoding = true,
                    UseVulkanRendering = true,
                    EnableLowLatency = false,
                    BufferSizeMs = 2000,
                    AutoPlay = true,
                    DefaultVolume = 0.8,
                    EnableFrameSkip = true,
                    EnableAudio = true,
                    EnableVideo = true,
                    IoPlan = IoPlan.zlmediakit
                },
                Encoding = new EncodingConfig
                {
                    EnableHardwareEncoding = true,
                    VideoBitrate = 2000,
                    AudioBitrate = 128,
                    FrameRate = 30,
                    VideoWidth = 1280,
                    VideoHeight = 720,
                    VideoCodec = VCodecId.h265,
                    AudioCodec = ACodecId.aac,
                    GopSize = 60,
                    EnableBFrames = false,
                    LocalPath = Helper.GetBasePath()
                },             
                History = new HistoryConfig
                {
                    MaxHistoryItems = 10
                },
                Rtc = new RtcConfig
                {
                    RtcRollType = RtcRollType.offer,
                    RtcMode = RtcMode.Pull,
                    PushUri = "",
                    PullUri = "",
                    UseSSE = false,
                    SseServer = "",
                    SseToken = "",
                    SseId = "",
                    TurnServer = "",
                    TurnUsername = "",
                    TurnPassword = ""
                },
                Stt = new SttConfig
                {
                    EnableStt = false,
                    EnableTranslation = false
                }
            };
        }

        /// <summary>
        /// 获取当前配置
        /// </summary>
        public PlayerConfig Config
        {
            get { return config; }
        }

        /// <summary>
        /// 更新配置
        /// </summary>
        public void UpdateConfig(PlayerConfig newConfig)
        {
            // 确保使用新配置对象
            config = newConfig;

            // 保存配置
            SaveConfig();
        }       
       
    }
}