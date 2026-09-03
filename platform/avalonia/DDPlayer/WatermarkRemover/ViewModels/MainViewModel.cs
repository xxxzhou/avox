using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using MsBox.Avalonia;

namespace WatermarkRemover.ViewModels;

public class ImageItem
{
    public string InputPath { get; set; } = "";
    public string OutputPath { get; set; } = "";
    public string FileName { get; set; } = "";
    public string Status { get; set; } = "待处理";
}

public class MainViewModel
{
    private readonly WatermarkService service = new();

    public bool IsInitialized { get; set; }
    public string StatusText { get; set; } = "未初始化";
    public bool IsProcessing { get; set; }
    public int ProcessedCount { get; set; }
    public int TotalCount { get; set; }
    public Bitmap? CurrentImage { get; set; }
    public string? CurrentImagePath { get; set; }
    public int MaskDilate { get; set; } = 20;

    public ObservableCollection<ImageItem> ImageList { get; } = new();

    public event Action? StateChanged;

    public async Task AutoInitAsync()
    {
        StatusText = "正在初始化...";
        StateChanged?.Invoke();

        await Task.Run(async () =>
        {
            try
            {
                Console.WriteLine("[WatermarkService] 尝试CPU初始化...");
                var result = service.Init(modelLevel: 1, useGPU: false);
                Console.WriteLine($"[WatermarkService] 初始化结果: {result}");

                IsInitialized = result == 0;
                StatusText = IsInitialized ? "初始化完成 (CPU)" : "初始化失败";
            }
            catch (Exception ex)
            {
                StatusText = $"初始化异常: {ex.Message}";
                Console.WriteLine($"[MainViewModel] 异常: {ex}");
            }
        });

        StateChanged?.Invoke();
    }

    public async Task SelectFilesAsync(object window)
    {
        if (window is Avalonia.Controls.Window win)
        {
            var files = await win.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "选择图片文件",
                AllowMultiple = true,
                FileTypeFilter = new[] { new FilePickerFileType("图片文件") { Patterns = new[] { "*.jpg", "*.jpeg", "*.png", "*.bmp" } } }
            });

            foreach (var file in files)
            {
                AddImage(file.Path.LocalPath);
            }

            // 显示第一张图
            if (ImageList.Count > 0)
            {
                ShowImage(ImageList[0].InputPath);
            }

            UpdateCounts();
            StateChanged?.Invoke();
        }
    }

    public async Task SelectFolderAsync(object window)
    {
        if (window is Avalonia.Controls.Window win)
        {
            var folders = await win.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
            {
                Title = "选择图片文件夹",
                AllowMultiple = false
            });

            if (folders.Count > 0)
            {
                var folderPath = folders[0].Path.LocalPath;
                var extensions = new[] { ".jpg", ".jpeg", ".png", ".bmp" };
                foreach (var ext in extensions)
                {
                    foreach (var file in Directory.GetFiles(folderPath, $"*{ext}"))
                    {
                        AddImage(file);
                    }
                }

                // 显示第一张图
                if (ImageList.Count > 0)
                {
                    ShowImage(ImageList[0].InputPath);
                }

                UpdateCounts();
                StateChanged?.Invoke();
            }
        }
    }

    private void AddImage(string path)
    {
        if (ImageList.Any(x => x.InputPath == path)) return;
        ImageList.Add(new ImageItem { InputPath = path, FileName = Path.GetFileName(path), Status = "待处理" });
    }

    private void UpdateCounts()
    {
        TotalCount = ImageList.Count;
        ProcessedCount = ImageList.Count(x => x.Status == "完成");
    }

    public void ClearList()
    {
        ImageList.Clear();
        CurrentImage = null;
        CurrentImagePath = null;
        UpdateCounts();
        StateChanged?.Invoke();
    }

    public void ShowImage(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                using var stream = File.OpenRead(path);
                CurrentImage = new Bitmap(stream);
                CurrentImagePath = path;
                StateChanged?.Invoke();
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine($"加载图片失败: {ex.Message}");
        }
    }

    public async Task ProcessAsync(object window)
    {
        if (!IsInitialized)
        {
            var box = MessageBoxManager.GetMessageBoxStandard("提示", "请先初始化");
            await box.ShowAsync();
            return;
        }

        if (ImageList.Count == 0)
        {
            var box = MessageBoxManager.GetMessageBoxStandard("提示", "请先添加图片");
            await box.ShowAsync();
            return;
        }

        // 更新膨胀参数
        service.MaskDilate = MaskDilate;

        IsProcessing = true;
        ProcessedCount = 0;
        StateChanged?.Invoke();

        await Task.Run(async () =>
        {
            var sw = Stopwatch.StartNew();

            foreach (var item in ImageList)
            {
                if (item.Status == "完成") continue;

                item.Status = "处理中...";
                StatusText = $"正在处理: {item.FileName}";
                StateChanged?.Invoke();

                try
                {
                    var dir = Path.GetDirectoryName(item.InputPath) ?? "";
                    var fileName = Path.GetFileNameWithoutExtension(item.InputPath);
                    var ext = Path.GetExtension(item.InputPath);

                    // 先检测并保存mask
                    var maskPath = Path.Combine(dir, $"{fileName}_mask.png");
                    service.DetectAndSaveMask(item.InputPath, maskPath);

                    // 再进行修复
                    item.OutputPath = Path.Combine(dir, $"{fileName}_clean{ext}");
                    var result = service.ProcessImage(item.InputPath, item.OutputPath);
                    item.Status = result ? "完成" : "失败";

                    // 处理完后显示修复后的图
                    if (result && File.Exists(item.OutputPath))
                    {
                        Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                        {
                            ShowImage(item.OutputPath);
                        });
                    }
                }
                catch (Exception ex)
                {
                    item.Status = $"错误: {ex.Message}";
                }

                ProcessedCount++;
                UpdateCounts();
                StateChanged?.Invoke();
            }

            sw.Stop();
            IsProcessing = false;
            var gpuMode = service.UsingGPU ? "GPU" : "CPU";
            StatusText = $"处理完成: {ProcessedCount}/{TotalCount} ({sw.Elapsed.TotalSeconds:F1}s, {gpuMode}, 膨胀{MaskDilate}px)";
            StateChanged?.Invoke();
        });

        var successCount = ImageList.Count(x => x.Status == "完成");
        var box2 = MessageBoxManager.GetMessageBoxStandard("完成", $"处理完成\n成功: {successCount}\n失败: {TotalCount - successCount}");
        await box2.ShowAsync();
    }
}
