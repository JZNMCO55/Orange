#version 450

// 内置 SSR 片元 shader —— view-space 射线步进的屏幕空间反射。
//
// 前向渲染无 G-buffer：从 sceneDepth 重建 view-space pos + 法线(深度导数)，
// 沿反射方向在 view 空间均匀步进，每步投回屏幕采 sceneDepth 判命中(射线
// 越过最近几何面且在 thickness 容差内)。命中则采 hdrColor 作反射色，按
// fresnel(NoV) + 边缘淡出加权，输出到独立 ssrColor(避免读写同 target)。
// ssr_composite 再加性合成进 HDR。
//
// 已知简化：统一 F0=0.04 fresnel(非材质驱动)、view-space 均匀步长(近处
// 略欠采样，未上 Hi-Z/DDA)、无二分细化 —— stylized 湿表面够用。

layout(set = 0, binding = 0) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 1) uniform sampler2D uHdrColor;
layout(set = 0, binding = 2, std140) uniform SsrUbo
{
    mat4 uProj;       // view → clip(与主 pass 同款 y-flip)
    mat4 uInvProj;    // clip → view
    vec4 uParams;     // x=maxDistance, y=maxSteps, z=thickness, w=strength
} ssr;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 ViewPosFromUV(vec2 uv)
{
    float depth = texture(uSceneDepth, uv).r;
    vec4  ndc   = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4  vp    = ssr.uInvProj * ndc;
    return vp.xyz / vp.w;
}

void main()
{
    float depth = texture(uSceneDepth, vUV).r;
    if (depth >= 0.9999)   // 天空/无几何 → 无反射
    {
        outColor = vec4(0.0);
        return;
    }

    vec3 P = ViewPosFromUV(vUV);
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));
    if (dot(N, -P) < 0.0) { N = -N; }       // 强制朝相机
    vec3 V = normalize(-P);                  // 表面 → 相机
    vec3 R = normalize(reflect(normalize(P), N));  // 反射方向(入射 = cam→surf = normalize(P))

    float maxDist   = ssr.uParams.x;
    int   steps     = int(ssr.uParams.y);
    float thickness = ssr.uParams.z;
    float strength  = ssr.uParams.w;
    float stepLen   = maxDist / float(max(steps, 1));

    vec3  hitColor  = vec3(0.0);
    float hitWeight = 0.0;
    for (int i = 1; i <= steps; ++i)
    {
        vec3 rayPos = P + R * (stepLen * float(i));
        if (rayPos.z >= 0.0) { break; }      // 越过相机平面(view z 正 = 相机后)

        vec4 clip = ssr.uProj * vec4(rayPos, 1.0);
        clip.xyz /= clip.w;
        vec2 uv = clip.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { break; }  // 出屏 → miss

        float sd = texture(uSceneDepth, uv).r;
        if (sd >= 0.9999) { continue; }      // 该处是天空 → 无几何，继续步进

        float sceneZ = ViewPosFromUV(uv).z;  // 该屏幕位置几何 view-z
        float diff   = sceneZ - rayPos.z;    // 射线在几何之后(更远 = 更负)→ diff>0
        if (diff > 0.0 && diff < thickness)
        {
            hitColor = texture(uHdrColor, uv).rgb;
            float NoV  = max(dot(N, V), 0.0);
            float fres = 0.04 + 0.96 * pow(1.0 - NoV, 5.0);  // Schlick(F0=0.04)
            // 边缘淡出：射线命中点接近屏幕边时反射淡出，避免硬切。
            vec2  ef   = smoothstep(vec2(0.0), vec2(0.15), uv)
                       * (1.0 - smoothstep(vec2(0.85), vec2(1.0), uv));
            float edge = ef.x * ef.y;
            hitWeight  = strength * fres * edge;
            break;
        }
    }

    outColor = vec4(hitColor * hitWeight, hitWeight);
}
