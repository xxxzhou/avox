using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using MsBox.Avalonia;

namespace WatermarkRemover.Views;

public class ImageItem
{
    public string InputPath { get; set; } = "";
    public string OutputPath { get; set; } = "";
    public string FileName { get; set; } = "";
    public string Status { get; set; } = "待处理";
}

public partial class MainWindow : Window
{
    private readonly WatermarkService service = new();
    private bool isInitialized;
    private bool isProcessing;

    public MainWindow()
    {
        InitializeComponent();
        ImageDataGrid.ItemsSource = ImageList;

        SelectFilesBtn.Click += OnSelectFiles;
        SelectFolderBtn.Click += OnSelectFolder;
        ClearBtn.Click += OnClear;
        ProcessBtn.Click += OnProcess;
        ImageDataGrid.SelectionChanged += OnSelectionChanged;
        MaskDilateSlider.ValueChanged += OnMaskDilateChanged;

        // 自动初始化
        _ = InitAsync();
    }

    private void OnMaskDilateChanged(object? sender, Avalonia.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        MaskDilateText.Text = $"{(int)e.NewValue}px";
    }

    private System.Collections.ObjectModel.ObservableCollection<ImageItem> ImageList { get; } = new();

    private async System.Threading.Tasks.Task InitAsync()
    {
        StatusText.Text = "正在初始化...";

        await System.Threading.Tasks.Task.Run(() =>
        {
            try
            {
                var result = service.Init(modelLevel: 2, useGPU: false);
                isInitialized = result == 0;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"初始化异常: {ex.Message}");
            }
        });

        StatusText.Text = isInitialized ? "初始化完成 (CPU)" : "初始化失败";
        ProcessBtn.IsEnabled = isInitialized;
    }

    private async void OnSelectFiles(object? sender, RoutedEventArgs e)
    {
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "选择图片文件",
            AllowMultiple = true,
            FileTypeFilter = new[] { new FilePickerFileType("图片") { Patterns = new[] { "*.jpg", "*.jpeg", "*.png", "*.bmp" } } }
        });

        foreach (var file in files)
        {
            AddImage(file.Path.LocalPath);
        }
        if (ImageList.Count > 0) ShowImage(ImageList[0].InputPath);
        UpdateCounts();
    }

    private async void OnSelectFolder(object? sender, RoutedEventArgs e)
    {
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = "选择文件夹" });
        if (folders.Count > 0)
        {
            var path = folders[0].Path.LocalPath;
            foreach (var ext in new[] { "*.jpg", "*.png" })
            {
                foreach (var file in Directory.GetFiles(path, ext))
                {
                    AddImage(file);
                }
            }
            if (ImageList.Count > 0) ShowImage(ImageList[0].InputPath);
            UpdateCounts();
        }
    }

    private void AddImage(string path)
    {
        if (ImageList.Count >= 100) return;
        foreach (var item in ImageList) if (item.InputPath == path) return;
        ImageList.Add(new ImageItem { InputPath = path, FileName = Path.GetFileName(path), Status = "待处理" });
    }

    private void OnClear(object? sender, RoutedEventArgs e)
    {
        ImageList.Clear();
        PreviewImage.Source = null;
        NoImageText.IsVisible = true;
        PreviewImage.IsVisible = false;
        UpdateCounts();
    }

    private async void OnProcess(object? sender, RoutedEventArgs e)
    {
        if (!isInitialized || ImageList.Count == 0) return;

        // 更新膨胀参数
        service.MaskDilate = (int)MaskDilateSlider.Value;

        isProcessing = true;
        ProcessBtn.IsEnabled = false;

        await System.Threading.Tasks.Task.Run(() =>
        {
            int count = 0;
            foreach (var item in ImageList)
            {
                if (item.Status == "完成") continue;

                Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                {
                    item.Status = "处理中...";
                    StatusText.Text = $"处理: {item.FileName}";
                });

                try
                {
                    var dir = Path.GetDirectoryName(item.InputPath) ?? "";
                    var name = Path.GetFileNameWithoutExtension(item.InputPath);
                    var ext = Path.GetExtension(item.InputPath);

                    // 先保存mask
                    var maskPath = Path.Combine(dir, $"{name}_mask.png");
                    service.DetectAndSaveMask(item.InputPath, maskPath);

                    // 再修复
                    var outPath = Path.Combine(dir, $"{name}_clean{ext}");
                    var ok = service.ProcessImage(item.InputPath, outPath);

                    item.OutputPath = outPath;
                    item.Status = ok ? "完成" : "失败";

                    if (ok) Avalonia.Threading.Dispatcher.UIThread.Post(() => ShowImage(outPath));
                }
                catch (Exception ex)
                {
                    item.Status = $"错误";
                    Console.WriteLine(ex.Message);
                }

                count++;
                Avalonia.Threading.Dispatcher.UIThread.Post(() =>
                {
                    CountText.Text = $"已处理: {count} / 总计: {ImageList.Count}";
                });
            }
        });

        isProcessing = false;
        ProcessBtn.IsEnabled = true;
        var gpuMode = service.UsingGPU ? "GPU" : "CPU";
        StatusText.Text = $"处理完成 ({gpuMode}, 膨胀{service.MaskDilate}px)";

        var box = MsBox.Avalonia.MessageBoxManager.GetMessageBoxStandard("完成", "处理完成");
        await box.ShowAsync();
    }

    private void OnSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (ImageDataGrid.SelectedItem is ImageItem item)
        {
            var path = !string.IsNullOrEmpty(item.OutputPath) && File.Exists(item.OutputPath) ? item.OutputPath : item.InputPath;
            ShowImage(path);
        }
    }

    private void ShowImage(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            PreviewImage.Source = new Bitmap(stream);
            NoImageText.IsVisible = false;
            PreviewImage.IsVisible = true;
        }
        catch { }
    }

    private void UpdateCounts()
    {
        CountText.Text = $"已处理: {ImageList.Count(x => x.Status == "完成")} / 总计: {ImageList.Count}";
    }
}
