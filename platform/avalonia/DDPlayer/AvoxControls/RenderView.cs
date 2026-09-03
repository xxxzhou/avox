using Avalonia;
using Avalonia.Controls;
using Avalonia.Platform;
using AvoxCommon;
using AvoxNet;
using System;
using System.ComponentModel;
using System.Reflection.Metadata;
using System.Runtime.InteropServices;

#if ANDROID
using Avalonia.Android;
using Android.Views;
using Avalonia.Android.Platform;
#endif
#if IOS
using UIKit;
using ObjCRuntime;
using CoreAnimation;
using Foundation;
using Metal;
#endif

namespace AvoxControls
{
    /// <summary>
    /// 渲染视图控件
    /// 以 ISurfaceRender 为主体封装，支持多平台原生窗口渲染
    /// </summary>
    public class RenderView : NativeControlHost, IDisposable
    {
        private IntPtr platformHandle;
        private ISurfaceRender windowRender;        
        private bool isDisposed = false;
#if IOS
        // 防止 GC
        private MetalView metalView;
#endif
        public RenderView()
        {
            AvoxWrapper.logMsg(LogLevel.info, "RenderView 创建");
        }

        /// <summary>
        /// 窗口渲染器
        /// </summary>
        public ISurfaceRender WindowRender
        {
            get => windowRender;
            set
            {
                if (windowRender != value)
                {           
                    // 解绑旧的渲染器
                    if (windowRender != null && platformHandle != IntPtr.Zero)
                    {          
                        windowRender.setSurface(IntPtr.Zero);                        
                    }
                    windowRender = value;
                    // 绑定新的渲染器
                    if (windowRender != null && platformHandle != IntPtr.Zero)
                    {
                        windowRender.setVulkan(UseVulkanRendering);
                        windowRender.setSurface(platformHandle);
                    }
                    OnPropertyChanged(nameof(WindowRender));
                }
            }
        }

        /// <summary>
        /// 是否使用 Vulkan 渲染
        /// </summary>
        public bool UseVulkanRendering
        {
            get {
                return ConfigManager.Instance.Config.Playback.UseVulkanRendering;
            }
        }

