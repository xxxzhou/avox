using Avalonia.Controls;
using Avalonia.Collections;
using Avalonia.Interactivity;
using AvoxCommon;
using AvoxControls;
using AvoxNet;
using System;
using Avalonia;

namespace AvoxControls.SourcePlayer
{
    /// <summary>
    /// SourcePlayer View
    /// 设备源播放器的 UI 控制和显示
    /// </summary>
    public partial class SourcePlayerView : UserControl
    {
        private SourcePlayerModel sourcePlayerModel;
        private VideoSourceModel videoSourceModel;
        private AudioSourceModel audioSourceModel;

        public SourcePlayerView()
        {
            InitializeComponent();
            // Model需要放在DataContext设置之前,否则相应Model的引用有问题
            sourcePlayerModel = new SourcePlayerModel();
            videoSourceModel = new VideoSourceModel();
            audioSourceModel = new AudioSourceModel();
            DataContext = this;
            // 绑定渲染器
            RenderView.WindowRender = sourcePlayerModel.WindowRender;

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
                    sourcePlayerModel.SetVideoSource(device.Source);
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
                    sourcePlayerModel.SetAudioSource(device.Source);
                }
            };
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
        /// SourcePlayerModel 实例
        /// </summary>
        public SourcePlayerModel SourcePlayerModel => sourcePlayerModel;

        /// <summary>
        /// VideoSourceModel 实例
        /// </summary>
        public VideoSourceModel VideoSourceModel => videoSourceModel;

        /// <summary>
        /// AudioSourceModel 实例
        /// </summary>
        public AudioSourceModel AudioSourceModel => audioSourceModel;

        /// <summary>
        /// 清理渲染器引用
        /// </summary>
        public void Cleanup()
        {
            // 清除渲染器引用，避免使用已释放的渲染器
            RenderView.WindowRender = null;
        }

        /// <summary>
        /// 隐藏渲染视图 (用于防止原生窗口遮挡设置界面)
        /// </summary>
        public void HideRender()
        {
            if (RenderView != null)
            {
                RenderView.IsVisible = false;
            }
        }

        /// <summary>
        /// 显示渲染视图 (用于从设置界面返回后恢复显示)
        /// </summary>
        public void ShowRender()
        {
            if (RenderView != null)
            {
                RenderView.IsVisible = true;
            }
        }
    }
}
