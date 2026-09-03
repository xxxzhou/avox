using AvoxNet;
using System.ComponentModel;

namespace AvoxCommon
{
    /// <summary>
    /// 录制模式
    /// </summary>
    public enum RecordMode
    {
        /// <summary>
        /// 本地文件
        /// </summary>
        Local = 0,
        /// <summary>
        /// 网络推流
        /// </summary>
        Network = 1
    }



    /// <summary>
    /// 编码相关配置
    /// </summary>
    public class EncodingConfig : INotifyPropertyChanged
    {
        private bool enableHardwareEncoding = true;
        private int videoBitrate = 2000;
        private int audioBitrate = 128;
        private int frameRate = 30;
        private int videoWidth = 1280;
        private int videoHeight = 720;
        private VCodecId videoCodec = VCodecId.h264;
        private ACodecId audioCodec = ACodecId.aac;
        private MuxerType muxerType = MuxerType.ffmpeg;
        // VCodecId
        private int gopSize = 60;
        private bool enableBFrames = false;
        private RecordMode recordMode = RecordMode.Local;
        private string pushUrl = "rtsp://127.0.0.1/live/test";
        private string localPath = "D://Back/savemedia";        

        /// <summary>
        /// 是否启用硬件编码
        /// </summary>
        public bool EnableHardwareEncoding
        {
            get => enableHardwareEncoding;
            set
            {
                if (enableHardwareEncoding != value)
                {
                    enableHardwareEncoding = value;
                    OnPropertyChanged(nameof(EnableHardwareEncoding));
                }
            }
        }

        /// <summary>
        /// 视频码率（kbps）
        /// </summary>
        public int VideoBitrate
        {
            get => videoBitrate;
            set
            {
                if (videoBitrate != value)
                {
                    videoBitrate = value;
                    OnPropertyChanged(nameof(VideoBitrate));
                }
            }
        }

        /// <summary>
        /// 音频码率（kbps）
        /// </summary>
        public int AudioBitrate
        {
            get => audioBitrate;
            set
            {
                if (audioBitrate != value)
                {
                    audioBitrate = value;
                    OnPropertyChanged(nameof(AudioBitrate));
                }
            }
        }

        /// <summary>
        /// 帧率（fps）
        /// </summary>
        public int FrameRate
        {
            get => frameRate;
            set
            {
                if (frameRate != value)
                {
                    frameRate = value;
                    OnPropertyChanged(nameof(FrameRate));
                }
            }
        }

        /// <summary>
        /// 视频宽度
        /// </summary>
        public int VideoWidth
        {
            get => videoWidth;
            set
            {
                if (videoWidth != value)
                {
                    videoWidth = value;
                    OnPropertyChanged(nameof(VideoWidth));
                }
            }
        }

        /// <summary>
        /// 视频高度
        /// </summary>
        public int VideoHeight
        {
            get => videoHeight;
            set
            {
                if (videoHeight != value)
                {
                    videoHeight = value;
                    OnPropertyChanged(nameof(VideoHeight));
                }
            }
        }

        /// <summary>
        /// 视频编码器
        /// </summary>
        public VCodecId VideoCodec
        {
            get => videoCodec;
            set
            {
                if (videoCodec != value)
                {
                    videoCodec = value;
                    OnPropertyChanged(nameof(VideoCodec));
                }
            }
        }

        /// <summary>
        /// 音频编码器
        /// </summary>
        public ACodecId AudioCodec
        {
            get => audioCodec;
            set
            {
                if (audioCodec != value)
                {
                    audioCodec = value;
                    OnPropertyChanged(nameof(AudioCodec));
                }
            }
        }

        /// <summary>
        /// GOP大小
        /// </summary>
        public int GopSize
        {
            get => gopSize;
            set
            {
                if (gopSize != value)
                {
                    gopSize = value;
                    OnPropertyChanged(nameof(GopSize));
                }
            }
        }

        /// <summary>
        /// 是否启用B帧
        /// </summary>
        public bool EnableBFrames
        {
            get => enableBFrames;
            set
            {
                if (enableBFrames != value)
                {
                    enableBFrames = value;
                    OnPropertyChanged(nameof(EnableBFrames));
                }
            }
        }

        /// <summary>
        /// 录制模式（本地文件/网络推流）
        /// </summary>
        public RecordMode RecordMode
        {
            get => recordMode;
            set
            {
                if (recordMode != value)
                {
                    recordMode = value;
                    OnPropertyChanged(nameof(RecordMode));
                }
            }
        }

        /// <summary>
        /// 推流地址（RTMP/RTSP等）
        /// </summary>
        public string PushUrl
        {
            get => pushUrl;
            set
            {
                if (pushUrl != value)
                {
                    pushUrl = value;
                    OnPropertyChanged(nameof(PushUrl));
                }
            }
        }

        /// <summary>
        /// 本地保存路径
        /// </summary>
        public string LocalPath
        {
            get => localPath;
            set
            {
                if (localPath != value)
                {
                    localPath = value;
                    OnPropertyChanged(nameof(LocalPath));
                }
            }
        }

        /// <summary>
        /// 复用器类型
        /// </summary>
        public MuxerType MuxerType
        {
            get => muxerType;
            set
            {
                if (muxerType != value)
                {
                    muxerType = value;
                    OnPropertyChanged(nameof(MuxerType));
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
    }
}