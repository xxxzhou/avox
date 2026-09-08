using AvoxCommon;
using AvoxNet;
using System;
using System.ComponentModel;
using System.Windows.Input;
using System.Threading.Tasks;

namespace AvoxControls.RtcPlayer
{
    /// <summary>
    /// RTC 播放器控制器
    /// 管理共享的 RtcPlayer 实例，协调拉流和推流
    /// </summary>
    public class RtcPlayerController : IDisposable
    {
        private AvoxCommon.RtcPlayer player;
        private RtcSdpAgentOb sdpAgentOb;
        // 内置信令agent(ZLM HTTP自动交换, 拉/推流Open时创建; addOb挂载, 换agent先摘旧的)
        private IRtcEventOb sdpAgent;
        private RtcRollType rollType = RtcRollType.offer;
        private string localSdp = "";
        private string remoteSdp = "";
        private SSEClient sseClient;

        /// <summary>
        /// 拉流 Model（显示远端）
        /// </summary>
        public RtcPullModel PullModel { get; private set; }

        /// <summary>
        /// 推流 Model（预览本地）
        /// </summary>
        public RtcPushModel PushModel { get; private set; }

        /// <summary>
        /// SSE 客户端（用于 WebRTC 信令）
        /// </summary>
        public SSEClient SSEClient => sseClient;

        /// <summary>
        /// 构造函数
        /// </summary>
        public RtcPlayerController()
        {
            player = new AvoxCommon.RtcPlayer();
            sdpAgentOb = new RtcSdpAgentOb(this);
            player.AddRtcEventOb(sdpAgentOb);

            // 创建 SSE 客户端
            sseClient = new SSEClient(this);
            sseClient.ConnectionStatusChanged += OnConnectionStatusChanged;
            sseClient.ErrorOccurred += OnSSEError;

            // 订阅本地 SDP 和 ICE 候选事件
            OnLocalSdp += OnLocalSdpGenerated;
            OnIceCandidate += OnIceCandidateGenerated;

            // 创建推拉流 Model，共享同一个 player
            PullModel = new RtcPullModel(player);
            PushModel = new RtcPushModel(player);

            // 初始化命令
            OpenCommand = new ActionCommand(async () => await OpenAsync());
            CloseCommand = new ActionCommand(() => Close());
            SetRemoteSdpCommand = new ActionCommand<string>(sdp => RemoteSdp = sdp);
            GetLocalSdpCommand = new ActionCommand(() => GetLocalSdp());
        }

        /// <summary>
        /// 打开 RTC 连接（异步）
        /// </summary>
        public async Task OpenAsync()
        {
            await Task.Run(async () =>
            {
                try
                {
                    // 获取当前配置
                    var config = ConfigManager.Instance.Config;
                    var rtcMode = config.Rtc.RtcMode;
                    var pushUri = config.Rtc.PushUri;
                    var pullUri = config.Rtc.PullUri;

                    // 设置角色类型
                    player.SetRollType(rollType);

                    // 初始化 SSE 客户端（如果启用）
                    if (config.Rtc.UseSSE)
                    {
                        await InitializeSSEClient();
                    }

                    // 根据模式执行不同的逻辑
                    if (rtcMode == RtcMode.Pull)
                    {
                        // 拉流逻辑（参考 webrtcplaytest.cpp）
                        if (!string.IsNullOrWhiteSpace(pullUri))
                        {
                            // 创建内置信令 Agent（ZLM HTTP 自动交换）
                            var agent = AvoxWrapper.createZlTestSdpAgent(player.Player, pullUri);
                            if (agent != null)
                            {
                                ReplaceSdpAgent(agent);
                                AvoxWrapper.logMsg(LogLevel.info, $"拉流 SDP Agent 创建成功: {pullUri}");
                            }
                        }
                    }
                    else if (rtcMode == RtcMode.Push)
                    {
                        // 推流逻辑（参考 webrtcpull.cpp）
                        if (!string.IsNullOrWhiteSpace(pushUri))
                        {
                            // 创建内置信令 Agent（ZLM HTTP 自动交换）
                            var agent = AvoxWrapper.createZlTestSdpAgent(player.Player, pushUri);
                            if (agent != null)
                            {
                                ReplaceSdpAgent(agent);
                                AvoxWrapper.logMsg(LogLevel.info, $"推流 SDP Agent 创建成功: {pushUri}");
                            }
                        }
                    }

                    // 打开连接
                    bool result = player.Open();
                    AvoxWrapper.logMsg(LogLevel.info, $"RTC 连接打开: {result}, 模式: {rtcMode}");
                }
                catch (Exception ex)
                {
                    AvoxWrapper.logMsg(LogLevel.error, $"RTC 连接打开失败: {ex.Message}");
                }
            });
        }

        /// <summary>
        /// 打开 RTC 连接
        /// </summary>
        public void Open()
        {
            _ = OpenAsync();
        }