        /// <summary>
        /// 属性变更事件
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// 触发属性变更通知
        /// </summary>
        protected virtual void OnPropertyChanged([System.Runtime.CompilerServices.CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        protected override Size ArrangeOverride(Size finalSize)
        {
            var result = base.ArrangeOverride(finalSize);

#if !ANDROID && !IOS
            // 仅在 Windows/Desktop 环境下通过消息队列同步尺寸
            if (windowRender != null && platformHandle != IntPtr.Zero)
            {
                double scale = this.VisualRoot?.RenderScaling ?? 1.0;
                int pixelWidth = (int)(finalSize.Width * scale);
                int pixelHeight = (int)(finalSize.Height * scale);
                //bool bchange = PlatformHelper.SetWindowPos(platformHandle, IntPtr.Zero, 0, 0, pixelWidth, pixelHeight, PlatformHelper.SWP_NOZORDER | PlatformHelper.SWP_NOACTIVATE);
                //AvoxWrapper.logMsg(LogLevel.info, $"size change:{bchange},width:{pixelWidth}-height:{pixelHeight}");
                //IntPtr lParam = (IntPtr)((pixelHeight << 16) | (pixelWidth & 0xffff));               
                //PlatformHelper.PostMessage(platformHandle, PlatformHelper.WM_SIZE, IntPtr.Zero, lParam);              
            }
#endif
            return result;
        }

        protected override IPlatformHandle CreateNativeControlCore(IPlatformHandle parent)
        {
            // Windows平台创建的窗口是可以直接绘制的
            // android的NativeControl是view,绘制需要SurfaceView
            // IOS的NativeControl是UIView，绘制需要CAMetalLayer
#if ANDROID
            // parent.Handle 指向的是 Android 的 ViewGroup (Avalonia 容器)
            var platformParent = parent as AndroidViewControlHandle;
            var parentView = platformParent?.View;
            var context = parentView?.Context ?? Android.App.Application.Context;
            // SurfaceView 继承自 View,所以完全符合 NativeControlHost 的要求
            var surfaceView = new SurfaceView(context);
            // 设置回调以获取 ANativeWindow 所需的 Surface
            surfaceView.Holder?.AddCallback(new SurfaceCallback(this));
            // 返回包装后的 View 句柄
            return new AndroidViewControlHandle(surfaceView);
#elif IOS
            // 创建支持 Metal 的自定义 UIView
            metalView = new MetalView();
            // metalView.Handle 是 UIView 的句柄
            // metalView.MetalLayer.Handle 是 CAMetalLayer 的句柄
            platformHandle = metalView.MetalLayer.Handle;
            OnAttachPlatformHandle();
            // 返回包装后的 iOS 句柄
            // 在 Avalonia 中通常使用包含 UIView 的平台句柄
            return new PlatformHandle(metalView.Handle, "uiview");
#else
            var result = base.CreateNativeControlCore(parent);
            platformHandle = result.Handle;
            OnAttachPlatformHandle();
            // 把windows根据platformHandle生成的新handle放入
            return new PlatformHandle(platformHandle, "HWND");
#endif
        }

#if ANDROID
        private class SurfaceCallback : Java.Lang.Object, ISurfaceHolderCallback
        {
            private RenderView renderView;
            public SurfaceCallback(RenderView view)
            {
                renderView = view;
            }

            public void SurfaceCreated(ISurfaceHolder holder)
            {
                // 获取 Java 层的 Surface 对象句柄
                IntPtr surfaceHandle = holder.Surface.Handle;
                using (var jniHelperClass = Java.Lang.Class.ForName("avox.android.library.JNIHelper"))
                {
                    var getNativeSurfaceMethod = jniHelperClass.GetMethod("getNativeSurface",
                        Java.Lang.Class.FromType(typeof(Android.Views.Surface)));
                    var resultObj = getNativeSurfaceMethod.Invoke(null, holder.Surface);
                    if (resultObj != null)
                    {
                        renderView.platformHandle = new IntPtr((long)resultObj);
                        renderView.OnAttachPlatformHandle();
                    }
                }
            }

            public void SurfaceChanged(Android.Views.ISurfaceHolder holder, Android.Graphics.Format format, int width, int height)
            {
            }

            public void SurfaceDestroyed(Android.Views.ISurfaceHolder holder)
            {
                renderView.OnDetachPlatformHandle();
            }
        }
#elif IOS
        public class MetalView : UIView
        {
            // 关键：告诉 UIKit 这个视图的底层 Layer 使用 CAMetalLayer
            [Export("layerClass")]
            public static Class LayerClass() => new Class(typeof(CAMetalLayer));

            public CAMetalLayer MetalLayer => (CAMetalLayer)Layer;

            public MetalView()
            {
                // 配置 Metal 层参数
                MetalLayer.Opaque = true;
                // 根据需要设置像素格式，通常是 BGRA8Unorm
                MetalLayer.PixelFormat = MTLPixelFormat.RGBA8Unorm;
            }

            // 必须处理尺寸变化，否则渲染出来的画面可能变形或只有一角
            public override void LayoutSubviews()
            {
                base.LayoutSubviews();
                // 关键：同步 View 尺寸到 MetalLayer
                // iOS 高刷屏/Retina屏需要乘以 ContentsScale，否则画面会模糊且尺寸不对
                var scale = UIScreen.MainScreen.Scale;
                var drawableSize = new CoreGraphics.CGSize(
                    Bounds.Width * scale,
                    Bounds.Height * scale
                );
                if (drawableSize.Width > 0 && drawableSize.Height > 0)
                {
                    MetalLayer.DrawableSize = drawableSize;
                    MetalLayer.ContentsScale = scale;
                }
            }
        }
#endif

        protected override void DestroyNativeControlCore(IPlatformHandle control)
        {
            // 解绑播放器句柄
            OnDetachPlatformHandle();
            platformHandle = IntPtr.Zero;
            base.DestroyNativeControlCore(control);
        }

        protected void OnAttachPlatformHandle()
        {
            if (WindowRender != null && platformHandle != IntPtr.Zero)
            {
                try
                {
                    WindowRender.setVulkan(UseVulkanRendering);
                    WindowRender.setSurface(platformHandle);                    
                    // windows平台传入的与创建的可能是不同的
                    platformHandle = WindowRender.getSurface();
                    AvoxWrapper.logMsg(LogLevel.info, "渲染器绑定到窗口成功");
                }
                catch (Exception ex)
                {
                    AvoxWrapper.logMsg(LogLevel.error, $"绑定渲染器失败: {ex.Message}");
                }
            }
        }

        protected void OnDetachPlatformHandle()
        {
            if (WindowRender != null)
            {
                try
                {
                    WindowRender.setSurface(IntPtr.Zero);
                    AvoxWrapper.logMsg(LogLevel.info, "渲染器从窗口解绑成功");
                }
                catch (Exception ex)
                {
                    AvoxWrapper.logMsg(LogLevel.error, $"解绑渲染器失败: {ex.Message}");
                }
            }
        }

        public void Dispose()
        {
            if (!isDisposed)
            {
                isDisposed = true;

                // 移除事件监听
                PropertyChanged = null;

                // 释放平台资源
                OnDetachPlatformHandle();

                AvoxWrapper.logMsg(LogLevel.info, "RenderView 资源释放完成");
            }
        }

    }
}
