using Avalonia.Input;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace AvoxControls
{
    public static class PlatformHelper
    {
        [DllImport("user32.dll")]
        public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")]
        public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll", SetLastError = true)]
        public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
        public const uint SWP_NOZORDER = 0x0004;
        public const uint SWP_NOACTIVATE = 0x0010;
        public const uint WM_SIZE = 0x0005;
        /// <summary>
        /// 获取应用的基础路径（用于存储配置、缓存等）
        /// </summary>
        public static string GetBasePath()
        {
            string basePath;

#if ANDROID
            // Android 10+ 推荐使用应用专属存储，不需要权限
            // 使用 Context.GetFilesDir() 对应的路径
            var context = Android.App.Application.Context;
            basePath = context.FilesDir.AbsolutePath;
#elif IOS
            // iOS 使用应用沙盒的 Library 目录（不会被 iTunes 备份）
            var documentsPath = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
            var libraryPath = Path.Combine(documentsPath, "..", "Library");
            basePath = Path.GetFullPath(libraryPath);
#else
            // Windows/Linux/macOS 使用 LocalApplicationData
            basePath = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
#endif

            // 确保路径不为空
            if (string.IsNullOrEmpty(basePath))
            {
                throw new InvalidOperationException("无法获取应用基础路径");
            }

            return basePath;
        }

        /// <summary>
        /// 获取用于存储图片的路径（截图等）
        /// </summary>
        public static string GetPicturesPath()
        {
            string basePath;

#if ANDROID
            // Android 10+ 使用应用专属 Pictures 目录
            var context = Android.App.Application.Context;
            basePath = context.GetExternalFilesDir(Android.OS.Environment.DirectoryPictures)?.AbsolutePath
                       ?? context.FilesDir.AbsolutePath;            
#elif IOS
            // iOS 使用 Documents/Screenshots
            var documentsPath = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
            basePath = Path.Combine(documentsPath, "Screenshots");
#else
            // Windows/Linux/macOS 使用系统 Pictures 目录
            basePath = Environment.GetFolderPath(Environment.SpecialFolder.MyPictures);
#endif

            if (string.IsNullOrEmpty(basePath))
            {
                throw new InvalidOperationException("无法获取图片存储路径");
            }
            return basePath;
        }
        public static string GetSystemPicturesPath()
        {
            string basePath;

#if ANDROID
    // 关键：必须使用 GetExternalStoragePublicDirectory
    // 这会返回 /storage/emulated/0/Pictures
    basePath = Android.OS.Environment.GetExternalStoragePublicDirectory(Android.OS.Environment.DirectoryPictures).AbsolutePath;
#elif IOS
    basePath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Screenshots");
#else
            basePath = Environment.GetFolderPath(Environment.SpecialFolder.MyPictures);
#endif

            return basePath;
        }
    }
}
