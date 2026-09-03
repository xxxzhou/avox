using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using System;

namespace AvoxControls
{
    public partial class MessageBox : Window
    {
        public string Message { get; set; }

        public MessageBox()
        {
            InitializeComponent();
            DataContext = this;
        }

        public MessageBox(string title, string message)
        {
            InitializeComponent();
            Title = title;
            Message = message;            
            // 设置数据上下文
            DataContext = this;
            OKButton.Click += (s, e) => Close();
            
        }

        private void InitializeComponent()
        {
            AvaloniaXamlLoader.Load(this);
        }
    }
}