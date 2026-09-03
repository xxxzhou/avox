using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text.Json.Serialization;

namespace AvoxCommon
{
    /// <summary>
    /// 历史记录项（统一用于媒体源、设备、RTC）
    /// </summary>
    public class HistoryItem
    {
        /// <summary>
        /// 标识符（媒体源路径/设备ID/SDP服务器地址）
        /// </summary>
        public string Id { get; set; }

        /// <summary>
        /// 显示名称
        /// </summary>
        public string Name { get; set; }

        /// <summary>
        /// 最后访问时间
        /// </summary>
        public DateTime LastAccessed { get; set; }

        /// <summary>
        /// 构造函数
        /// </summary>
        public HistoryItem()
        {
            LastAccessed = DateTime.Now;
        }

        /// <summary>
        /// 构造函数
        /// </summary>
        /// <param name="id">标识符</param>
        /// <param name="name">显示名称</param>
        public HistoryItem(string id, string name = null)
        {
            Id = id;
            Name = name;
            LastAccessed = DateTime.Now;
        }
    }

    /// <summary>
    /// 历史记录列表，处理通用逻辑
    /// </summary>
    public class HistoryList : INotifyPropertyChanged
    {
        [JsonIgnore]  // 私有字段不参与序列化
        private List<HistoryItem> items = new List<HistoryItem>();
        private int maxItems = 10;
        private bool autoSort = true;

        /// <summary>
        /// 最大历史记录项数
        /// </summary>
        public int MaxItems
        {
            get => maxItems;
            set
            {
                if (maxItems != value && value > 0)
                {
                    maxItems = value;
                    TrimExcessItems();
                    OnPropertyChanged(nameof(MaxItems));
                }
            }
        }

        /// <summary>
        /// 是否自动排序（选择时自动将项移到最前面）
        /// </summary>
        public bool AutoSort
        {
            get => autoSort;
            set
            {
                if (autoSort != value)
                {
                    autoSort = value;
                    OnPropertyChanged(nameof(AutoSort));
                }
            }
        }

        /// <summary>
        /// 历史记录项列表（用于 JSON 序列化/反序列化）
        /// </summary>
        [JsonPropertyName("items")]
        public List<HistoryItem> SerializableItems
        {
            get => items;
            set => items = value ?? new List<HistoryItem>();
        }

        /// <summary>
        /// 历史记录项列表（运行时访问）
        /// </summary>
        [JsonIgnore]  // 这个属性不参与 JSON 序列化
        public IReadOnlyList<HistoryItem> Items
        {
            get
            {
                if (autoSort)
                {
                    return items.OrderByDescending(x => x.LastAccessed).ToList();
                }
                else
                {
                    return items.ToList();
                }
            }
        }

        /// <summary>
        /// 添加或更新历史记录项
        /// </summary>
        /// <param name="item">要添加的项</param>
        public void AddOrUpdate(HistoryItem item)
        {
            if (item == null || string.IsNullOrEmpty(item.Id))
            {
                return;
            }

            var existingItem = items.FirstOrDefault(x => x.Id == item.Id);
            if (existingItem != null)
            {
                // 更新现有项的时间和名称
                existingItem.LastAccessed = DateTime.Now;
                existingItem.Name = item.Name;
            }
            else
            {
                // 添加新项
                items.Add(item);
            }

            TrimExcessItems();
            OnPropertyChanged(nameof(Items));
        }

        /// <summary>
        /// 添加或更新历史记录项
        /// </summary>
        /// <param name="id">标识符</param>
        /// <param name="name">显示名称</param>
        public void AddOrUpdate(string id, string name = null)
        {
            AddOrUpdate(new HistoryItem(id, name));
        }

        /// <summary>
        /// 根据ID删除历史记录项
        /// </summary>
        /// <param name="id">项的ID</param>
        public void Remove(string id)
        {
            var item = items.FirstOrDefault(x => x.Id == id);
            if (item != null)
            {
                items.Remove(item);
                OnPropertyChanged(nameof(Items));
            }
        }

        /// <summary>
        /// 清空所有历史记录
        /// </summary>
        public void Clear()
        {
            if (items.Count > 0)
            {
                items.Clear();
                OnPropertyChanged(nameof(Items));
            }
        }

        /// <summary>
        /// 移动指定ID的项到最前面（更新访问时间）
        /// </summary>
        /// <param name="id">项的ID</param>
        public void MoveToTop(string id)
        {
            var item = items.FirstOrDefault(x => x.Id == id);
            if (item != null)
            {
                item.LastAccessed = DateTime.Now;
                OnPropertyChanged(nameof(Items));
            }
        }

