using AvoxCommon;
using AvoxNet;
using System;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace AvoxCommon
{
    /// <summary>
    /// 播放器基类，实现 IMediaPlayerOb 回调接口
    /// IMediaPlayer、ISourcePlayer、IRtcPlayer 都使用相同的回调接口
    /// </summary>
    public abstract class BasePlayer : IMediaPlayerOb, INotifyPropertyChanged, IDisposable
    {
        protected PlayerState currentState = PlayerState.none;
        private bool isDisposed = false;

        /// <summary>
        /// 当前播放状态
        /// </summary>
        public PlayerState CurrentState
        {
            get => currentState;
            protected set
            {
                if (currentState != value)
                {
                    currentState = value;
                    OnPropertyChanged(nameof(CurrentState));
                    OnPropertyChanged(nameof(IsPlaying));
                    OnPropertyChanged(nameof(IsPaused));
                    OnPropertyChanged(nameof(IsStopped));
                    OnPropertyChanged(nameof(IsNone));
                }
            }
        }

        /// <summary>
        /// 是否正在播放
        /// </summary>
        public bool IsPlaying => CurrentState == PlayerState.playing;

        /// <summary>
        /// 是否已暂停
        /// </summary>
        public bool IsPaused => CurrentState == PlayerState.pause;

        /// <summary>
        /// 是否已停止
        /// </summary>
        public bool IsStopped => CurrentState == PlayerState.stopped;

        /// <summary>
        /// 是否无状态
        /// </summary>
        public bool IsNone => CurrentState == PlayerState.none;

        /// <summary>
        /// 属性变更事件
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// 状态变更事件
        /// </summary>
        public event Action<PlayerState, PlayerState> OnStateChanged;

        /// <summary>
        /// IO 错误事件
        /// </summary>
        public event Action<AVError, string> OnIoError;

        /// <summary>
        /// 解码错误事件
        /// </summary>
        public event Action<TrackType, DecodeResult> OnDecodeError;

        /// <summary>
        /// 就绪事件
        /// </summary>
        public event Action OnReady;

        /// <summary>
        /// 完成事件
        /// </summary>
        public event Action OnComplete;

        /// <summary>
        /// Seek 事件
        /// </summary>
        public event Action OnSeek;

        /// <summary>
        /// 暂停事件
        /// </summary>
        public event Action OnPause;

        /// <summary>
        /// 恢复事件
        /// </summary>
        public event Action OnResume;

        /// <summary>
        /// 关闭事件
        /// </summary>
        public event Action OnClose;

        /// <summary>
        /// 触发属性变更通知
        /// </summary>
        protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        /// <summary>
        /// 状态变更回调
        /// </summary>
        public override void onStateChange(PlayerState preState, PlayerState state) 
        {
            CurrentState = state;
            OnStateChanged?.Invoke(preState, state);          
        }

        /// <summary>
        /// IO 错误回调
        /// </summary>
        public override void onIoError(AVError error, string msg)
        {           
            OnIoError?.Invoke(error, msg);
        }

        /// <summary>
        /// 解码错误回调
        /// </summary>
        public override void onDecodeError(TrackType trackType, DecodeResult error)
        {          
            OnDecodeError?.Invoke(trackType, error);
        }

        /// <summary>
        /// 就绪回调
        /// </summary>
        public override void onReady()
        {         
            OnReady?.Invoke();
        }

        /// <summary>
        /// 完成回调
        /// </summary>
        public override void onComplete()
        {            
            OnComplete?.Invoke();
        }

        /// <summary>
        /// Seek 回调
        /// </summary>
        public override void onSeek()
        {         
            OnSeek?.Invoke();
        }

        /// <summary>
        /// 暂停回调
        /// </summary>
        public override void onPause()
        {        
            OnPause?.Invoke();
        }

        /// <summary>
        /// 恢复回调
        /// </summary>
        public override void onResume()
        {           
            OnResume?.Invoke();
        }

        /// <summary>
        /// 关闭回调
        /// </summary>
        public override void onClose()
        {           
            CurrentState = PlayerState.none;
            OnClose?.Invoke();
        }

        /// <summary>
        /// 释放资源
        /// </summary>
        public virtual void Dispose()
        {
            if (!isDisposed)
            {
                isDisposed = true;
                // 清理所有事件订阅
                OnStateChanged = null;
                OnIoError = null;
                OnDecodeError = null;
                OnReady = null;
                OnComplete = null;
                OnSeek = null;
                OnPause = null;
                OnResume = null;
                OnClose = null;
            }
        }
    }
}
