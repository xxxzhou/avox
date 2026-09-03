using Avalonia.Controls;
using Avalonia.Collections;
using Avalonia.Interactivity;
using AvoxCommon;
using AvoxControls;
using AvoxNet;
using System;
using Avalonia;

namespace AvoxControls.RtcPlayer
{
    /// <summary>
    /// RtcPlayer View
    /// WebRTC 播放器的 UI 控制和显示
    /// 左边拉流（类似 MediaPlayerView），右边推流（类似 SourcePlayerView）
    /// </summary>
    public partial class RtcPlayerView : UserControl
    {
        private RtcPlayerController controller;
        private VideoSourceModel videoSourceModel;
        private AudioSourceModel audioSourceModel;

        /// <summary>
        /// 导航到设置界面请求事件
        /// </summary>
        public event EventHandler NavigateToSettingsRequested;

        public RtcPlayerView()
        {
            InitializeComponent();
            // android暂时有问题,先不管
            controller = new RtcPlayerController();

            videoSourceModel = new VideoSourceModel();
            audioSourceModel = new AudioSourceModel();
            DataContext = this;

            // 绑定本地渲染器（推流预览）
            LocalRenderView.WindowRender = controller.PushModel.WindowRender;
            // 绑定远端渲染器（拉流显示）
            RemoteRenderView.WindowRender = controller.PullModel.WindowRender;
            // 初始化设备选择
            InitializeDeviceSelectors();
        }

        private void InitializeDeviceSelectors()
        {
            // 绑定视频设备列表
            VideoSourceComboBox.ItemsSource = videoSourceModel.Devices;
            VideoSourceComboBox.DisplayMemberBinding = new Avalonia.Data.Binding("Name");

            // 视频源选择变更
            VideoSourceComboBox.SelectionChanged += (s, e) =>
            {
                if (VideoSourceComboBox.SelectedItem is VideoDeviceInfo device)
                {
                    controller?.SetVideoSource(device.Source);
                }
            };

            // 绑定音频设备列表
            AudioSourceComboBox.ItemsSource = audioSourceModel.Devices;
            AudioSourceComboBox.DisplayMemberBinding = new Avalonia.Data.Binding("Name");

            // 音频源选择变更
            AudioSourceComboBox.SelectionChanged += (s, e) =>
            {
                if (AudioSourceComboBox.SelectedItem is AudioDeviceInfo device)
                {
                    controller?.SetAudioSource(device.Source);
                }
            };
        }

        /// <summary>
        /// RTC 播放器控制器
        /// </summary>
        public RtcPlayerController Controller => controller;

        /// <summary>
        /// 拉流 Model（远端显示）
        /// </summary>
        public RtcPullModel PullModel => controller?.PullModel;

        /// <summary>
        /// 推流 Model（本地预览）
        /// </summary>
        public RtcPushModel PushModel => controller?.PushModel;

        /// <summary>
        /// VideoSourceModel 实例
        /// </summary>
        public VideoSourceModel VideoSourceModel => videoSourceModel;

        /// <summary>
        /// AudioSourceModel 实例
        /// </summary>
        public AudioSourceModel AudioSourceModel => audioSourceModel;

        /// <summary>
        /// 设置按钮点击
        /// </summary>
        private void OnSettingsButtonClick(object sender, RoutedEventArgs e)
        {
            // 触发导航到设置界面的事件
            NavigateToSettingsRequested?.Invoke(this, EventArgs.Empty);
        }

        /// <summary>
        /// 清理渲染器引用
        /// </summary>
        public void Cleanup()
        {
            // 清除渲染器引用，避免使用已释放的渲染器
            LocalRenderView.WindowRender = null;
            RemoteRenderView.WindowRender = null;
        }
    }
}
