using AvoxCommon;
using AvoxNet;
using System;
using System.Windows.Input;

namespace AvoxControls.RtcPlayer
{
    /// <summary>
    /// RTC 拉流 Model
    /// 用于显示远端视频、播放远端音频
    /// </summary>
    public class RtcPullModel : PlayerModel
    {
        private AvoxCommon.RtcPlayer player;

        /// <summary>
        /// 构造函数
        /// </summary>
        /// <param name="rtcPlayer">共享的 RtcPlayer 实例</param>
        public RtcPullModel(AvoxCommon.RtcPlayer rtcPlayer)
        {
            player = rtcPlayer ?? throw new ArgumentNullException(nameof(rtcPlayer));
            // 订阅状态变更事件
            player.OnStateChanged += OnPlayerStateChanged;
        }

        /// <summary>
        /// 远端视频渲染器（拉流显示）
        /// </summary>
        public override ISurfaceRender WindowRender => player?.GetRemoteSurfaceRender();

        /// <summary>
        /// 远端音频渲染器
        /// </summary>
        public override IAudioRender AudioRender => player?.GetRemoteAudioRender();

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
        /// 播放器状态变更处理
        /// </summary>
        private void OnPlayerStateChanged(PlayerState preState, PlayerState state)
        {
            // 更新基类的 CurrentState
            CurrentState = state;

            OnPropertyChanged(nameof(IsPlaying));
            OnPropertyChanged(nameof(IsPaused));
            OnPropertyChanged(nameof(IsStopped));

            // 调用基类状态处理（更新 SourceInfoModel）
            base.OnStateChanged(preState, state);
        }

        /// <summary>
        /// 获取源信息（供基类调用）- 远端源信息
        /// </summary>
        protected override ISourceInfo GetSourceInfo()
        {
            return player?.GetRemoteSourceInfo();
        }

        /// <summary>
        /// 释放资源
        /// </summary>
        public void Dispose()
        {
            if (player != null)
            {
                player.OnStateChanged -= OnPlayerStateChanged;
            }
        }
    }
}
