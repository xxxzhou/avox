using AvoxNet;
using System;
using System.ComponentModel;

namespace AvoxCommon
{
    /// <summary>
    /// 语音识别与翻译配置
    /// 用于配置播放器的语音识别(ASR)和翻译功能
    ///
    /// 说明：
    /// - 流式模式(streaming)：只支持中英文识别，不支持翻译
    /// - PTS同步模式(ptsSync)：支持多语言识别，支持日转中翻译
    /// </summary>
    public class SttConfig : INotifyPropertyChanged
    {
        #region 私有字段
        private bool enableStt = false;
        private bool enableTranslation = false;
        private string tencentSecretId = "";
        private string tencentSecretKey = "";

        // 字体显示配置
        private float fontScale = 2.0f;
        private float fontColorR = 1.0f;
        private float fontColorG = 1.0f;
        private float fontColorB = 1.0f;
        private float fontOpacity = 0.0f;
        private float layoutX = 0.5f;
        private float layoutY = 0.8f;
        private float layoutWidth = 0.5f;
        private float layoutHeight = 0.4f;
        private HAlignType hAlignment = HAlignType.mid;
        private VAlignType vAlignment = VAlignType.top;
        #endregion

        /// <summary>
        /// 是否启用语音识别
        /// </summary>
        public bool EnableStt
        {
            get => enableStt;
            set
            {
                if (enableStt != value)
                {
                    enableStt = value;
                    OnPropertyChanged(nameof(EnableStt));
                }
            }
        }

        /// <summary>
        /// 是否启用翻译（日转中，仅 ptsSync 模式有效）
        /// </summary>
        public bool EnableTranslation
        {
            get => enableTranslation;
            set
            {
                if (enableTranslation != value)
                {
                    enableTranslation = value;
                    OnPropertyChanged(nameof(EnableTranslation));
                }
            }
        }

        /// <summary>
        /// 腾讯云翻译 API SecretId
        /// </summary>
        public string TencentSecretId
        {
            get => tencentSecretId;
            set
            {
                if (tencentSecretId != value)
                {
                    tencentSecretId = value;
                    OnPropertyChanged(nameof(TencentSecretId));
                }
            }
        }

        /// <summary>
        /// 腾讯云翻译 API SecretKey
        /// </summary>
        public string TencentSecretKey
        {
            get => tencentSecretKey;
            set
            {
                if (tencentSecretKey != value)
                {
                    tencentSecretKey = value;
                    OnPropertyChanged(nameof(TencentSecretKey));
                }
            }
        }

        #region 字体显示配置

        /// <summary>
        /// 字体缩放比例，默认1.0，用于放大显示
        /// </summary>
        public float FontScale
        {
            get => fontScale;
            set
            {
                if (Math.Abs(fontScale - value) > 0.001f)
                {
                    fontScale = value;
                    OnPropertyChanged(nameof(FontScale));
                }
            }
        }

        /// <summary>
        /// 字体颜色红色分量 (0-1)
        /// </summary>
        public float FontColorR
        {
            get => fontColorR;
            set
            {
                if (Math.Abs(fontColorR - value) > 0.001f)
                {
                    fontColorR = value;
                    OnPropertyChanged(nameof(FontColorR));
                }
            }
        }

        /// <summary>
        /// 字体颜色绿色分量 (0-1)
        /// </summary>
        public float FontColorG
        {
            get => fontColorG;
            set
            {
                if (Math.Abs(fontColorG - value) > 0.001f)
                {
                    fontColorG = value;
                    OnPropertyChanged(nameof(FontColorG));
                }
            }
        }

        /// <summary>
        /// 字体颜色蓝色分量 (0-1)
        /// </summary>
        public float FontColorB
        {
            get => fontColorB;
            set
            {
                if (Math.Abs(fontColorB - value) > 0.001f)
                {
                    fontColorB = value;
                    OnPropertyChanged(nameof(FontColorB));
                }
            }
        }

        /// <summary>
        /// 字体透明度 (0=不透明, 1=完全透明)
        /// </summary>
        public float FontOpacity
        {
            get => fontOpacity;
            set
            {
                if (Math.Abs(fontOpacity - value) > 0.001f)
                {
                    fontOpacity = value;
                    OnPropertyChanged(nameof(FontOpacity));
                }
            }
        }

        /// <summary>
        /// 字体布局 X 坐标 (0-1)
        /// </summary>
        public float LayoutX
        {
            get => layoutX;
            set
            {
                if (Math.Abs(layoutX - value) > 0.001f)
                {
                    layoutX = value;
                    OnPropertyChanged(nameof(LayoutX));
                }
            }
        }

        /// <summary>
        /// 字体布局 Y 坐标 (0-1)
        /// </summary>
        public float LayoutY
        {
            get => layoutY;
            set
            {
                if (Math.Abs(layoutY - value) > 0.001f)
                {
                    layoutY = value;
                    OnPropertyChanged(nameof(LayoutY));
                }
            }
        }

        /// <summary>
        /// 字体布局最大宽度比例 (0-1)
        /// </summary>
        public float LayoutWidth
        {
            get => layoutWidth;
            set
            {
                if (Math.Abs(layoutWidth - value) > 0.001f)
                {
                    layoutWidth = value;
                    OnPropertyChanged(nameof(LayoutWidth));
                }
            }
        }

        /// <summary>
        /// 字体布局最大高度比例 (0-1)
        /// </summary>
        public float LayoutHeight
        {
            get => layoutHeight;
            set
            {
                if (Math.Abs(layoutHeight - value) > 0.001f)
                {
                    layoutHeight = value;
                    OnPropertyChanged(nameof(LayoutHeight));
                }
            }
        }

        /// <summary>
        /// 水平对齐方式
        /// </summary>
        public HAlignType HAlignment
        {
            get => hAlignment;
            set
            {
                if (hAlignment != value)
                {
                    hAlignment = value;
                    OnPropertyChanged(nameof(HAlignment));
                }
            }
        }

        /// <summary>
        /// 垂直对齐方式
        /// </summary>
        public VAlignType VAlignment
        {
            get => vAlignment;
            set
            {
                if (vAlignment != value)
                {
                    vAlignment = value;
                    OnPropertyChanged(nameof(VAlignment));
                }
            }
        }

        #endregion

        /// <summary>
        /// 属性变更通知事件
        /// </summary>
        public event PropertyChangedEventHandler PropertyChanged;

        /// <summary>
        /// 触发属性变更通知
        /// </summary>
        /// <param name="propertyName">属性名称</param>
        protected virtual void OnPropertyChanged(string propertyName)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }

        /// <summary>
        /// 克隆配置
        /// </summary>
        public SttConfig Clone()
        {
            return new SttConfig
            {
                EnableStt = this.EnableStt,
                EnableTranslation = this.EnableTranslation,
                TencentSecretId = this.TencentSecretId,
                TencentSecretKey = this.TencentSecretKey,
                FontScale = this.FontScale,
                FontColorR = this.FontColorR,
                FontColorG = this.FontColorG,
                FontColorB = this.FontColorB,
                FontOpacity = this.FontOpacity,
                LayoutX = this.LayoutX,
                LayoutY = this.LayoutY,
                LayoutWidth = this.LayoutWidth,
                LayoutHeight = this.LayoutHeight,
                HAlignment = this.HAlignment,
                VAlignment = this.VAlignment
            };
        }
    }
}