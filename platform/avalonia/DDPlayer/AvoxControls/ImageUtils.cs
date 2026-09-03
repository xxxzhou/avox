using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;
using AvoxNet;
using System;
using System.IO;
using System.Runtime.InteropServices;

namespace AvoxCommon
{
    public static class ImageUtils
    {
        public static bool SaveAsPng(IImageBuffer imageBuffer, string filePath)
        {
            if (imageBuffer == null || imageBuffer.getPointer() == IntPtr.Zero)
            {
                Console.WriteLine("[ImageUtils] 错误: 图像缓冲区为空");
                return false;
            }
            // 获取图像元数据
            AvoxNet.ImageFormat format = imageBuffer.getImageFormat();
            int width = format.width;
            int height = format.height;
            int stride = format.rowPitch;
            // 如果步长为0，根据像素大小自动计算
            if (stride == 0)
            {               
                stride = width * 4; 
            }
            return SaveAsPng(imageBuffer.getPointer(), width, height, stride, format.imageType, filePath);
        }

        /// <summary>
        /// 内部实现：使用 Avalonia WriteableBitmap 处理像素并保存
        /// </summary>
        public static bool SaveAsPng(IntPtr dataPtr, int width, int height, int stride, AvoxNet.ImageType imageType, string filePath)
        {
            if (dataPtr == IntPtr.Zero || width <= 0 || height <= 0) return false;

            try
            {
                // 1. 确保目录存在
                string directory = Path.GetDirectoryName(filePath);
                if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
                {
                    Directory.CreateDirectory(directory);
                }

                // 2. 映射像素格式 (RGBA <-> BGRA)
                // Avalonia 的 PixelFormat 在 Android 上表现非常稳定
                PixelFormat avaloniaFormat = PixelFormat.Rgba8888;
                if (imageType == AvoxNet.ImageType.bgra8)
                {
                    avaloniaFormat = PixelFormat.Bgra8888;
                }
                // 注意：如果你的 C++ fetchFrame 输出的是 ARGB，Avalonia 也有对应的格式

                // 3. 创建 WriteableBitmap (DPI 统一设为 96)
                using (var bitmap = new WriteableBitmap(
                    new PixelSize(width, height),
                    new Vector(96, 96),
                    avaloniaFormat,
                    AlphaFormat.Unpremul)) // 通常截图使用非预乘 Alpha
                {
                    using (var lockedBuffer = bitmap.Lock())
                    {
                        IntPtr destPtr = lockedBuffer.Address;
                        int destStride = lockedBuffer.RowBytes;

                        // 4. 逐行拷贝内存 (处理 Stride 不一致的情况)
                        for (int y = 0; y < height; y++)
                        {
                            IntPtr srcRow = dataPtr + (y * stride);
                            IntPtr dstRow = destPtr + (y * destStride);

                            // 拷贝一行数据 (假设 4 字节每像素)
                            byte[] rowData = new byte[width * 4];
                            Marshal.Copy(srcRow, rowData, 0, rowData.Length);
                            Marshal.Copy(rowData, 0, dstRow, rowData.Length);
                        }
                    }
                    // 5. 调用 Avalonia 内置保存方法 (Android 上会自动处理成 PNG)
                    bitmap.Save(filePath);
                    return true;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[ImageUtils] 保存异常: {ex.Message}");
                return false;
            }
        }

    }
}
