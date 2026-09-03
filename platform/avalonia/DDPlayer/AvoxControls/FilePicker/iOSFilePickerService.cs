using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;

namespace AvoxControls.FilePicker
{
    public class iOSFilePickerService : IFilePickerService
    {
        public async Task<string[]> PickFilesAsync(string title, string[] extensions)
        {
            try
            {
                var mainWindow = GetMainWindow();
                if (mainWindow != null)
                {
                    var dialog = new OpenFileDialog
                    {
                        Title = title,
                        AllowMultiple = true,
                        Filters = new List<FileDialogFilter>
                        {
                            new FileDialogFilter { Name = "媒体文件", Extensions = new List<string>(extensions) },
                            new FileDialogFilter { Name = "所有文件", Extensions = new List<string> { "*" } }
                        }
                    };

                    var result = await dialog.ShowAsync(mainWindow);
                    return result ?? new string[0];
                }
                return new string[0];
            }
            catch (Exception ex)
            {
                Console.WriteLine($"文件选择失败: {ex.Message}");
                return new string[0];
            }
        }

        public async Task<string> PickFileAsync(string title, string[] extensions)
        {
            try
            {
                var mainWindow = GetMainWindow();
                if (mainWindow != null)
                {
                    var dialog = new OpenFileDialog
                    {
                        Title = title,
                        AllowMultiple = false,
                        Filters = new List<FileDialogFilter>
                        {
                            new FileDialogFilter { Name = "媒体文件", Extensions = new List<string>(extensions) },
                            new FileDialogFilter { Name = "所有文件", Extensions = new List<string> { "*" } }
                        }
                    };

                    var result = await dialog.ShowAsync(mainWindow);
                    return result?.Length > 0 ? result[0] : null;
                }
                return null;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"文件选择失败: {ex.Message}");
                return null;
            }
        }

        public async Task<string> PickFolderAsync(string title)
        {
            try
            {
                var mainWindow = GetMainWindow();
                if (mainWindow != null)
                {
                    var dialog = new OpenFolderDialog
                    {
                        Title = title
                    };

                    var result = await dialog.ShowAsync(mainWindow);
                    return result;
                }
                return null;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"文件夹选择失败: {ex.Message}");
                return null;
            }
        }

        private Window GetMainWindow()
        {
            try
            {
                if (Avalonia.Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
                {
                    return desktop.MainWindow;
                }
                else if (Avalonia.Application.Current?.ApplicationLifetime is ISingleViewApplicationLifetime)
                {
                    // For mobile platforms, create a temporary window
                    var tempWindow = new Window
                    {
                        Width = 1,
                        Height = 1,
                        Opacity = 0.01,
                        ShowInTaskbar = false,
                        WindowStartupLocation = WindowStartupLocation.CenterScreen
                    };
                    
                    // 必须先显示窗口才能使用ShowAsync
                    tempWindow.Show();
                    return tempWindow;
                }
                return null;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"获取主窗口失败: {ex.Message}");
                return null;
            }
        }
    }
}