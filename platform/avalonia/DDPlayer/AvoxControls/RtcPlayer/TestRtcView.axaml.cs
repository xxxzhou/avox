using Avalonia.Controls;
using AvoxCommon;
using AvoxNet;
using System;

namespace AvoxControls.RtcPlayer
{
    /// <summary>
    /// RTC 测试视图容器
    /// 管理主界面和设置界面之间的导航
    /// </summary>
    public partial class TestRtcView : UserControl
    {
        /// <summary>
        /// 构造函数
        /// </summary>
        public TestRtcView()
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
            // 先隐藏渲染窗口（避免 NativeControlHost 窗口在最上层）
            MainView.RtcPlayerViewControl.LocalRenderView.IsVisible = false;
            MainView.RtcPlayerViewControl.RemoteRenderView.IsVisible = false;

            // 显示设置界面
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

            // 恢复渲染窗口显示
            MainView.RtcPlayerViewControl.LocalRenderView.IsVisible = true;
            MainView.RtcPlayerViewControl.RemoteRenderView.IsVisible = true;
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
                MainView?.RtcPlayerViewControl?.Cleanup();
                // 释放控制器的资源
                MainView?.RtcPlayerViewControl?.Controller?.Dispose();

            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error disposing TestRtcView: {ex.Message}");
            }

            AvoxWrapper.logMsg(LogLevel.info, "TestRtcView.Dispose - 资源释放完成");
        }
    }
}
