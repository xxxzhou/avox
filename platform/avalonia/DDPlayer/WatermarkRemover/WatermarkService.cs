using System;
using AvoxNet;

namespace WatermarkRemover;

/// <summary>
/// 水印去除服务 - 使用 IWatermarkRemoval 接口
/// </summary>
public class WatermarkService : IDisposable
{
    private IWatermarkRemoval? remover;
    private bool disposed;
    private int maskDilate = 20;

    /// <summary>
    /// 是否使用GPU
    /// </summary>
    public bool UsingGPU { get; private set; }

    /// <summary>
    /// 模型级别 (0=mini, 1=base, 2=high)
    /// </summary>
    public int ModelLevel { get; private set; }

    /// <summary>
    /// Mask膨胀像素数 (10-50, 默认20)
    /// </summary>
    public int MaskDilate
    {
        get => maskDilate;
        set
        {
            maskDilate = Math.Clamp(value, 10, 50);
            remover?.setMaskDilate(maskDilate);
        }
    }

    /// <summary>
    /// 检测耗时 (毫秒)
    /// </summary>
    public float DetectTimeMs => remover?.getDetectTimeMs() ?? 0;

    /// <summary>
    /// 修复耗时 (毫秒)
    /// </summary>
    public float InpaintTimeMs => remover?.getInpaintTimeMs() ?? 0;

    /// <summary>
    /// 检测到的水印数量
    /// </summary>
    public int WatermarkCount => remover?.getWatermarkCount() ?? 0;

    /// <summary>
    /// 初始化水印去除服务
    /// </summary>
    /// <param name="modelLevel">模型级别: 1=mini, 2=base, 3=high</param>
    public int Init(int modelLevel = 2, bool useGPU = true)
    {
        Release();

        ModelLevel = modelLevel;

#if CS_ENABLE_INPAINT
        // 创建实例
        remover = AvoxWrapper.createWatermarkRemoval();
#else
        // AvoxNet 尚未生成 createWatermarkRemoval 工厂 (native 侧为
        // watermarkRemovalHub.create("inpaint")，未导出 C 接口)；
        // 工厂补齐后删除此 #else 分支即可启用
        remover = null;
#endif
        if (remover == null)
        {
            Console.WriteLine("[WatermarkService] 创建 IWatermarkRemoval 失败");
            return -1;
        }

        // 配置参数
        remover.setModelLevel((ModelLevel)modelLevel);
        remover.setMaskDilate(maskDilate);

        // 先尝试CPU（更稳定）
        Console.WriteLine("[WatermarkService] 尝试CPU初始化...");
        remover.setUseGPU(false);
        if (remover.open())
        {
            UsingGPU = false;
            Console.WriteLine("[WatermarkService] CPU初始化成功");
            return 0;
        }

        // CPU失败，尝试GPU
        if (useGPU)
        {
            Console.WriteLine("[WatermarkService] 尝试GPU初始化...");
            remover.setUseGPU(true);
            if (remover.open())
            {
                UsingGPU = true;
                Console.WriteLine("[WatermarkService] GPU初始化成功");
                return 0;
            }
        }

        Console.WriteLine("[WatermarkService] 所有初始化均失败");
        remover?.Dispose();
        remover = null;
        return -1;
    }

    /// <summary>
    /// 处理单张图片 (检测 + 修复)
    /// </summary>
    public bool ProcessImage(string inputPath, string? outputPath = null)
    {
        if (remover == null || !remover.ready())
            throw new InvalidOperationException("请先调用Init初始化");

        using var input = AvoxWrapper.createImageBuffer();
        using var output = AvoxWrapper.createImageBuffer();

        // 加载图片
        if (!AvoxWrapper.loadImagePath(inputPath, input))
        {
            Console.WriteLine($"[WatermarkService] 加载图片失败: {inputPath}");
            return false;
        }

        // 检测 + 修复
        if (!remover.process(input, output))
        {
            Console.WriteLine($"[WatermarkService] 处理失败");
            return false;
        }

        // 保存结果
        var outPath = outputPath ?? GetDefaultOutputPath(inputPath);
        if (!AvoxWrapper.saveImagePath(outPath, output))
        {
            Console.WriteLine($"[WatermarkService] 保存图片失败: {outPath}");
            return false;
        }

        return true;
    }

    /// <summary>
    /// 仅检测水印并保存mask
    /// </summary>
    public bool DetectAndSaveMask(string inputPath, string? maskPath = null)
    {
        if (remover == null || !remover.ready())
            throw new InvalidOperationException("请先调用Init初始化");

        using var input = AvoxWrapper.createImageBuffer();
        using var mask = AvoxWrapper.createImageBuffer();

        // 加载图片
        if (!AvoxWrapper.loadImagePath(inputPath, input))
        {
            Console.WriteLine($"[WatermarkService] 加载图片失败: {inputPath}");
            return false;
        }

        // 检测
        if (!remover.detect(input, mask))
        {
            Console.WriteLine($"[WatermarkService] 未检测到水印");
            return false;
        }

        // 保存mask
        var mPath = maskPath ?? GetDefaultMaskPath(inputPath);
        if (!AvoxWrapper.saveImagePath(mPath, mask))
        {
            Console.WriteLine($"[WatermarkService] 保存mask失败: {mPath}");
            return false;
        }

        return true;
    }

    /// <summary>
    /// 使用外部mask进行修复
    /// </summary>
    public bool ProcessImageWithMask(string inputPath, string maskPath, string? outputPath = null)
    {
        if (remover == null || !remover.ready())
            throw new InvalidOperationException("请先调用Init初始化");

        using var input = AvoxWrapper.createImageBuffer();
        using var mask = AvoxWrapper.createImageBuffer();
        using var output = AvoxWrapper.createImageBuffer();

        // 加载图片和mask
        if (!AvoxWrapper.loadImagePath(inputPath, input))
        {
            Console.WriteLine($"[WatermarkService] 加载图片失败: {inputPath}");
            return false;
        }

        if (!AvoxWrapper.loadImagePath(maskPath, mask))
        {
            Console.WriteLine($"[WatermarkService] 加载mask失败: {maskPath}");
            return false;
        }

        // 修复
        if (!remover.inpaint(input, mask, output))
        {
            Console.WriteLine($"[WatermarkService] 修复失败");
            return false;
        }

        // 保存结果
        var outPath = outputPath ?? GetDefaultOutputPath(inputPath);
        if (!AvoxWrapper.saveImagePath(outPath, output))
        {
            Console.WriteLine($"[WatermarkService] 保存图片失败: {outPath}");
            return false;
        }

        return true;
    }

    private static string GetDefaultOutputPath(string inputPath)
    {
        var dir = System.IO.Path.GetDirectoryName(inputPath) ?? "";
        var name = System.IO.Path.GetFileNameWithoutExtension(inputPath);
        var ext = System.IO.Path.GetExtension(inputPath);
        return System.IO.Path.Combine(dir, $"{name}_clean{ext}");
    }

    private static string GetDefaultMaskPath(string inputPath)
    {
        var dir = System.IO.Path.GetDirectoryName(inputPath) ?? "";
        var name = System.IO.Path.GetFileNameWithoutExtension(inputPath);
        return System.IO.Path.Combine(dir, $"{name}_mask.png");
    }

    /// <summary>
    /// 释放资源
    /// </summary>
    public void Release()
    {
        remover?.close();
        remover?.Dispose();
        remover = null;
    }

    public void Dispose()
    {
        if (disposed) return;
        Release();
        disposed = true;
        GC.SuppressFinalize(this);
    }

    ~WatermarkService()
    {
        Dispose();
    }
}
