using Avalonia.Controls;
using AvoxCommon;
using AvoxNet;
using System;

namespace AvoxControls.MediaPlayer
{
    /// <summary>
    /// TestMediaView - 容器视图，管理主界面和设置界面之间的导航
    /// 使用覆盖层方式避免 NativeControlHost 窗口脱离问题
    /// </summary>
    public partial class TestMediaView : UserControl
    {
        /// <summary>
        /// 构造函数
        /// </summary>
        public TestMediaView()
        {
            InitializeComponent();

            // 绑定主界面的设置导航请求
            MainView.NavigateToSettingsRequested += OnNavigateToSettingsRequested;

            // 绑定设置界面的事件
            SettingsView.SettingsConfirmed += OnSettingsConfirmed;
            SettingsView.SettingsCancelled += OnSettingsCancelled;
        }

        /// <summary>
        /// 导航到设置界面
        /// </summary>
        private void OnNavigateToSettingsRequested(object sender, EventArgs e)
        {
            // 先隐藏播放器视图（避免 NativeControlHost 窗口在最上层）
            MainView.SetMediaPlayerVisible(false);

            // 显示设置界面（覆盖层方式）
            SettingsOverlay.IsVisible = true;
        }

        /// <summary>
        /// 设置确认 - 保存并返回主界面
        /// </summary>
        private void OnSettingsConfirmed(object sender, PlaybackConfig config)
        {
            ConfigManager.Instance.SaveConfig();
            NavigateBackToMain();
        }

        /// <summary>
        /// 设置取消 - 返回主界面
        /// </summary>
        private void OnSettingsCancelled(object sender, EventArgs e)
        {
            NavigateBackToMain();
        }

        /// <summary>
        /// 返回主界面
        /// </summary>
        private void NavigateBackToMain()
        {
            // 隐藏设置界面
            SettingsOverlay.IsVisible = false;

            // 恢复播放器视图显示
            MainView.SetMediaPlayerVisible(true);
        }

        /// <summary>
        /// 释放资源 - 解绑事件避免内存泄漏
        /// </summary>
        public void Dispose()
        {
            try
            {
                // 解绑事件
                MainView.NavigateToSettingsRequested -= OnNavigateToSettingsRequested;
                SettingsView.SettingsConfirmed -= OnSettingsConfirmed;
                SettingsView.SettingsCancelled -= OnSettingsCancelled;
             // 清理渲染器引用
                MainView?.MediaPlayerViewControl?.Cleanup();
                // 释放 MediaPlayerModel 的资源（关键：释放 C++ 底层资源）
                MainView?.MediaPlayerViewControl?.MediaModel?.Dispose();   
            }
            catch (Exception ex)
            {
                AvoxWrapper.logMsg(LogLevel.warn, $"TestMediaView.Dispose 出错: {ex.Message}");
            }
        }
    }
}
