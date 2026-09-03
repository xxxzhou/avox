using Avalonia.Controls;
using AvoxNet;

namespace AvoxControls
{
    /// <summary>
    /// SourceInfoView - 显示媒体源信息
    /// </summary>
    public partial class SourceInfoView : UserControl
    {
        private SourceInfoModel model;

        public SourceInfoView()
        {
            InitializeComponent();
            model = new SourceInfoModel();
            DataContext = model;
        }

        /// <summary>
        /// 更新源信息
        /// </summary>
        public void UpdateSourceInfo(ISourceInfo sourceInfo)
        {
            model.UpdateSourceInfo(sourceInfo);
        }

        /// <summary>
        /// 清除源信息
        /// </summary>
        public void Clear()
        {
            model.Clear();
        }

        /// <summary>
        /// 获取 ViewModel
        /// </summary>
        public SourceInfoModel ViewModel => model;
    }
}
