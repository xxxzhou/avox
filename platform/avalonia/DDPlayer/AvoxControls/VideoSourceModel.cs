using AvoxNet;
using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace AvoxControls
{
    /// <summary>
    /// 视频设备信息，用于 UI 显示
    /// </summary>
    public class VideoDeviceInfo
    {
        public string Name { get; }
        public string Id { get; }
        public IVideoSource Source { get; }

        public VideoDeviceInfo(IVideoSource source)
        {
            Source = source;
            Name = source?.getDeviceName() ?? "Unknown";
            Id = source?.getDeviceId() ?? "";
        }

        public override string ToString() => Name;
    }

    /// <summary>
    /// 视频源设备管理 Model
    /// 用于 UI 显示所有视频设备并记录选择的设备
    /// </summary>
    public class VideoSourceModel : INotifyPropertyChanged
    {
        private IVideoManager videoManager = null;
        private VideoDeviceInfo selectedDevice = null;

        /// <summary>
        /// 所有可用视频设备列表
        /// </summary>
        public ObservableCollection<VideoDeviceInfo> Devices { get; } = new ObservableCollection<VideoDeviceInfo>();

        /// <summary>
        /// 当前选中的设备
        /// </summary>
        public VideoDeviceInfo SelectedDevice
        {
            get => selectedDevice;
            set
            {
                if (selectedDevice != value)
                {
                    selectedDevice = value;
                    OnPropertyChanged(nameof(SelectedDevice));
                    OnPropertyChanged(nameof(HasSelectedDevice));
                }
            }
        }

        /// <summary>
        /// 是否有选中的设备
        /// </summary>
        public bool HasSelectedDevice => selectedDevice != null;

        /// <summary>
        /// 选中的设备源（直接返回 IVideoSource）
        /// </summary>
        public IVideoSource SelectedSource => selectedDevice?.Source;

        /// <summary>
        /// 设备数量
        /// </summary>
        public int DeviceCount => Devices.Count;

        public VideoSourceModel()
        {
            InitializeManager();
        }

        private void InitializeManager()
        {
            VDeviceSdk deviceSdk = VDeviceSdk.win_capture;
#if ANDROID
            deviceSdk = VDeviceSdk.and_ndkcamer2;
#endif
#if IOS
            deviceSdk = VDeviceSdk.ios_avf;
#endif
            videoManager = AvoxWrapper.getVideoManager(deviceSdk);
            RefreshDevices();
        }

        /// <summary>
        /// 刷新设备列表
        /// </summary>
        public void RefreshDevices()
        {
            // 保存当前选中设备的 ID
            string selectedId = selectedDevice?.Id;

            // 清空列表
            Devices.Clear();

            // 刷新底层设备列表
            videoManager?.refreshDevices();

            // 重新获取设备
            int count = videoManager?.getDeviceCount() ?? 0;
            for (int i = 0; i < count; i++)
            {
                var source = videoManager.getDevice(i);
                if (source != null)
                {
                    Devices.Add(new VideoDeviceInfo(source));
                }
            }

            // 尝试恢复选中状态
            if (!string.IsNullOrEmpty(selectedId))
            {
                foreach (var device in Devices)
                {
                    if (device.Id == selectedId)
                    {
                        SelectedDevice = device;
                        break;
                    }
                }
            }

            OnPropertyChanged(nameof(DeviceCount));
        }

        /// <summary>
        /// 根据设备 ID 选择设备
        /// </summary>
        /// <param name="deviceId">设备 ID</param>
        /// <returns>是否选择成功</returns>
        public bool SelectDeviceById(string deviceId)
        {
            foreach (var device in Devices)
            {
                if (device.Id == deviceId)
                {
                    SelectedDevice = device;
                    return true;
                }
            }
            return false;
        }

        /// <summary>
        /// 根据设备名称选择设备
        /// </summary>
        /// <param name="deviceName">设备名称</param>
        /// <returns>是否选择成功</returns>
        public bool SelectDeviceByName(string deviceName)
        {
            foreach (var device in Devices)
            {
                if (device.Name == deviceName)
                {
                    SelectedDevice = device;
                    return true;
                }
            }
            return false;
        }

        /// <summary>
        /// 清除选中状态
        /// </summary>
        public void ClearSelection()
        {
            SelectedDevice = null;
        }

        public event PropertyChangedEventHandler PropertyChanged;

        protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
