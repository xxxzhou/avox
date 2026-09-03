namespace AvoxControls.Permissions
{
    public static class PermissionHelperFactory
    {
        public static IPermissionHelper Create()
        {
#if ANDROID
            return new AndroidPermissionHelper();
#elif IOS
            return new iOSPermissionHelper();
#else
            return new DesktopPermissionHelper();
#endif
        }
    }
}