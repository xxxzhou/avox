// avox CPU 回退路径的 YUV→RGB 转换 (Graphics.Blit 用)
//
// 源纹理是一张 R8 的 w × h*3/2, 装整帧紧凑 NV12:
//   行 [0, h)      : Y, 每行 w 字节
//   行 [h, h*3/2)  : UV 交错, 每行 w 字节 (w/2 组 UV)
// 与 swig/nodejs/yuvglrender.js 的单图方案同构。
//
// 放在 Resources 下: 包内 shader 只被 Shader.Find 引用会被打包剥离,
// Resources.Load 才保证进包。
Shader "Hidden/Avox/YuvToRgb"
{
    Properties
    {
        _MainTex ("YUV (R8, w x h*1.5)", 2D) = "black" {}
    }
    SubShader
    {
        Cull Off
        ZWrite Off
        ZTest Always
        Blend Off

        Pass
        {
            CGPROGRAM
            #pragma vertex vert_img
            #pragma fragment frag
            #include "UnityCG.cginc"

            sampler2D _MainTex;
            // (texW, texH, yRows, flipY)
            float4 _AvoxTexSize;
            // (rV, gU, gV, bU) 矩阵系数, 与原生 CPU 版 Q10 定点同参
            float4 _AvoxCoef;
            // (yBias, yScale, cScale, 0) limited→full 量程展开
            float4 _AvoxRange;

            float4 frag(v2f_img i) : SV_Target
            {
                float2 uv = i.uv;
                if (_AvoxTexSize.w > 0.5) uv.y = 1.0 - uv.y;
                // 目标 RT 尺寸 == 视频尺寸, 故 uv 落在像素中心, floor 得整数像素坐标
                float2 px = floor(uv * float2(_AvoxTexSize.x, _AvoxTexSize.z));
                float2 inv = 1.0 / _AvoxTexSize.xy;
                float y = (tex2D(_MainTex, (px + 0.5) * inv).r - _AvoxRange.x) * _AvoxRange.y;
                // 色度行在 Y 之后, 每两条 Y 行共用一条; U/V 是相邻两字节,
                // 必须精确落在 texel 中心, 否则过滤会把 U 和 V 混在一起
                float cy = _AvoxTexSize.z + floor(px.y * 0.5) + 0.5;
                float cx = floor(px.x * 0.5) * 2.0;
                float u = (tex2D(_MainTex, float2(cx + 0.5, cy) * inv).r - 0.5019608) * _AvoxRange.z;
                float v = (tex2D(_MainTex, float2(cx + 1.5, cy) * inv).r - 0.5019608) * _AvoxRange.z;
                return float4(saturate(y + _AvoxCoef.x * v),
                              saturate(y - _AvoxCoef.y * u - _AvoxCoef.z * v),
                              saturate(y + _AvoxCoef.w * u),
                              1.0);
            }
            ENDCG
        }
    }
    Fallback Off
}
