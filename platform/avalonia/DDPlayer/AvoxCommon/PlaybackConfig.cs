using AvoxNet;
using System;
using System.ComponentModel;

namespace AvoxCommon
{
    /// <summary>
    /// 播放相关配置
    /// 包含播放设置和渲染设置
    /// </summary>
    public class PlaybackConfig : INotifyPropertyChanged
    {
        /// <summary>
        /// 获取所有可用的 IO 方案列表
        /// </summary>
        #region 播放设置字段
        private bool enableHardwareDecoding = true;
        private bool useVulkanRendering = true;
        private bool enableLowLatency = false;
        private int bufferSizeMs = 1000;
        private bool autoPlay = true;
        private double defaultVolume = 0.8;
        private bool enableFrameSkip = true;
        private bool enableAudio = true;
        private bool enableVideo = true;
        private IoPlan ioPlan = IoPlan.ffmpeg;
        #endregion

        #region 水印设置字段
        private bool enableWatermark = false;
        private string watermarkPath = "blend.png";
        private float watermarkCenterX = 0.1f;    // 归一化中心 X (0-1)
        private float watermarkCenterY = 0.1f;    // 归一化中心 Y (0-1)
        private float watermarkWidth = 0.1f;      // 归一化宽度 (0-1)
        private float watermarkHeight = 0.05f;    // 归一化高度 (0-1)
        private float watermarkAlpha = 0.5f;      // 透明度 (0-1)
        #endregion

        /// <summary>
        /// 是否启用硬件解码
        /// </summary>
        public bool EnableHardwareDecoding
        {
            get => enableHardwareDecoding;
            set
            {
                if (enableHardwareDecoding != value)
                {
                    enableHardwareDecoding = value;
                    OnPropertyChanged(nameof(EnableHardwareDecoding));
                }
            }
        }

        /// <summary>
        /// 是否使用Vulkan渲染
        /// </summary>
        public bool UseVulkanRendering
        {
            get => useVulkanRendering;
            set
            {
                if (useVulkanRendering != value)
                {
                    useVulkanRendering = value;
                    OnPropertyChanged(nameof(UseVulkanRendering));
                }
            }
        }

        /// <summary>
        /// 是否启用低延迟模式
        /// </summary>
        public bool EnableLowLatency
        {
            get => enableLowLatency;
            set
            {
                if (enableLowLatency != value)
                {
                    enableLowLatency = value;
                    OnPropertyChanged(nameof(EnableLowLatency));
                }
            }
        }

        /// <summary>
        /// 缓冲大小（毫秒）
        /// </summary>
        public int BufferSizeMs
        {
            get => bufferSizeMs;
            set
            {
                if (bufferSizeMs != value)
                {
                    bufferSizeMs = value;
                    OnPropertyChanged(nameof(BufferSizeMs));
                }
            }
        }

        /// <summary>
        /// 是否自动播放
        /// </summary>
        public bool AutoPlay
        {
            get => autoPlay;
            set
            {
                if (autoPlay != value)
                {
                    autoPlay = value;
                    OnPropertyChanged(nameof(AutoPlay));
                }
            }
        }

        /// <summary>
        /// 默认音量（0-1）
        /// </summary>
        public double DefaultVolume
        {
            get => defaultVolume;
            set
            {
                if (defaultVolume != value)
                {
                    defaultVolume = value;
                    OnPropertyChanged(nameof(DefaultVolume));
                }
            }
        }

        /// <summary>
        /// 是否启用跳帧
        /// </summary>
        public bool EnableFrameSkip
        {
            get => enableFrameSkip;
            set
            {
                if (enableFrameSkip != value)
                {
                    enableFrameSkip = value;
                    OnPropertyChanged(nameof(EnableFrameSkip));
                }
            }
        }

        /// <summary>
        /// 是否启用音频
        /// </summary>
        public bool EnableAudio
        {
            get => enableAudio;
            set
            {
                if (enableAudio != value)
                {
                    enableAudio = value;
                    OnPropertyChanged(nameof(EnableAudio));
                }
            }
        }

