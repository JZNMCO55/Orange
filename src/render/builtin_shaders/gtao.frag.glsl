#version 450

// 内置 GTAO 片元 shader —— Ground Truth Ambient Occlusion（Jimenez et al.,
// "Practical Realtime Strategies for Accurate Indirect Occlusion", 2016）。
//
// 相比半球 kernel SSAO（ssao.frag）：GTAO 在屏幕空间取若干 slice，每条 slice
// 沿 ±方向 march 找最大 horizon 角，把表面法线投到 slice 平面后**解析积分**
// 余弦加权可见弧——更接近真值、噪声低、曲面接触更准。
//
// 复用 ssaoLayout（0=sceneDepth, 1=SsaoUbo, 2=normalBuffer）与 SsaoUbo 布局，
// 故必须与 C++ SsaoUboData / ssao.frag 的 std140 完全一致。GTAO 不用 kernel：
//   * uParams  : x=radius(view 米), y=bias(未用), z=strength, w=power
//   * uParams2 : x=kernelSize(未用), y=sliceCount, z=stepsPerSlice, w 预留
//
// 输出单通道 AO ∈ [0,1]（1=无遮蔽）。后续 ssao_apply 模糊 + 乘进 HDR（与
// 半球 SSAO 共用同一 apply 路径）。

#define ORANGE_SSAO_MAX_KERNEL 32  // 占位，保证 SsaoUbo std140 布局与 C++ 一致

const float kPi     = 3.14159265359;
const float kHalfPi = 1.57079632679;

layout(set = 0, binding = 0) uniform sampler2D uSceneDepth;
layout(set = 0, binding = 1, std140) uniform SsaoUbo
{
    mat4 uProj;
    mat4 uInvProj;
    vec4 uKernel[ORANGE_SSAO_MAX_KERNEL];  // GTAO 不用
    vec4 uParams;     // x=radius, y=bias, z=strength, w=power
    vec4 uParams2;    // x=kernelSize, y=sliceCount, z=stepsPerSlice, w 预留
} ssao;
layout(set = 0, binding = 2) uniform sampler2D uNormal;  // view-space 法线(n*0.5+0.5)

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

float Hash12(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 ViewPosFromUV(vec2 uv)
{
    float depth = texture(uSceneDepth, uv).r;
    vec4  ndc   = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4  vp    = ssao.uInvProj * ndc;
    return vp.xyz / vp.w;
}

void main()
{
    float depth = texture(uSceneDepth, vUV).r;
    if (depth >= 0.9999)   // 远平面 / 天空 → 全亮
    {
        outColor = vec4(1.0);
        return;
    }

    vec3 P = ViewPosFromUV(vUV);
    vec3 N = normalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);
    if (dot(N, -P) < 0.0) { N = -N; }
    vec3 V = normalize(-P);                       // 表面 → 相机

    float radius   = ssao.uParams.x;
    int   slices   = max(int(ssao.uParams2.y), 1);
    int   steps    = max(int(ssao.uParams2.z), 1);

    // view-space radius 投到屏幕（uv 单位）；近处 clamp 防超采样。
    float focal       = abs(ssao.uProj[1][1]);    // y 焦距(cot(fov/2))
    float screenRadius = clamp(radius * focal * 0.5 / max(-P.z, 1e-4), 0.002, 0.25);

    // per-pixel 随机相位抖动 slice 角，高频噪声由后续 ssao_apply box 模糊抹平。
    float jitter = Hash12(gl_FragCoord.xy);

    float visibility = 0.0;
    for (int s = 0; s < slices; ++s)
    {
        float phi   = kPi * (float(s) + jitter) / float(slices);
        vec2  omega = vec2(cos(phi), sin(phi));   // 屏幕空间 slice 方向

        // slice 平面：含 V 与 omega 抬到 view 空间的方向（image-plane 近似，
        // XeGTAO 同款）。注意主 pass 投影含 y-flip(p[1][1]=-f)，屏幕 uv +y 对应
        // view -y，故 omega.y 取负，否则 view-space slice 方向与实际 uv march
        // 方向 y 相反 → 切线基线错位 → 全局过暗。
        vec3 dirView      = vec3(omega.x, -omega.y, 0.0);
        vec3 orthoDir     = dirView - dot(dirView, V) * V;
        vec3 axis         = normalize(cross(orthoDir, V));   // slice 平面法线
        vec3 projN        = N - dot(N, axis) * axis;         // N 投到 slice 平面
        float projLen     = length(projN);
        if (projLen < 1e-4) { continue; }
        vec3  projNn      = projN / projLen;

        // 投影法线相对 V 的带符号夹角 n。
        float cosN = clamp(dot(projNn, V), -1.0, 1.0);
        float n    = sign(dot(orthoDir, projNn)) * acos(cosN);

        // 表面切线基线：horizon 初值取 slice 内的表面切线（⊥ N），而非盲设
        // -1。共面采样点（同平面）落在切线 = 法线半球边界 → 不产生额外遮蔽，
        // 消除平面自遮蔽（horizon-AO 的经典陷阱）。
        vec3  sliceT = normalize(cross(axis, N));    // slice 平面内的表面切线
        float sgn    = sign(dot(sliceT, orthoDir));  // 对齐到 +omega 侧
        float cTan1  = dot( sgn * sliceT, V);   // +omega 侧切线基线 cos
        float cTan2  = dot(-sgn * sliceT, V);   // -omega 侧切线基线 cos
        float cH1 = cTan1;
        float cH2 = cTan2;
        for (int t = 1; t <= steps; ++t)
        {
            float frac = (float(t) - 0.5) / float(steps);
            vec2  off  = omega * frac * screenRadius;

            vec3  d1   = ViewPosFromUV(vUV + off) - P;
            float l1   = length(d1);
            float fo1  = clamp(1.0 - (l1 * l1) / (radius * radius), 0.0, 1.0);
            float cd1  = dot(d1, V) / max(l1, 1e-5);
            cH1 = max(cH1, mix(cTan1, cd1, fo1));   // 远样本被 falloff 拉回切线基线

            vec3  d2   = ViewPosFromUV(vUV - off) - P;
            float l2   = length(d2);
            float fo2  = clamp(1.0 - (l2 * l2) / (radius * radius), 0.0, 1.0);
            float cd2  = dot(d2, V) / max(l2, 1e-5);
            cH2 = max(cH2, mix(cTan2, cd2, fo2));
        }

        // horizon 角，clamp 到法线半球 [n-PI/2, n+PI/2]。
        float h1 = n + clamp( acos(clamp(cH1, -1.0, 1.0)) - n, -kHalfPi, kHalfPi);
        float h2 = n + clamp(-acos(clamp(cH2, -1.0, 1.0)) - n, -kHalfPi, kHalfPi);

        // 余弦加权可见弧解析积分（Activision GTAO 内积分式）。
        float arc = 0.25 * (
            (-cos(2.0 * h1 - n) + cos(n) + 2.0 * h1 * sin(n)) +
            (-cos(2.0 * h2 - n) + cos(n) + 2.0 * h2 * sin(n)));
        visibility += projLen * arc;
    }
    visibility /= float(slices);

    float ao = clamp(visibility, 0.0, 1.0);
    ao = pow(ao, ssao.uParams.w);              // power 提对比
    ao = mix(1.0, ao, ssao.uParams.z);         // strength 整体强度
    outColor = vec4(vec3(ao), 1.0);
}
