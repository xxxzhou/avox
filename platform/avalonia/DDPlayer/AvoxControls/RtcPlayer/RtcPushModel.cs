using AvoxCommon;
using AvoxNet;
using System;
using System.Windows.Input;

namespace AvoxControls.RtcPlayer
{
    /// <summary>
    /// RTC 推流 Model
    /// 用于预览本地视频源
    /// </summary>
    public class RtcPushModel : PlayerModel
    {
        private AvoxCommon.RtcPlayer player;

        /// <summary>
        /// 构造函数
        /// </summary>
        /// <param name="rtcPlayer">共享的 RtcPlayer 实例</param>
        public RtcPushModel(AvoxCommon.RtcPlayer rtcPlayer)
        {
            player = rtcPlayer ?? throw new ArgumentNullException(nameof(rtcPlayer));
            // 订阅状态变更事件
            player.OnStateChanged += OnPlayerStateChanged;
        }

        /// <summary>
        /// 本地视频渲染器（预览）
        /// 如果未设置 videoSource，返回 nullptr
        /// </summary>
        public override ISurfaceRender WindowRender => player?.GetLocalSurfaceRender();

        /// <summary>
        /// 推流没有音频渲染（音频是采集不是播放）
        /// </summary>
        public override IAudioRender AudioRender => player?.GetLocalAudioRender();

        /// <summary>
        /// 是否正在播放（推流中）
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
        /// 是否已设置视频源
        /// </summary>
        public bool HasVideoSource => player?.GetLocalSurfaceRender() != null;

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
            OnPropertyChanged(nameof(HasVideoSource));

            // 调用基类状态处理（更新 SourceInfoModel）
            base.OnStateChanged(preState, state);
        }

        /// <summary>
        /// 获取源信息（供基类调用）- 本地源信息
        /// </summary>
        protected override ISourceInfo GetSourceInfo()
        {
            return player?.GetLocalSourceInfo();
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
