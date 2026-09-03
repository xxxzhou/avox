using AvoxNet;
using System.ComponentModel;

namespace AvoxCommon
{
    /// <summary>
    /// WebRTC模式
    /// </summary>
    public enum RtcMode
    {
        /// <summary>
        /// 推流
        /// </summary>
        Push = 0,
        /// <summary>
        /// 拉流
        /// </summary>
        Pull = 1
    }

    /// <summary>
    /// WebRTC相关配置
    /// </summary>
    public class RtcConfig : INotifyPropertyChanged
    {
        private RtcRollType rtcRollType = RtcRollType.offer;
        private RtcMode rtcMode = RtcMode.Pull;
        private string pushUri = "";
        private string pullUri = "";
        private bool useSSE = false;
        private string sseServer = "";
        private string sseToken = "";
        private string sseId = "";
        private string turnServer = "";
        private string turnUsername = "";
        private string turnPassword = "";

        /// <summary>
        /// WebRTC角色类型
        /// </summary>
        public RtcRollType RtcRollType
        {
            get => rtcRollType;
            set
            {
                if (rtcRollType != value)
                {
                    rtcRollType = value;
                    OnPropertyChanged(nameof(RtcRollType));
                }
            }
        }

        /// <summary>
        /// WebRTC模式（推流/拉流）
        /// </summary>
        public RtcMode RtcMode
        {
            get => rtcMode;
            set
            {
                if (rtcMode != value)
                {
                    rtcMode = value;
                    OnPropertyChanged(nameof(RtcMode));
                }
            }
        }

        /// <summary>
        /// 推流URI
        /// </summary>
        public string PushUri
        {
            get => pushUri;
            set
            {
                if (pushUri != value)
                {
                    pushUri = value;
                    OnPropertyChanged(nameof(PushUri));
                }
            }
        }

        /// <summary>
        /// 拉流URI
        /// </summary>
        public string PullUri
        {
            get => pullUri;
            set
            {
                if (pullUri != value)
                {
                    pullUri = value;
                    OnPropertyChanged(nameof(PullUri));
                }
            }
        }

        /// <summary>
        /// 是否使用SSE信令
        /// </summary>
        public bool UseSSE
        {
            get => useSSE;
            set
            {
                if (useSSE != value)
                {
                    useSSE = value;
                    OnPropertyChanged(nameof(UseSSE));
                }
            }
        }

        /// <summary>
        /// SSE服务地址
        /// </summary>
        public string SseServer
        {
            get => sseServer;
            set
            {
                if (sseServer != value)
                {
                    sseServer = value;
                    OnPropertyChanged(nameof(SseServer));
                }
            }
        }

        /// <summary>
        /// SSE认证令牌
        /// </summary>
        public string SseToken
        {
            get => sseToken;
            set
            {
                if (sseToken != value)
                {
                    sseToken = value;
                    OnPropertyChanged(nameof(SseToken));
                }
            }
        }

        /// <summary>
        /// SSE客户端ID
        /// </summary>
        public string SseId
        {
            get => sseId;
            set
            {
                if (sseId != value)
                {
                    sseId = value;
                    OnPropertyChanged(nameof(SseId));
                }
            }
        }

        /// <summary>
        /// TURN服务地址
        /// </summary>
        public string TurnServer
        {
            get => turnServer;
            set
            {
                if (turnServer != value)
                {
                    turnServer = value;
                    OnPropertyChanged(nameof(TurnServer));
                }
            }
        }

        /// <summary>
        /// TURN服务用户名
        /// </summary>
        public string TurnUsername
        {
            get => turnUsername;
            set
            {
                if (turnUsername != value)
                {
                    turnUsername = value;
                    OnPropertyChanged(nameof(TurnUsername));
                }
            }
        }

        /// <summary>
        /// TURN服务密码
        /// </summary>
        public string TurnPassword
        {
            get => turnPassword;
            set
            {
                if (turnPassword != value)
                {
                    turnPassword = value;
                    OnPropertyChanged(nameof(TurnPassword));
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