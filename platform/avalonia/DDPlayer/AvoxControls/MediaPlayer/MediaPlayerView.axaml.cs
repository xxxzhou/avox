using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Threading;
using AvoxCommon;
using AvoxControls;
using AvoxNet;
using System;
using System.Xml;

namespace AvoxControls.MediaPlayer
{
    /// <summary>
    /// MediaPlayer View
    /// 媒体播放器的 UI 控制和显示
    /// </summary>
    public partial class MediaPlayerView : UserControl
    {
        private MediaPlayerModel mediaModel;
        private DispatcherTimer positionUpdateTimer;

        public MediaPlayerView()
        {
            InitializeComponent();
            // 创建 Model
            mediaModel = new MediaPlayerModel();
            // 设置 DataContext
            DataContext = mediaModel;
            // 绑定渲染器 - InitializeComponent 后可直接使用 x:Name 定义的控件
            RenderView.WindowRender = mediaModel.WindowRender;

            // 绑定进度条事件
            SetupProgressControlBar();

            // 创建位置更新定时器
            positionUpdateTimer = new DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(500)
            };
            positionUpdateTimer.Tick += OnPositionUpdateTimerTick;

            // 监听状态变化
            mediaModel.PropertyChanged += OnMediaModelPropertyChanged;
        }

        /// <summary>
        /// 设置进度条控制条
        /// </summary>
        private void SetupProgressControlBar()
        {
            if (ProgressControlBar == null) return;

            // 绑定进度条值变化事件（拖动时）
            ProgressControlBar.ProgressValueChanged += (s, e) =>
            {
                // 用户拖动进度条时，更新位置
                if (mediaModel != null && mediaModel.CanSeek)
                {
                    double newPosition = ProgressControlBar.ProgressValue / 100.0 * mediaModel.Duration;
                    string newTimeStr = FormatTime(newPosition);
                    AvoxWrapper.logMsg(LogLevel.info, $"seek time:{newTimeStr}");
                    mediaModel.Seek((long)newPosition);
                }
            };
        }

        /// <summary>
        /// Model 属性变化处理
        /// </summary>
        private void OnMediaModelPropertyChanged(object sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(MediaPlayerModel.CanSeek))
            {
                // CanSeek 变化时，更新进度条显示状态
                UpdateProgressBarVisibility();
            }
            else if (e.PropertyName == nameof(MediaPlayerModel.CurrentState))
            {
                // 状态变化时，控制定时器
                UpdatePositionTimer();
            }
        }

        /// <summary>
        /// 更新进度条显示
        /// </summary>
        private void UpdateProgressBarVisibility()
        {
            Dispatcher.UIThread.InvokeAsync(() =>
            {
                if (ProgressControlBar != null)
                {
                    ProgressControlBar.IsVisible = mediaModel?.CanSeek ?? false;
                }
            });
        }

        /// <summary>
        /// 更新位置定时器
        /// </summary>
        private void UpdatePositionTimer()
        {
            if (mediaModel == null) return;

            var state = mediaModel.CurrentState;
            if (state == PlayerState.playing)
            {
                if (!positionUpdateTimer.IsEnabled)
                {
                    positionUpdateTimer.Start();
                }
            }
            else if (state == PlayerState.none || state == PlayerState.stopped)
            {
                positionUpdateTimer.Stop();   
            }
        }

        /// <summary>
        /// 定时更新位置和进度条显示
        /// </summary>
        private void OnPositionUpdateTimerTick(object sender, EventArgs e)
        {
            if (mediaModel == null) return;
            PlayerState playerState = mediaModel.CurrentState;
            if (playerState == PlayerState.seek)
            {
                return;
            }
            // 更新进度条显示
            UpdateProgressDisplay();
        }

        /// <summary>
        /// 更新进度条显示
        /// </summary>
        private void UpdateProgressDisplay()
        {
            Dispatcher.UIThread.InvokeAsync(() =>
            {
                if (ProgressControlBar == null || mediaModel == null) return;

                double position = mediaModel.Position;
                double duration = mediaModel.Duration;

                // 更新时间显示
                ProgressControlBar.CurrentTimeText = FormatTime(position);
                ProgressControlBar.TotalTimeText = FormatTime(duration);

                // 拖动中不更新进度条值，避免覆盖用户点击/拖动的位置
                if (ProgressControlBar.IsDragging)
                {
                    return;
                }

                // 更新进度条值 (0-100)
                if (duration > 0)
                {
                    ProgressControlBar.ProgressValue = (position / duration) * 100.0;
                }
                else
                {
                    ProgressControlBar.ProgressValue = 0;
                }
            });
        }

        /// <summary>
        /// 格式化时间为 mm:ss 或 hh:mm:ss
        /// </summary>
        private string FormatTime(double milliseconds)
        {
            if (milliseconds < 0) milliseconds = 0;

            TimeSpan time = TimeSpan.FromMilliseconds(milliseconds);
            if (time.TotalHours >= 1)
            {
                return $"{time.Hours:D2}:{time.Minutes:D2}:{time.Seconds:D2}";
            }
            else
            {
                return $"{time.Minutes:D2}:{time.Seconds:D2}";
            }
        }

        /// <summary>
        /// MediaPlayer Model 实例
        /// </summary>
        public MediaPlayerModel MediaModel => mediaModel;

        /// <summary>
        /// 清理渲染器引用
        /// </summary>
        public void Cleanup()
        {
            // 清除渲染器引用，避免使用已释放的渲染器
            RenderView.WindowRender = null;
            
            // 停止定时器
            positionUpdateTimer?.Stop();
        }
    }
}
