namespace AvoxControls.Permissions
{
    public class iOSPermissionHelper : IPermissionHelper
    {
        public System.Threading.Tasks.Task<bool> RequestFileAccessPermission()
        {
            // iOS权限请求实现
            // 注意：实际实现需要引用iOS特定的库
            return System.Threading.Tasks.Task.FromResult(true);
        }
    }
}