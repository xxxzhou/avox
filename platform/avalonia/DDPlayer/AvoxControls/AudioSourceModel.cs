using AvoxNet;
using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace AvoxControls
{
    /// <summary>
    /// 音频设备信息，用于 UI 显示
    /// </summary>
    public class AudioDeviceInfo
    {
        public string Name { get; }
        public string Id { get; }
        public IAudioSource Source { get; }

        public AudioDeviceInfo(IAudioSource source)
        {
            Source = source;
            Name = source?.getDeviceName() ?? "Unknown";
            Id = source?.getDeviceId() ?? "";
        }

        public override string ToString() => Name;
    }

    /// <summary>
    /// 音频源设备管理 Model
    /// 用于 UI 显示所有音频设备并记录选择的设备
    /// </summary>
    public class AudioSourceModel : INotifyPropertyChanged
    {
        private IAudioManager audioManager = null;
        private AudioDeviceInfo selectedDevice = null;

        /// <summary>
        /// 所有可用音频设备列表
        /// </summary>
        public ObservableCollection<AudioDeviceInfo> Devices { get; } = new ObservableCollection<AudioDeviceInfo>();

        /// <summary>
        /// 当前选中的设备
        /// </summary>
        public AudioDeviceInfo SelectedDevice
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
        /// 选中的设备源（直接返回 IAudioSource）
        /// </summary>
        public IAudioSource SelectedSource => selectedDevice?.Source;

        /// <summary>
        /// 设备数量
        /// </summary>
        public int DeviceCount => Devices.Count;

        public AudioSourceModel()
        {
            InitializeManager();
        }

        private void InitializeManager()
        {
            ADeviceSdk deviceSdk = ADeviceSdk.wasapi;
#if ANDROID
            deviceSdk = ADeviceSdk.android;
#elif IOS
            deviceSdk = ADeviceSdk.ios;
#endif
            audioManager = AvoxWrapper.getAudioManager(deviceSdk);
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
            audioManager?.refreshDevices();

            // 重新获取设备
            int count = audioManager?.getDeviceCount() ?? 0;
            for (int i = 0; i < count; i++)
            {
                var source = audioManager.getDevice(i);
                if (source != null)
                {
                    Devices.Add(new AudioDeviceInfo(source));
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
