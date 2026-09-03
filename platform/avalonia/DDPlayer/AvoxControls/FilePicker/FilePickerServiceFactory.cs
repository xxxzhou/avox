namespace AvoxControls.FilePicker
{
    public static class FilePickerServiceFactory
    {
        public static IFilePickerService Create()
        {
#if ANDROID
            return new AndroidFilePickerService();
#elif IOS
            return new iOSFilePickerService();
#else
            return new DesktopFilePickerService();
#endif
        }
    }
}