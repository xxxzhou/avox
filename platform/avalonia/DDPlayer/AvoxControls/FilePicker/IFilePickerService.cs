using System.Threading.Tasks;

namespace AvoxControls.FilePicker
{
    public interface IFilePickerService
    {
        Task<string[]> PickFilesAsync(string title, string[] extensions);
        Task<string> PickFileAsync(string title, string[] extensions);
        Task<string> PickFolderAsync(string title);
    }
}