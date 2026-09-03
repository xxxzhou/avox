using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using AvoxCommon;
using System;
using System.Windows.Input;

namespace AvoxControls
{
    public partial class SettingsWindow : Window
    {
        public bool IsConfirmed { get; private set; }
        
        public SettingsWindow()
        {
            InitializeComponent();
            DataContext = ConfigManager.Instance.Config;

            // 初始化单选按钮状态
            var config = ConfigManager.Instance.Config;
            if (config != null)
            {
                FFmpegRadio.IsChecked = config.Playback.IoPlan == AvoxNet.IoPlan.ffmpeg;
                ZLMediaKitRadio.IsChecked = config.Playback.IoPlan == AvoxNet.IoPlan.zlmediakit;
            }
        }
        
        private void OKButton_Click(object sender, RoutedEventArgs e)
        {
            IsConfirmed = true;
            ConfigManager.Instance.SaveConfig();
            Close();
        }
        
        private void CancelButton_Click(object sender, RoutedEventArgs e)
        {
            IsConfirmed = false;
            Close();
        }
        
        private void FFmpegRadio_Checked(object sender, RoutedEventArgs e)
        {
            var config = ConfigManager.Instance.Config;
            if (config != null && (bool)FFmpegRadio.IsChecked)
            {
                config.Playback.IoPlan = AvoxNet.IoPlan.ffmpeg;
            }
        }
        
        private void FFmpegRadio_Unchecked(object sender, RoutedEventArgs e)
        {
            // 不需要处理取消选中事件，因为单选按钮组会自动处理
        }
        
        private void ZLMediaKitRadio_Checked(object sender, RoutedEventArgs e)
        {
            var config = ConfigManager.Instance.Config;
            if (config != null && (bool)ZLMediaKitRadio.IsChecked)
            {
                config.Playback.IoPlan = AvoxNet.IoPlan.zlmediakit;
            }
        }
        
        private void ZLMediaKitRadio_Unchecked(object sender, RoutedEventArgs e)
        {
            // 不需要处理取消选中事件，因为单选按钮组会自动处理
        }        

    }
}