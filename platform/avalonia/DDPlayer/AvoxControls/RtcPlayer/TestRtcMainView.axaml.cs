using Avalonia.Controls;
using System;

namespace AvoxControls.RtcPlayer
{
    /// <summary>
    /// RTC 测试主视图
    /// 包含 RtcPlayerView 和 SourceInfoView
    /// </summary>
    public partial class TestRtcMainView : UserControl
    {
        /// <summary>
        /// 导航到设置界面请求事件
        /// </summary>
        public event EventHandler NavigateToSettingsRequested;

        public TestRtcMainView()
        {
            InitializeComponent();

            // 绑定RtcPlayerView的设置导航请求事件
            RtcPlayerViewControl.NavigateToSettingsRequested += (sender, e) =>
            {
                NavigateToSettingsRequested?.Invoke(this, e);
            };
        }
    }
}
