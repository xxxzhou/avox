using System;
using System.Runtime.InteropServices;
using Avalonia.Controls;
using AvoxCommon;
using AvoxControls;
using AvoxNet;
using Avalonia.Interactivity;

namespace AvaPlayer.Views
{
    public partial class MainView : UserControl
    {
        // 字段确保 AvoxLog 不会被 GC 回收
        private AvoxLog avoxLog = new AvoxLog();

        // 当前显示的视图（动态加载模式使用）
        private object currentView;

        // 模式开关：true=动态加载模式（节省内存），false=预加载模式（快速切换）
        private bool useDynamicLoading = false;

        public MainView()
        {
            AvoxWrapper.setLogObserver(avoxLog);
            InitializeComponent();

            if (useDynamicLoading)
            {
                // 动态加载模式：默认显示媒体播放器
                ShowMediaPlayerView();
            }
            else
            {
                // 预加载模式：加载所有视图，只显示第一个
                InitializePreloadedViews();
            }
        }

        /// <summary>
        /// 初始化预加载视图
        /// </summary>
        private void InitializePreloadedViews()
        {
            // 显示预加载容器，隐藏动态加载容器
            PreloadedContainer.IsVisible = true;
            DynamicContent.IsVisible = false;
            
            // 默认显示媒体播放器
            MediaPlayerView.IsVisible = true;
            SourcePlayerView.IsVisible = false;
            RtcPlayerView.IsVisible = false;
            
            AvoxWrapper.logMsg(LogLevel.info, "使用预加载模式：所有视图已加载");
        }

        /// <summary>
        /// 视图模式切换处理
        /// </summary>
        private void OnViewModeChanged(object sender, RoutedEventArgs e)
        {
            UpdateView();
        }

        /// <summary>
        /// 更新显示的视图
        /// </summary>
        private void UpdateView()
        {
            if (useDynamicLoading)
            {
                UpdateDynamicView();
            }
            else
            {
                UpdatePreloadedView();
            }
        }

        /// <summary>
        /// 更新预加载视图的可见性
        /// </summary>
        private void UpdatePreloadedView()
        {
            if (MediaPlayerRadio?.IsChecked == true)
            {
                // 切换到媒体播放器
                SourcePlayerView.IsVisible = false;
                RtcPlayerView.IsVisible = false;
                MediaPlayerView.IsVisible = true;
                AvoxWrapper.logMsg(LogLevel.info, "预加载模式：切换到媒体播放器");
            }
            else if (SourcePlayerRadio?.IsChecked == true)
            {
                // 切换到设备源播放器
                MediaPlayerView.IsVisible = false;
                RtcPlayerView.IsVisible = false;
                SourcePlayerView.IsVisible = true;
                AvoxWrapper.logMsg(LogLevel.info, "预加载模式：切换到设备推送");
            }
            else if (RtcPlayerRadio?.IsChecked == true)
            {
                // 切换到 RTC 播放器
                MediaPlayerView.IsVisible = false;
                SourcePlayerView.IsVisible = false;
                RtcPlayerView.IsVisible = true;
                AvoxWrapper.logMsg(LogLevel.info, "预加载模式：切换到 RTC 通话");
            }
        }

        /// <summary>
        /// 更新动态加载视图
        /// </summary>
        private void UpdateDynamicView()
        {
            if (MediaPlayerRadio?.IsChecked == true)
            {
                ShowMediaPlayerView();
            }
            else if (SourcePlayerRadio?.IsChecked == true)
            {
                ShowSourcePlayerView();
            }
            else if (RtcPlayerRadio?.IsChecked == true)
            {
                ShowRtcPlayerView();
            }
        }

        /// <summary>
        /// 显示媒体播放器视图（动态加载模式）
        /// </summary>
        private void ShowMediaPlayerView()
        {
            // 确保使用动态加载模式
            if (!useDynamicLoading)
            {
                // 切换到动态加载模式
                SwitchToDynamicMode();
            }

            // 释放当前视图的资源
            DisposeCurrentView();

            // 创建新的媒体播放器视图
            var mediaView = new AvoxControls.MediaPlayer.TestMediaView();
            DynamicContent.Content = mediaView;
            currentView = mediaView;

            AvoxWrapper.logMsg(LogLevel.info, "动态加载模式：切换到媒体播放器视图");
        }

        /// <summary>
        /// 显示设备推送视图（动态加载模式）
        /// </summary>
        private void ShowSourcePlayerView()
        {
            // 确保使用动态加载模式
            if (!useDynamicLoading)
            {
                // 切换到动态加载模式
                SwitchToDynamicMode();
            }

            // 释放当前视图的资源
            DisposeCurrentView();

            // 创建新的设备推送视图
            var sourceView = new AvoxControls.SourcePlayer.TestSourceView();
            DynamicContent.Content = sourceView;
            currentView = sourceView;

            AvoxWrapper.logMsg(LogLevel.info, "动态加载模式：切换到设备推送视图");
        }

