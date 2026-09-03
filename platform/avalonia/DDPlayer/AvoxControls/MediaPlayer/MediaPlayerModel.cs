using AvoxCommon;
using AvoxControls;
using AvoxNet;
using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Windows.Input;

namespace AvoxControls.MediaPlayer
{
    /// <summary>
    /// MediaPlayer 的 Model 实现
    /// 封装 AvoxCommon.MediaPlayer，用于需要解码的媒体文件、直播流等
    /// </summary>
    public class MediaPlayerModel : PlayerModel
    {
        private AvoxCommon.MediaPlayer player;
        private double playbackSpeed = 1.0;

        /// <summary>
        /// 构造函数
        /// </summary>
        public MediaPlayerModel()
        {
            player = new AvoxCommon.MediaPlayer();
            // 初始化命令
            PlayCommand = new ActionCommand(() => Play());
            PauseCommand = new ActionCommand(() => Pause());
            CloseCommand = new ActionCommand(() => Close());
            SetSpeedCommand = new ActionCommand<double>(speed => PlaybackSpeed = speed);
            player.OnStateChanged += OnPlayerStateChanged;
        }


        /// <summary>
        /// 重写视频渲染器
        /// </summary>
        public override ISurfaceRender WindowRender
        {
            get
            {
                return player?.GetSurfaceRender();
            }
        }

        /// <summary>
        /// 重写音频渲染器
        /// </summary>
        public override IAudioRender AudioRender
        {
            get
            {
                return player?.GetAudioRender();
            }
        }

        public override IMediaMuxer MediaMuxer
        {
            get
            {
                return player?.GetMuxer();
            }
        }

        /// <summary>
        /// 是否可以开始录制（需要正在播放且未在录制）
        /// </summary>
        public override bool CanRecord => IsPlaying && !IsRecording;

        /// <summary>
        /// MediaPlayer 实例（只读，在构造时确定）
        /// </summary>
        public AvoxCommon.MediaPlayer Player => player;

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
        /// 当前播放速度
        /// </summary>
        public double PlaybackSpeed
        {
            get => playbackSpeed;
            set
            {
                if (playbackSpeed != value)
                {
                    playbackSpeed = value;
                    Player?.SetSpeed(playbackSpeed);
                    OnPropertyChanged(nameof(PlaybackSpeed));
                }
            }
        }

        /// <summary>
        /// 速度选项列表
        /// </summary>
        public double[] SpeedOptions { get; } = new double[] { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0 };

        /// <summary>
        /// 播放进度（0-1）
        /// </summary>
        public double Process => Player?.GetProcess() ?? 0.0;

        /// <summary>
        /// 帧率
        /// </summary>
        public double Fps => Player?.GetFps() ?? 0.0;

        /// <summary>
        /// 视频码率
        /// </summary>
        public double VideoRate => Player?.GetRate(TrackType.video) ?? 0.0;

        /// <summary>
        /// 音频码率
        /// </summary>
        public double AudioRate => Player?.GetRate(TrackType.audio) ?? 0.0;

        /// <summary>
        /// 源信息
        /// </summary>
        public ISourceInfo SourceInfo => Player?.GetSourceInfo();

        /// <summary>
        /// 定义命令
        /// </summary>
        public ICommand PlayCommand { get; }
        public ICommand PauseCommand { get; }
        public ICommand CloseCommand { get; }
        public ICommand SetSpeedCommand { get; }

        /// <summary>
        /// 播放器状态变更处理
        /// </summary>
        private void OnPlayerStateChanged(PlayerState preState, PlayerState state)
        {
            // 更新基类的 CurrentState
            CurrentState = state;

            // 通知属性变化
            OnPropertyChanged(nameof(IsPlaying));
            OnPropertyChanged(nameof(IsPaused));
            OnPropertyChanged(nameof(IsStopped));
            OnPropertyChanged(nameof(CanRecord));

            // 当从 opening 变为其他状态时，SourceInfo 可能已可用，通知 CanSeek 变化
            if (preState == PlayerState.none || state != PlayerState.none)
            {
                OnPropertyChanged(nameof(CanSeek));
            }

            // 关闭/重置时通知 CanSeek
            if (state == PlayerState.none)
            {
                OnPropertyChanged(nameof(CanSeek));
            }

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
        /// 打开媒体文件或流
        /// </summary>
        public void Open(string url, IoPlan ioPlan = IoPlan.ffmpeg, bool hardDecode = true)
        {
            try
            {
                Player.SetIoPlan(ioPlan);
                Player.SetHardDecode(hardDecode);
                Player.GetOption().setInt("mp.delay.ms", ConfigManager.Instance.Config.Playback.BufferSizeMs); 
                Player.Open(url);
                AvoxWrapper.logMsg(LogLevel.info, $"打开媒体: {url}, IO计划: {ioPlan}, 硬解: {hardDecode}");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.error, $"打开媒体失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 播放
        /// </summary>
        public void Play()
        {
            if (Player != null && !Player.IsPlaying)
            {
                Player.Resume();
                AvoxWrapper.logMsg(LogLevel.info, "开始播放");
            }
        }

        /// <summary>
        /// 暂停
        /// </summary>
        public void Pause()
        {
            if (Player != null && Player.IsPlaying)
            {
                Player.Pause();
                AvoxWrapper.logMsg(LogLevel.info, "暂停播放");
            }
        }

        /// <summary>
        /// 关闭
        /// </summary>
        public void Close()
        {
            if (Player != null)
            {
                Player.Close();
                AvoxWrapper.logMsg(LogLevel.info, "关闭媒体");
            }
        }

        // 当状态是ready后,能获取SourceInfo,确定是否能seek
        public bool CanSeek
        {
            get
            {
                if (SourceInfo != null)
                {
                    return SourceInfo.canSeek();
                }
                return false;
            }
        }

        /// <summary>
        /// 当前播放位置（毫秒）
        /// </summary>
        public long Position
        {
            get
            {
                if (player != null)
                {
                    return player.GetPosition();
                }
                return 0;
            }
        }

        /// <summary>
        /// 媒体总时长（毫秒）
        /// </summary>
        public long Duration
        {
            get
            {
                if (player != null)
                {
                    return player.GetDuration();
                }
                return 0;
            }
        }

        /// <summary>
        /// Seek 到指定位置（毫秒）
        /// </summary>
        public void Seek(long pos)
        {
            if (Player != null)
            {
                Player.Seek(pos);
                AvoxWrapper.logMsg(LogLevel.info, $"Seek 到: {pos} ms");
            }
        }

        /// <summary>
        /// 设置播放速度
        /// </summary>
        public void SetSpeed(double speed)
        {
            PlaybackSpeed = speed;
        }

        /// <summary>
        /// 获取选项接口
        /// </summary>
        public IOption GetOption()
        {
            return Player?.GetOption();
        }

        /// <summary>
        /// 获取复用器
        /// </summary>
        public IMediaMuxer GetMuxer()
        {
            return Player?.GetMuxer();
        }

        public void Dispose()
        {
            Player?.Dispose();
        }
    }
}