        /// <summary>
        /// RtcPlayer 实例（内部使用）
        /// </summary>
        internal AvoxCommon.RtcPlayer Player => player;

        /// <summary>
        /// RTC 角色类型
        /// </summary>
        public RtcRollType RollType
        {
            get => rollType;
            set
            {
                if (rollType != value)
                {
                    rollType = value;
                    player?.SetRollType(rollType);
                    OnPropertyChanged(nameof(RollType));
                }
            }
        }

        /// <summary>
        /// RTC 角色类型列表
        /// </summary>
        public Array RollTypes => Enum.GetValues(typeof(RtcRollType));

        /// <summary>
        /// 本地 SDP（生成后通过事件通知）
        /// </summary>
        public string LocalSdp
        {
            get => localSdp;
            private set
            {
                if (localSdp != value)
                {
                    localSdp = value;
                    OnPropertyChanged(nameof(LocalSdp));
                    OnLocalSdp?.Invoke(localSdp);
                }
            }
        }

        /// <summary>
        /// 远端 SDP
        /// </summary>
        public string RemoteSdp
        {
            get => remoteSdp;
            set
            {
                if (remoteSdp != value)
                {
                    remoteSdp = value;
                    OnPropertyChanged(nameof(RemoteSdp));
                    // 自动设置远端 SDP
                    if (player != null && !string.IsNullOrEmpty(value))
                    {
                        player.SetRemoteSdp(value);
                    }
                }
            }
        }

        /// <summary>
        /// 是否正在播放
        /// </summary>
        public bool IsPlaying => player?.IsPlaying ?? false;

        /// <summary>
        /// 当前连接状态
        /// </summary>
        public PlayerState CurrentState => player?.CurrentState ?? PlayerState.none;

        /// <summary>
        /// 定义命令
        /// </summary>
        public ICommand OpenCommand { get; }
        public ICommand CloseCommand { get; }
        public ICommand SetRemoteSdpCommand { get; }
        public ICommand GetLocalSdpCommand { get; }

        /// <summary>
        /// SDP 事件
        /// </summary>
        public event Action<string> OnLocalSdp;

        /// <summary>
        /// ICE 候选事件
        /// </summary>
        public event Action<string, string, int> OnIceCandidate;

        /// <summary>
        /// 属性变更事件
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// 触发 ICE 候选事件（内部使用）
        /// </summary>
        internal void RaiseIceCandidate(string candidate, string mid, int mlineIndex)
        {
            OnIceCandidate?.Invoke(candidate, mid, mlineIndex);
        }

        /// <summary>
        /// 替换内置信令 Agent (先摘旧 agent, 防观察者列表堆积)
        /// </summary>
        private void ReplaceSdpAgent(IRtcEventOb ob)
        {
            if (sdpAgent != null)
            {
                player.RemoveRtcEventOb(sdpAgent);
                sdpAgent.Dispose();
            }
            sdpAgent = ob;
            player.AddRtcEventOb(ob);
        }