        /// <summary>
        /// 显示 RTC 通话视图（动态加载模式）
        /// </summary>
        private void ShowRtcPlayerView()
        {
            // 确保使用动态加载模式
            if (!useDynamicLoading)
            {
                // 切换到动态加载模式
                SwitchToDynamicMode();
            }

            // 释放当前视图的资源
            DisposeCurrentView();

            // 创建新的 RTC 通话视图
            var rtcView = new AvoxControls.RtcPlayer.TestRtcView();
            DynamicContent.Content = rtcView;
            currentView = rtcView;

            AvoxWrapper.logMsg(LogLevel.info, "动态加载模式：切换到 RTC 通话视图");
        }

        /// <summary>
        /// 切换到动态加载模式
        /// </summary>
        private void SwitchToDynamicMode()
        {
            if (useDynamicLoading) return;
            
            useDynamicLoading = true;
            
            // 释放预加载视图的资源
            DisposePreloadedViews();
            
            // 切换到动态加载容器
            PreloadedContainer.IsVisible = false;
            DynamicContent.IsVisible = true;
            
            AvoxWrapper.logMsg(LogLevel.info, "已切换到动态加载模式");
        }

        /// <summary>
        /// 释放预加载视图的资源
        /// </summary>
        private void DisposePreloadedViews()
        {
            try
            {
                MediaPlayerView?.Dispose();
                SourcePlayerView?.Dispose();
                RtcPlayerView?.Dispose();

                AvoxWrapper.logMsg(LogLevel.info, "预加载视图资源已释放");
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.warn, $"释放预加载视图资源时出错: {ex.Message}");
            }
        }

        /// <summary>
        /// 释放当前视图的资源（动态加载模式使用）
        /// </summary>
        private void DisposeCurrentView()
        {
            try
            {
                if (currentView != null)
                {
                    // 媒体播放器视图
                    if (currentView is AvoxControls.MediaPlayer.TestMediaView mediaView)
                    {
                        mediaView.Dispose();
                        AvoxWrapper.logMsg(LogLevel.info, "媒体播放器资源已释放");
                    }
                    // 设备推送视图
                    else if (currentView is AvoxControls.SourcePlayer.TestSourceView sourceView)
                    {
                        sourceView.Dispose();
                        AvoxWrapper.logMsg(LogLevel.info, "设备推送资源已释放");
                    }
                    // RTC 通话视图
                    else if (currentView is AvoxControls.RtcPlayer.TestRtcView rtcView)
                    {
                        rtcView.Dispose();
                        AvoxWrapper.logMsg(LogLevel.info, "RTC 通话资源已释放");
                    }
                }
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.warn, $"释放视图资源时出错: {ex.Message}");
            }

            currentView = null;
            DynamicContent.Content = null;
        }

        protected override void OnUnloaded(RoutedEventArgs e)
        {
            base.OnUnloaded(e);
            
            // 清理所有资源
            if (useDynamicLoading)
            {
                DisposeCurrentView();
            }
            else
            {
                DisposePreloadedViews();
            }
            
            AvoxWrapper.logMsg(LogLevel.info, "MainView 已卸载，所有资源已清理");
        }

        /// <summary>
        /// 获取或设置是否使用动态加载模式
        /// true = 动态加载模式（节省内存，切换时重新加载）
        /// false = 预加载模式（快速切换，但占用更多内存）
        /// </summary>
        public bool UseDynamicLoading
        {
            get => useDynamicLoading;
            set
            {
                if (useDynamicLoading != value)
                {
                    useDynamicLoading = value;

                    if (value)
                    {
                        // 切换到动态加载模式
                        SwitchToDynamicMode();

                        // 根据当前选中的 RadioButton 显示对应视图
                        if (MediaPlayerRadio?.IsChecked == true)
                        {
                            ShowMediaPlayerView();
                        }
                        else if (SourcePlayerRadio?.IsChecked == true)
                        {
                            ShowSourcePlayerView();
                        }
                        else if (RtcPlayerRadio?.IsChecked == true)
                        {
                            ShowRtcPlayerView();
                        }
                    }
                    else
                    {
                        // 切换到预加载模式
                        DisposeCurrentView();
                        currentView = null;
                        DynamicContent.Content = null;
                        DynamicContent.IsVisible = false;
                        PreloadedContainer.IsVisible = true;

                        // 根据当前选中的 RadioButton 显示对应视图
                        UpdatePreloadedView();

                        AvoxWrapper.logMsg(LogLevel.info, "已切换到预加载模式");
                    }
                }
            }
        }
    }
}