        /// <summary>
        /// 修剪超过最大数量的项（删除最旧的）
        /// </summary>
        private void TrimExcessItems()
        {
            if (items.Count > maxItems)
            {
                var sortedByTime = items.OrderBy(x => x.LastAccessed).ToList();
                var toRemoveCount = items.Count - maxItems;
                for (int i = 0; i < toRemoveCount; i++)
                {
                    items.Remove(sortedByTime[i]);
                }
            }
        }

        /// <summary>
        /// 属性变更通知事件
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// 触发属性变更通知
        /// </summary>
        /// <param name="propertyName">属性名称</param>
        protected virtual void OnPropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }

    /// <summary>
    /// 历史记录配置
    /// </summary>
    public class HistoryConfig : INotifyPropertyChanged
    {
        [JsonIgnore]  // 私有字段不参与序列化
        private HistoryList mediaHistory = new HistoryList();
        [JsonIgnore]
        private HistoryList audioDeviceHistory = new HistoryList();
        [JsonIgnore]
        private HistoryList videoDeviceHistory = new HistoryList();
        [JsonIgnore]
        private HistoryList rtcHistory = new HistoryList();
        private int maxHistoryItems = 10;

        public HistoryConfig()
        {
            // 设置自动排序选项
            mediaHistory.AutoSort = true;
            audioDeviceHistory.AutoSort = false;
            videoDeviceHistory.AutoSort = false;
            rtcHistory.AutoSort = true;
        }

        /// <summary>
        /// 最大历史记录项数
        /// </summary>
        public int MaxHistoryItems
        {
            get => maxHistoryItems;
            set
            {
                if (maxHistoryItems != value)
                {
                    maxHistoryItems = value;
                    mediaHistory.MaxItems = value;
                    audioDeviceHistory.MaxItems = value;
                    videoDeviceHistory.MaxItems = value;
                    rtcHistory.MaxItems = value;
                    OnPropertyChanged(nameof(MaxHistoryItems));
                }
            }
        }

        /// <summary>
        /// 媒体源历史记录（包含本地文件和网络流）
        /// </summary>
        public IReadOnlyList<HistoryItem> MediaHistory => mediaHistory.Items;

        /// <summary>
        /// 音频设备历史记录
        /// </summary>
        public IReadOnlyList<HistoryItem> AudioDeviceHistory => audioDeviceHistory.Items;

        /// <summary>
        /// 视频设备历史记录
        /// </summary>
        public IReadOnlyList<HistoryItem> VideoDeviceHistory => videoDeviceHistory.Items;

        /// <summary>
        /// RTC历史记录
        /// </summary>
        public IReadOnlyList<HistoryItem> RtcHistory => rtcHistory.Items;

        /// <summary>
        /// 媒体源历史记录列表（用于绑定和 JSON 反序列化）
        /// </summary>
        [JsonPropertyName("mediaHistoryList")]
        public HistoryList MediaHistoryList
        {
            get => mediaHistory;
            set => mediaHistory = value ?? new HistoryList();
        }

        /// <summary>
        /// 音频设备历史记录列表（用于绑定和 JSON 反序列化）
        /// </summary>
        [JsonPropertyName("audioDeviceHistoryList")]
        public HistoryList AudioDeviceHistoryList
        {
            get => audioDeviceHistory;
            set => audioDeviceHistory = value ?? new HistoryList();
        }

        /// <summary>
        /// 视频设备历史记录列表（用于绑定和 JSON 反序列化）
        /// </summary>
        [JsonPropertyName("videoDeviceHistoryList")]
        public HistoryList VideoDeviceHistoryList
        {
            get => videoDeviceHistory;
            set => videoDeviceHistory = value ?? new HistoryList();
        }

        /// <summary>
        /// RTC历史记录列表（用于绑定和 JSON 反序列化）
        /// </summary>
        [JsonPropertyName("rtcHistoryList")]
        public HistoryList RtcHistoryList
        {
            get => rtcHistory;
            set => rtcHistory = value ?? new HistoryList();
        }

        /// <summary>
        /// 添加或更新媒体源历史记录
        /// </summary>
        /// <param name="path">媒体路径</param>
        /// <param name="name">显示名称</param>
        public void AddMedia(string path, string name = null)
        {
            mediaHistory.AddOrUpdate(path, name);
            OnPropertyChanged(nameof(MediaHistory));
        }

        /// <summary>
        /// 添加或更新音频设备历史记录
        /// </summary>
        /// <param name="deviceId">设备ID</param>
        /// <param name="name">设备名称</param>
        public void AddAudioDevice(string deviceId, string name)
        {
            audioDeviceHistory.AddOrUpdate(deviceId, name);
            OnPropertyChanged(nameof(AudioDeviceHistory));
        }

