using CommunityToolkit.Mvvm.ComponentModel;

namespace AvaPlayer.ViewModels
{
    public partial class MainViewModel : ViewModelBase
    {
        [ObservableProperty]
        private string greeting = "Welcome to Avalonia!";
    }
}
