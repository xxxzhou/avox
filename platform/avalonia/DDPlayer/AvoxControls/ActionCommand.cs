using System;
using System.Windows.Input;

namespace AvoxControls
{
    /// <summary>
    /// 简单的命令实现
    /// </summary>
    public class ActionCommand : ICommand
    {
        private readonly Action execute;
        private readonly Func<bool> canExecute;

        public ActionCommand(Action executeAction, Func<bool> canExecuteAction = null)
        {
            execute = executeAction ?? throw new ArgumentNullException(nameof(executeAction));
            canExecute = canExecuteAction;
        }

        public bool CanExecute(object parameter) => canExecute?.Invoke() ?? true;

        public void Execute(object parameter) => execute();

        public event EventHandler CanExecuteChanged;

        public void RaiseCanExecuteChanged()
        {
            CanExecuteChanged?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>
    /// 带参数的命令实现
    /// </summary>
    /// <typeparam name="T">参数类型</typeparam>
    public class ActionCommand<T> : ICommand
    {
        private readonly Action<T> execute;
        private readonly Func<T, bool> canExecute;

        public ActionCommand(Action<T> executeAction, Func<T, bool> canExecuteAction = null)
        {
            execute = executeAction ?? throw new ArgumentNullException(nameof(executeAction));
            canExecute = canExecuteAction;
        }

        public bool CanExecute(object parameter)
        {
            if (canExecute == null) return true;
            if (parameter is T typedParam)
            {
                return canExecute(typedParam);
            }
            return true;
        }

        public void Execute(object parameter)
        {
            if (parameter is T typedParam)
            {
                execute(typedParam);
            }
        }

        public event EventHandler CanExecuteChanged;

        public void RaiseCanExecuteChanged()
        {
            CanExecuteChanged?.Invoke(this, EventArgs.Empty);
        }
    }
}
