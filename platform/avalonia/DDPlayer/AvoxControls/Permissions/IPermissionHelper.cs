using System.Threading.Tasks;

namespace AvoxControls.Permissions
{
    public interface IPermissionHelper
    {
        Task<bool> RequestFileAccessPermission();
    }
}