        /// <summary>
        /// 添加或更新视频设备历史记录
        /// </summary>
        /// <param name="deviceId">设备ID</param>
        /// <param name="name">设备名称</param>
        public void AddVideoDevice(string deviceId, string name)
        {
            videoDeviceHistory.AddOrUpdate(deviceId, name);
            OnPropertyChanged(nameof(VideoDeviceHistory));
        }

        /// <summary>
        /// 添加或更新RTC历史记录
        /// </summary>
        /// <param name="serverUrl">SDP交换服务器地址</param>
        /// <param name="name">服务器名称</param>
        public void AddRtc(string serverUrl, string name)
        {
            rtcHistory.AddOrUpdate(serverUrl, name);
            OnPropertyChanged(nameof(RtcHistory));
        }

        /// <summary>
        /// 添加或更新设备历史记录（根据设备类型）
        /// </summary>
        /// <param name="deviceId">设备ID</param>
        /// <param name="name">设备名称</param>
        /// <param name="isAudio">是否为音频设备</param>
        public void AddDevice(string deviceId, string name, bool isAudio)
        {
            if (isAudio)
            {
                AddAudioDevice(deviceId, name);
            }
            else
            {
                AddVideoDevice(deviceId, name);
            }
        }

        /// <summary>
        /// 移动媒体源历史记录项到最前面
        /// </summary>
        /// <param name="path">媒体路径</param>
        public void MoveMediaToTop(string path)
        {
            mediaHistory.MoveToTop(path);
            OnPropertyChanged(nameof(MediaHistory));
        }

        /// <summary>
        /// 移动音频设备历史记录项到最前面
        /// </summary>
        /// <param name="deviceId">设备ID</param>
        public void MoveAudioDeviceToTop(string deviceId)
        {
            audioDeviceHistory.MoveToTop(deviceId);
            OnPropertyChanged(nameof(AudioDeviceHistory));
        }

        /// <summary>
        /// 移动视频设备历史记录项到最前面
        /// </summary>
        /// <param name="deviceId">设备ID</param>
        public void MoveVideoDeviceToTop(string deviceId)
        {
            videoDeviceHistory.MoveToTop(deviceId);
            OnPropertyChanged(nameof(VideoDeviceHistory));
        }

        /// <summary>
        /// 移动RTC历史记录项到最前面
        /// </summary>
        /// <param name="serverUrl">服务器地址</param>
        public void MoveRtcToTop(string serverUrl)
        {
            rtcHistory.MoveToTop(serverUrl);
            OnPropertyChanged(nameof(RtcHistory));
        }

        /// <summary>
        /// 删除媒体源历史记录项
        /// </summary>
        /// <param name="path">媒体路径</param>
        public void RemoveMedia(string path)
        {
            mediaHistory.Remove(path);
            OnPropertyChanged(nameof(MediaHistory));
        }

        /// <summary>
        /// 删除音频设备历史记录项
        /// </summary>
        /// <param name="deviceId">设备ID</param>
        public void RemoveAudioDevice(string deviceId)
        {
            audioDeviceHistory.Remove(deviceId);
            OnPropertyChanged(nameof(AudioDeviceHistory));
        }

        /// <summary>
        /// 删除视频设备历史记录项
        /// </summary>
        /// <param name="deviceId">设备ID</param>
        public void RemoveVideoDevice(string deviceId)
        {
            videoDeviceHistory.Remove(deviceId);
            OnPropertyChanged(nameof(VideoDeviceHistory));
        }

        /// <summary>
        /// 删除设备历史记录项（根据设备类型）
        /// </summary>
        /// <param name="deviceId">设备ID</param>
        /// <param name="isAudio">是否为音频设备</param>
        public void RemoveDevice(string deviceId, bool isAudio)
        {
            if (isAudio)
            {
                RemoveAudioDevice(deviceId);
            }
            else
            {
                RemoveVideoDevice(deviceId);
            }
        }

        /// <summary>
        /// 删除RTC历史记录项
        /// </summary>
        /// <param name="serverUrl">服务器地址</param>
        public void RemoveRtc(string serverUrl)
        {
            rtcHistory.Remove(serverUrl);
            OnPropertyChanged(nameof(RtcHistory));
        }

        /// <summary>
        /// 清空所有历史记录
        /// </summary>
        public void ClearAll()
        {
            mediaHistory.Clear();
            audioDeviceHistory.Clear();
            videoDeviceHistory.Clear();
            rtcHistory.Clear();
            OnPropertyChanged(nameof(MediaHistory));
            OnPropertyChanged(nameof(AudioDeviceHistory));
            OnPropertyChanged(nameof(VideoDeviceHistory));
            OnPropertyChanged(nameof(RtcHistory));
        }

        /// <summary>
        /// 属性变更通知事件
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// 触发属性变更通知
        /// </summary>
        /// <param name="propertyName">属性名称</param>
        protected virtual void OnPropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