        /// <summary>
        /// 初始化 SSE 客户端
        /// </summary>
        private async Task InitializeSSEClient()
        {
            var config = ConfigManager.Instance.Config;
            if (config.Rtc.UseSSE && !string.IsNullOrWhiteSpace(config.Rtc.SseServer))
            {
                try
                {
                    await sseClient.ConnectAsync(
                        config.Rtc.SseServer,
                        config.Rtc.SseToken,
                        config.Rtc.SseId
                    );
                    AvoxWrapper.logMsg(LogLevel.info, "SSE 客户端初始化成功");
                }
                catch (Exception ex)
                {
                    AvoxWrapper.logMsg(LogLevel.error, $"SSE 客户端初始化失败: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// 关闭 RTC 连接
        /// </summary>
        public void Close()
        {
            if (player != null)
            {
                player.Close();
                localSdp = "";
                remoteSdp = "";
                OnPropertyChanged(nameof(LocalSdp));
                OnPropertyChanged(nameof(RemoteSdp));
                AvoxWrapper.logMsg(LogLevel.info, "RTC 连接关闭");
            }
        }

        /// <summary>
        /// 设置视频源（用于推流预览）
        /// </summary>
        public void SetVideoSource(IVideoSource videoSource)
        {
            try
            {
                player.SetVideoSource(videoSource);
                // 通知 PushModel 属性变化（通过刷新绑定）
                OnPropertyChanged(nameof(PushModel));
                AvoxWrapper.logMsg(LogLevel.info, "设置 RTC 视频源成功");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置 RTC 视频源失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 设置音频源（用于推流）
        /// </summary>
        public void SetAudioSource(IAudioSource audioSource)
        {
            try
            {
                player.SetAudioSource(audioSource);
                AvoxWrapper.logMsg(LogLevel.info, "设置 RTC 音频源成功");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"设置 RTC 音频源失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 获取本地 SDP
        /// </summary>
        public string GetLocalSdp()
        {
            localSdp = player.GetLocalSdp() ?? "";
            OnPropertyChanged(nameof(LocalSdp));
            OnLocalSdp?.Invoke(localSdp);
            return localSdp;
        }

        /// <summary>
        /// 添加 ICE 候选
        /// </summary>
        public void AddIceCandidate(string candidate, string mid, int mlineIndex)
        {
            try
            {
                player?.AddIceCandidate(candidate, mid, mlineIndex);
                AvoxWrapper.logMsg(LogLevel.info, $"添加 ICE 候选: mid={mid}, index={mlineIndex}");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"添加 ICE 候选失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 触发属性变更通知
        /// </summary>
        protected virtual void OnPropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        /// <summary>
        /// 连接状态变更处理
        /// </summary>
        /// <param name="connected">是否连接</param>
        private void OnConnectionStatusChanged(bool connected)
        {
            AvoxWrapper.logMsg(LogLevel.info, $"SSE 连接状态: {connected}");
            OnPropertyChanged(nameof(IsConnected));
        }

        /// <summary>
        /// SSE 错误处理
        /// </summary>
        /// <param name="ex">错误信息</param>
        private void OnSSEError(Exception ex)
        {
            AvoxWrapper.logMsg(LogLevel.error, $"SSE 错误: {ex.Message}");
        }

        /// <summary>
        /// 本地 SDP 生成处理
        /// </summary>
        /// <param name="sdp">本地 SDP</param>
        private async void OnLocalSdpGenerated(string sdp)
        {
            AvoxWrapper.logMsg(LogLevel.info, "本地 SDP 生成，准备发送...");
            try
            {
                // 发送 SDP 到信令服务器
                var data = new
                {
                    sdp = sdp,
                    type = RollType == RtcRollType.offer ? "offer" : "answer"
                };

                string method = RollType == RtcRollType.offer ? "offer" : "answer";
                await sseClient.SendSignalAsync(method, data);
                AvoxWrapper.logMsg(LogLevel.info, $"{method} 发送成功");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"发送 SDP 失败: {ex.Message}");
            }
        }

        /// <summary>
        /// ICE 候选生成处理
        /// </summary>
        /// <param name="candidate">候选信息</param>
        /// <param name="mid">媒体 ID</param>
        /// <param name="mlineIndex">媒体行索引</param>
        private async void OnIceCandidateGenerated(string candidate, string mid, int mlineIndex)
        {
            AvoxWrapper.logMsg(LogLevel.info, "ICE 候选生成，准备发送...");
            try
            {
                // 发送 ICE 候选到信令服务器
                var data = new
                {
                    candidate = candidate,
                    sdpMid = mid,
                    sdpMLineIndex = mlineIndex
                };

                await sseClient.SendSignalAsync("candidate", data);
                AvoxWrapper.logMsg(LogLevel.info, "ICE 候选发送成功");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"发送 ICE 候选失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 连接到 SSE 信令服务器
        /// </summary>
        /// <param name="signalServer">信令服务器地址</param>
        /// <param name="token">认证令牌</param>
        /// <param name="selfId">自身 ID</param>
        /// <returns></returns>
        public async Task ConnectToSignalServerAsync(string signalServer, string token, string selfId)
        {
            await sseClient.ConnectAsync(signalServer, token, selfId);
        }

        /// <summary>
        /// 是否连接到信令服务器
        /// </summary>
        public bool IsConnected => sseClient?.IsConnected ?? false;

        /// <summary>
        /// 释放资源
        /// </summary>
        public void Dispose()
        {
            PullModel?.Dispose();
            PushModel?.Dispose();

            if (sdpAgent != null)
            {
                player?.RemoveRtcEventOb(sdpAgent);
                sdpAgent.Dispose();
                sdpAgent = null;
            }
            if (player != null && sdpAgentOb != null)
            {
                player.RemoveRtcEventOb(sdpAgentOb);
            }

            if (sseClient != null)
            {
                sseClient.Dispose();
                sseClient = null;
            }

            if (player != null)
            {
                player.Dispose();
                player = null;
            }
        }
    }

    /// <summary>
    /// RTC 事件观察者实现 (自定义SSE信令: 本地SDP/ICE经此转发信令服务器)
    /// </summary>
    public class RtcSdpAgentOb : IRtcEventOb
    {
        private RtcPlayerController controller;

        public RtcSdpAgentOb(RtcPlayerController ctrl)
        {
            controller = ctrl;
        }

        /// <summary>
        /// 本地 SDP 生成回调
        /// </summary>
        public override void onLocalSdp(string sdp)
        {
            // 通过 Controller 通知上层
            controller?.GetLocalSdp();
        }

        /// <summary>
        /// ICE 候选生成回调
        /// </summary>
        public override void onIceCandidate(string candidate, string mid, int mlineIndex)
        {
            controller?.RaiseIceCandidate(candidate, mid, mlineIndex);
        }
    }
}