using Android;
using Android.App;
using Android.Content;
using Android.Content.PM;
using Android.OS;
using AndroidX.Core.App;
using AndroidX.Core.Content;
using Avalonia;
using Avalonia.Android;
using AvaPlayer;
using System;
using System.IO;
using System.Linq;

namespace AvaPlayer.Android
{
    [Activity(
        Label = "AvaPlayer.Android",
        Theme = "@style/MyTheme.NoActionBar",
        Icon = "@drawable/icon",
        MainLauncher = true,
        ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize | ConfigChanges.UiMode)]
    public class MainActivity : AvaloniaMainActivity<App>
    {
        protected override void OnCreate(Bundle savedInstanceState)
        {
            try
            {  
                // 初始化底层avox
                using (var jniHelperClass = Java.Lang.Class.ForName("avox.android.library.JNIHelper"))
                {
                    var initJniMethod = jniHelperClass.GetMethod("initJNI", Java.Lang.Class.FromType(typeof(Activity)));
                    initJniMethod.Invoke(null, this);
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"[WebRTC] Critical Error: {ex}");
            }

            base.OnCreate(savedInstanceState);

            // 定义需要请求的权限数组
            string[] permissions = {
                Manifest.Permission.RecordAudio,
                Manifest.Permission.Camera
            };

            // 检查并请求未授予的权限
            var missingPermissions = permissions
                .Where(p => ContextCompat.CheckSelfPermission(this, p) != Permission.Granted)
                .ToArray();

            if (missingPermissions.Any())
            {
                // 1 只是一个请求码（RequestCode），你可以自定义
                ActivityCompat.RequestPermissions(this, missingPermissions, 1);
            }
        }

        protected override AppBuilder CustomizeAppBuilder(AppBuilder builder)
        {
            return base.CustomizeAppBuilder(builder)
                .WithInterFont();
        }
    }
}