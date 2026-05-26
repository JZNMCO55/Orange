#version 450

// 内置景深（depth of field）gather pass —— 从 sceneDepth 算每像素 circle-of-
// confusion（CoC），按 CoC 半径在 hdrColor 上做圆盘 gather 模糊，写到独立
// dofColor（避免读写同 target 反馈，与 SSR 同款分离）。dof_composite 再写回 HDR。
//
// CoC = saturate(|viewDepth - focusDistance| / focusRange)，对焦面 CoC≈0（保持
// 锐利），离焦越远 CoC→1（最大模糊半径）。view 空间相机看 -z，场景深度 = -P.z。
//
// 已知简化：单层 gather（不分 near/far）+ 圆盘均匀权重（无 bokeh 形状 / 无
// 前景散射），对焦边界有轻微 bleeding——stylized 够用，物理 bokeh 需 scatter
// 或多层。

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1, std140) uniform DofUbo
{
    mat4 uInvProj;     // clip → view（重建 view 深度）
    vec4 uParams;      // x=focusDistance, y=focusRange, z=maxCoCRadius(uv), w 预留
} dof;
layout(set = 0, binding = 2) uniform sampler2D uSceneDepth;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// 16-tap 圆盘（黄金角螺旋的固定采样），半径按 CoC 缩放。
const int   kTaps = 16;
const float kPi2  = 6.28318530718;

float ViewDepth(vec2 uv)
{
    float d   = texture(uSceneDepth, uv).r;
    vec4  ndc = vec4(uv * 2.0 - 1.0, d, 1.0);
    vec4  vp  = dof.uInvProj * ndc;
    return -(vp.z / vp.w);   // 场景深度（正，离相机越远越大）
}

void main()
{
    float focusDist = dof.uParams.x;
    float focusRange= dof.uParams.y;
    float maxCoC    = dof.uParams.z;

    float vd  = ViewDepth(vUV);
    float coc = clamp(abs(vd - focusDist) / max(focusRange, 1e-4), 0.0, 1.0);
    float radius = coc * maxCoC;

    vec3  sum = texture(uHdrColor, vUV).rgb;
    float wsum = 1.0;
    // 对焦区（radius 极小）直接返回锐利，省 gather。
    if (radius > 0.0008)
    {
        for (int i = 0; i < kTaps; ++i)
        {
            float t     = (float(i) + 0.5) / float(kTaps);
            float ang   = t * kPi2 * 4.0;           // 多圈螺旋
            float r     = sqrt(t) * radius;         // sqrt 使盘内均匀
            vec2  off   = vec2(cos(ang), sin(ang)) * r;
            sum  += texture(uHdrColor, vUV + off).rgb;
            wsum += 1.0;
        }
    }
    outColor = vec4(sum / wsum, 1.0);
}