        /// <summary>
        /// 是否启用视频
        /// </summary>
        public bool EnableVideo
        {
            get => enableVideo;
            set
            {
                if (enableVideo != value)
                {
                    enableVideo = value;
                    OnPropertyChanged(nameof(EnableVideo));
                }
            }
        }

        /// <summary>
        /// IO选择方案
        /// </summary>
        public IoPlan IoPlan
        {
            get => ioPlan;
            set
            {
                if (ioPlan != value)
                {
                    ioPlan = value;
                    OnPropertyChanged(nameof(IoPlan));
                }
            }
        }

        #region 水印设置属性

        /// <summary>
        /// 是否启用水印
        /// </summary>
        public bool EnableWatermark
        {
            get => enableWatermark;
            set
            {
                if (enableWatermark != value)
                {
                    enableWatermark = value;
                    OnPropertyChanged(nameof(EnableWatermark));
                }
            }
        }

        /// <summary>
        /// 水印图片路径（对应assets/images下的文件名，或外部绝对路径）
        /// </summary>
        public string WatermarkPath
        {
            get => watermarkPath;
            set
            {
                if (watermarkPath != value)
                {
                    watermarkPath = value;
                    OnPropertyChanged(nameof(WatermarkPath));
                }
            }
        }

        /// <summary>
        /// 水印中心 X 坐标 (归一化 0-1)
        /// </summary>
        public float WatermarkCenterX
        {
            get => watermarkCenterX;
            set
            {
                value = Math.Max(0, Math.Min(1, value));
                if (watermarkCenterX != value)
                {
                    watermarkCenterX = value;
                    OnPropertyChanged(nameof(WatermarkCenterX));
                }
            }
        }

        /// <summary>
        /// 水印中心 Y 坐标 (归一化 0-1)
        /// </summary>
        public float WatermarkCenterY
        {
            get => watermarkCenterY;
            set
            {
                value = Math.Max(0, Math.Min(1, value));
                if (watermarkCenterY != value)
                {
                    watermarkCenterY = value;
                    OnPropertyChanged(nameof(WatermarkCenterY));
                }
            }
        }

        /// <summary>
        /// 水印宽度 (归一化 0-1)
        /// </summary>
        public float WatermarkWidth
        {
            get => watermarkWidth;
            set
            {
                value = Math.Max(0.01f, Math.Min(1, value));
                if (watermarkWidth != value)
                {
                    watermarkWidth = value;
                    OnPropertyChanged(nameof(WatermarkWidth));
                }
            }
        }

        /// <summary>
        /// 水印高度 (归一化 0-1)
        /// </summary>
        public float WatermarkHeight
        {
            get => watermarkHeight;
            set
            {
                value = Math.Max(0.01f, Math.Min(1, value));
                if (watermarkHeight != value)
                {
                    watermarkHeight = value;
                    OnPropertyChanged(nameof(WatermarkHeight));
                }
            }
        }

        /// <summary>
        /// 水印透明度 (0-1)
        /// </summary>
        public float WatermarkAlpha
        {
            get => watermarkAlpha;
            set
            {
                value = Math.Max(0, Math.Min(1, value));
                if (watermarkAlpha != value)
                {
                    watermarkAlpha = value;
                    OnPropertyChanged(nameof(WatermarkAlpha));
                }
            }
        }

        /// <summary>
        /// 创建原生 Watermark 对象
        /// </summary>
        public Watermark CreateNativeWatermark()
        {
            if (!EnableWatermark) return null;
            if (string.IsNullOrWhiteSpace(WatermarkPath)) return null;

            return new Watermark
            {
                centerX = WatermarkCenterX,
                centerY = WatermarkCenterY,
                width = WatermarkWidth,
                height = WatermarkHeight,
                alaph = WatermarkAlpha
            };
        }

        #endregion

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
    }
}