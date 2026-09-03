using Avalonia.Controls;
using AvoxCommon;
using AvoxNet;
using System;

namespace AvoxControls.SourcePlayer
{
    /// <summary>
    /// TestSourceView - 容器视图，管理主界面和设置界面之间的导航
    /// </summary>
    public partial class TestSourceView : UserControl
    {
        /// <summary>
        /// 构造函数
        /// </summary>
        public TestSourceView()
        {
            InitializeComponent();

            // 绑定主界面的设置导航请求
            MainView.SourcePlayerViewControl.NavigateToSettingsRequested += OnNavigateToSettingsRequested;

            // 绑定设置界面的事件
            SettingsView.SettingsConfirmed += OnSettingsConfirmed;
            SettingsView.SettingsCancelled += OnSettingsCancelled;
        }

        /// <summary>
        /// 导航到设置界面
        /// </summary>
        private void OnNavigateToSettingsRequested(object sender, EventArgs e)
        {
            // 隐藏视频渲染，避免原生窗口遮挡设置界面
            MainView.SourcePlayerViewControl.HideRender();

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

            // 恢复显示视频渲染
            MainView.SourcePlayerViewControl.ShowRender();
        }

        /// <summary>
        /// 释放资源 - 解绑事件避免内存泄漏
        /// </summary>
        public void Dispose()
        {
            try
            {
                // 解绑事件
                if (MainView != null)
                {
                    MainView.SourcePlayerViewControl.NavigateToSettingsRequested -= OnNavigateToSettingsRequested;
                    SettingsView.SettingsConfirmed -= OnSettingsConfirmed;
                    SettingsView.SettingsCancelled -= OnSettingsCancelled;
                }
                // 清理渲染器引用
                MainView?.SourcePlayerViewControl?.Cleanup();
                // 释放 SourcePlayerModel 的资源
                MainView?.SourcePlayerViewControl?.SourcePlayerModel?.Dispose();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error disposing TestSourceView: {ex.Message}");
            }

            AvoxWrapper.logMsg(LogLevel.info, "TestSourceView.Dispose - 资源释放完成");
        }
    }
}
