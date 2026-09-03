using System.Threading.Tasks;

namespace AvoxControls.Permissions
{
    public class DesktopPermissionHelper : IPermissionHelper
    {
        public Task<bool> RequestFileAccessPermission()
        {
            return Task.FromResult(true);
        }
    }
}