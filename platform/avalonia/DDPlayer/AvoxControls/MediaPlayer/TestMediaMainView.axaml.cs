using Avalonia.Controls;
using AvoxCommon;
using AvoxNet;
using System;
using Avalonia.Interactivity;

namespace AvoxControls.MediaPlayer
{
    /// <summary>
    /// TestMediaMainView - 主界面
    /// </summary>
    public partial class TestMediaMainView : UserControl
    {
        /// <summary>
        /// 构造函数
        /// </summary>
        public TestMediaMainView()
        {
            InitializeComponent();
            DataContext = this;

            // 绑定历史记录列表
            UrlHistoryTextBox.HistoryList = ConfigManager.Instance.Config.History.MediaHistoryList;
            // 订阅历史记录事件
            UrlHistoryTextBox.HistoryItemSelected += OnHistoryItemSelected;

            // 自动填充最新的历史记录
            var latestHistory = ConfigManager.Instance.Config.History.MediaHistoryList.Items;
            if (latestHistory != null && latestHistory.Count > 0)
            {
                UrlHistoryTextBox.Text = latestHistory[0].Id;
            }
        }

        /// <summary>
        /// 播放器配置
        /// </summary>
        public PlayerConfig Config => ConfigManager.Instance.Config;

        /// <summary>
        /// 确认按钮点击事件
        /// </summary>
        private void OnConfirmButtonClick(object sender, RoutedEventArgs e)
        {
            OpenUrl();
        }

        /// <summary>
        /// URL确认事件
        /// </summary>
        private void OnUrlConfirmed(object sender, RoutedEventArgs e)
        {
            OpenUrl();
        }

        /// <summary>
        /// 历史记录项选中事件
        /// </summary>
        private void OnHistoryItemSelected(object sender, HistoryItemEventArgs e)
        {
            ConfigManager.Instance.Config.History.MoveMediaToTop(e.HistoryItem.Id);
            ConfigManager.Instance.SaveConfig();
        }

        /// <summary>
        /// 打开媒体地址
        /// </summary>
        private void OpenUrl()
        {
            string url = UrlHistoryTextBox.Text?.Trim();
            if (string.IsNullOrEmpty(url))
            {
                System.Diagnostics.Debug.WriteLine("URL 为空，无法打开");
                return;
            }

            try
            {
                if (MediaPlayerViewControl?.MediaModel != null)
                {
                    var ioPlan = ConfigManager.Instance.Config.Playback.IoPlan;
                    var hardDecode = ConfigManager.Instance.Config.Playback.EnableHardwareDecoding;
                    MediaPlayerViewControl.MediaModel.Open(url, ioPlan: ioPlan, hardDecode: hardDecode);

                    ConfigManager.Instance.Config.History.AddMedia(url);
                    ConfigManager.Instance.SaveConfig();
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"打开媒体失败: {ex.Message}");
            }
        }

        /// <summary>
        /// 设置按钮点击事件
        /// </summary>
        private void OnSettingsButtonClick(object sender, RoutedEventArgs e)
        {
            NavigateToSettingsRequested?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>
        /// 导航到设置界面请求事件
        /// </summary>
        public event EventHandler NavigateToSettingsRequested;

        /// <summary>
        /// 设置播放器视图的可见性（用于打开设置界面时隐藏原生窗口）
        /// </summary>
        public void SetMediaPlayerVisible(bool visible)
        {
            if (MediaPlayerViewControl != null)
            {
                MediaPlayerViewControl.IsVisible = visible;
            }
        }
    }
}
