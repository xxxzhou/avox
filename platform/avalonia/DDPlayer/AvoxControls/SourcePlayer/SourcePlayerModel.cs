using AvoxCommon;
using AvoxNet;
using System;
using System.ComponentModel;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Windows.Input;

namespace AvoxControls.SourcePlayer
{
    /// <summary>
    /// SourcePlayer 的 Model 实现
    /// 封装 AvoxCommon.SourcePlayer，用于直出的设备源（摄像头、麦克风等）
    /// </summary>
    public class SourcePlayerModel : PlayerModel
    {
        private AvoxCommon.SourcePlayer player;
        private IVideoSource videoSource;
        private IAudioSource audioSource;

        /// <summary>
        /// 构造函数
        /// </summary>
        public SourcePlayerModel()
        {
            player = new AvoxCommon.SourcePlayer();
            // 订阅状态变更事件
            player.OnStateChanged += OnPlayerStateChanged;
            // 初始化命令
            OpenCommand = new ActionCommand(() => Open());
            CloseCommand = new ActionCommand(() => Close());
        }

        /// <summary>
        /// 重写视频渲染器
        /// </summary>
        public override ISurfaceRender WindowRender => player?.GetSurfaceRender();

        /// <summary>
        /// 重写音频渲染器
        /// </summary>
        public override IAudioRender AudioRender => player?.GetAudioRender();

        public override IMediaMuxer MediaMuxer
        {
            get
            {
                return player?.GetMuxer();
            }
        }

        /// <summary>
        /// SourcePlayer 实例
        /// </summary>
        public AvoxCommon.SourcePlayer Player
        {
            get => player;
        }

        /// <summary>
        /// 是否正在播放
        /// </summary>
        public bool IsPlaying => player?.IsPlaying ?? false;

        /// <summary>
        /// 是否已暂停
        /// </summary>
        public bool IsPaused => player?.IsPaused ?? false;

        /// <summary>
        /// 是否已停止
        /// </summary>
        public bool IsStopped => player?.IsStopped ?? false;

        /// <summary>
        /// 是否可以打开（必须有视频源或音频源，且未在播放）
        /// </summary>
        public bool CanOpen => (videoSource != null || audioSource != null) && !IsPlaying;

        /// <summary>
        /// 是否有视频源
        /// </summary>
        public bool HasVideoSource => videoSource != null;

        /// <summary>
        /// 是否有音频源
        /// </summary>
        public bool HasAudioSource => audioSource != null;

        /// <summary>
        /// 源信息
        /// </summary>
        public ISourceInfo SourceInfo => Player?.GetSourceInfo();

        /// <summary>
        /// 定义命令
        /// </summary>
        public ICommand OpenCommand { get; }
        public ICommand CloseCommand { get; }

        /// <summary>
        /// 播放器状态变更处理
        /// </summary>
        private void OnPlayerStateChanged(PlayerState preState, PlayerState state)
        {
            if(preState == state || CurrentState == state)
            {
                return;
            }
            // 更新基类的 CurrentState
            CurrentState = state;

            // 通知属性变化
            OnPropertyChanged(nameof(IsPlaying));
            OnPropertyChanged(nameof(IsPaused));
            OnPropertyChanged(nameof(IsStopped));
            OnPropertyChanged(nameof(CanOpen));

            // 调用基类状态处理（更新 SourceInfoModel）
            base.OnStateChanged(preState, state);
        }

        /// <summary>
        /// 获取源信息（供基类调用）
        /// </summary>
        protected override ISourceInfo GetSourceInfo()
        {
            return player?.GetSourceInfo();
        }

        /// <summary>
        /// 打开源播放
        /// </summary>
        public void Open()
        {
            if (!CanOpen)
            {
                AvoxWrapper.logMsg(LogLevel.warn, "无法打开：没有选择设备或已在播放中");
                return;
            }
            try
            {
                // 打开播放
                player.Open();
                AvoxWrapper.logMsg(LogLevel.info, "源播放已打开");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"源播放打开失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 关闭源播放
        /// </summary>
        public void Close()
        {
            if (Player != null && IsPlaying)
            {
                StopRecording();
                Player.Close();
                AvoxWrapper.logMsg(LogLevel.info, "源播放已关闭");
            }
        }

        /// <summary>
        /// 设置音频源
        /// </summary>
        public void SetAudioSource(IAudioSource source)
        {
            audioSource = source;
            player.SetAudioSource(audioSource);
            OnPropertyChanged(nameof(HasAudioSource));
            OnPropertyChanged(nameof(CanOpen));

            if (source != null)
            {
                AvoxWrapper.logMsg(LogLevel.info, $"已选择音频源: {source.getDeviceName()}");
            }
        }

        /// <summary>
        /// 设置视频源
        /// </summary>
        public void SetVideoSource(IVideoSource source)
        {
            videoSource = source;
            player.SetVideoSource(videoSource);
            OnPropertyChanged(nameof(HasVideoSource));
            OnPropertyChanged(nameof(CanOpen));

            if (source != null)
            {
                AvoxWrapper.logMsg(LogLevel.info, $"已选择视频源: {source.getDeviceName()}");
            }
        }

        /// <summary>
        /// 获取当前视频源
        /// </summary>
        public IVideoSource GetVideoSource() => videoSource;

        /// <summary>
        /// 获取当前音频源
        /// </summary>
        public IAudioSource GetAudioSource() => audioSource;       

        /// <summary>
        /// 释放资源
        /// </summary>
        public void Dispose()
        {
            if (player != null)
            {
                player.OnStateChanged -= OnPlayerStateChanged;
                player.Dispose();
                player = null;
            }
            videoSource = null;
            audioSource = null;
        }
    }
}
