#version 450

// 内置接触阴影（contact shadows）片元 shader —— 屏幕空间向光源做 view-space
// 射线步进，命中遮挡 → 输出 <1 的阴影因子，乘法 blend 进 HDR。
//
// 补 shadow map + PCSS 在接触处因 depthBias 抬起留下的"漏光缝隙"（sub-texel
// 接触细节），AAA 引擎常用（UE "Contact Shadows"）。与 SSR 同款 view-space
// march + proj 投回（用 invProj 重建 / proj 投回，矩阵自带 y-flip 自洽，规避
// 从屏幕 2D 方向构造 view 方向的 y-flip 陷阱）。
//
// 已知简化：阴影因子乘进整个 HDR（含 ambient / 其他光），而非仅 directional
// 项——与 SSAO 同款前向取舍。length 取接触尺度（短），串扰可忽略。

layout(set = 0, binding = 0) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 1, std140) uniform CsUbo
{
    mat4 uProj;          // view → clip（与主 pass 同款 y-flip）
    mat4 uInvProj;       // clip → view
    vec4 uViewLightDir;  // xyz = 朝光方向（view space，单位），w 未用
    vec4 uParams;        // x=length(view 米), y=maxSteps, z=thickness, w=strength
    vec4 uParams2;       // x=bias, y/z/w 预留
} cs;
layout(set = 0, binding = 2) uniform sampler2D uNormal;  // view-space 法线(n*0.5+0.5)

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// per-pixel 抖动起点相位，把固定步长的阶梯 banding 打散成细噪声（接触阴影
// 标准做法；低步数下尤其必要）。
float Hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 ViewPosFromUV(vec2 uv)
{
    float d   = texture(uSceneDepth, uv).r;
    vec4  ndc = vec4(uv * 2.0 - 1.0, d, 1.0);
    vec4  vp  = cs.uInvProj * ndc;
    return vp.xyz / vp.w;
}

void main()
{
    float depth = texture(uSceneDepth, vUV).r;
    if (depth >= 0.9999) { outColor = vec4(1.0); return; }   // 天空 → 不遮蔽

    vec3  P = ViewPosFromUV(vUV);
    vec3  L = normalize(cs.uViewLightDir.xyz);   // 朝光方向

    // 法线 fade：背光面（N·L≤0）本就被 N·L 着色压暗，不叠接触阴影，避免在
    // 终结线 / 自遮蔽处重复暗化（接触阴影只该补朝光面的接触缝隙）。
    vec3  N = normalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);
    if (dot(N, -P) < 0.0) { N = -N; }
    float ndl = dot(N, L);
    if (ndl <= 0.0) { outColor = vec4(1.0); return; }
    float ndlFade = smoothstep(0.0, 0.25, ndl);   // 近终结线渐隐

    float rayLen    = cs.uParams.x;
    int   steps     = int(cs.uParams.y);
    float thickness = cs.uParams.z;
    float strength  = cs.uParams.w;
    float bias      = cs.uParams2.x;

    float stepLen = rayLen / float(max(steps, 1));
    // 起点 = bias（防自遮蔽 acne）+ 抖动相位（打散 banding）。
    float jitter  = Hash12(gl_FragCoord.xy);
    vec3  rayPos  = P + L * (bias + jitter * stepLen);

    float occluded = 0.0;
    for (int i = 1; i <= steps; ++i)
    {
        rayPos += L * stepLen;
        if (rayPos.z >= 0.0) { break; }          // 越过相机平面（view z 正 = 相机后）

        vec4 clip = cs.uProj * vec4(rayPos, 1.0);
        clip.xyz /= clip.w;
        vec2 uv = clip.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { break; }  // 出屏

        float sd = texture(uSceneDepth, uv).r;
        if (sd >= 0.9999) { continue; }          // 该处天空 → 无几何

        float sceneZ = ViewPosFromUV(uv).z;      // 该屏幕位置几何 view-z
        float diff   = sceneZ - rayPos.z;        // 几何比射线更近相机(z 更大) → diff>0 遮挡
        if (diff > 0.0 && diff < thickness)
        {
            occluded = 1.0;
            break;
        }
    }

    float shadow = 1.0 - occluded * strength * ndlFade;
    outColor = vec4(vec3(shadow), 1.0);
}
