#version 450

// 自定义 fresnel shader 片段段（sample 08）—— 验证 MaterialSystem::RegisterTemplate
// 扩展点。游戏侧不修改引擎一行，自带 .vert.glsl + .frag.glsl，自带 cmake
// 调 glslangValidator 编 .spv，自带 ShaderTemplateDesc 注册到 MaterialSystem。
//
// **descriptor set 0 binding 合同**（与内置 toon / rim_light 同 layout，docs/extension-points.md 强制）：
//   binding 0 = sampler2D shadowMap
//   binding 1 = uniform LightUbo（含 lightViewProj / lightDir / cameraWorldPos / uFrameInfo.x = time）
//
// **效果**：fresnel = pow(1 - dot(N, V), kPower)。silhouette 处亮，正对相机
// 处暗。乘上 0.5 + 0.5 * sin(uTime * kPulseSpeed) 形成时间脉动；底色叠
// NdotL × shadow 给基本明暗。
//
// 限制（Phase 3 范围）：
//   * fresnel 颜色 / 幂 / 脉动速率 hardcode 在本 shader——MaterialInstance::SetUniform
//     还没接通到 push-constant 自动 packing（Pipeline 当前只支持 64/128 B 两档），
//     因此 ShaderTemplateDesc.uniforms 只声明 uMVP + uModel；per-instance fresnel 调参
//     等 Phase 6 Material UBO 上线再补。
//   * PCF shadow 采样**直接 inline** 在本 shader 里——sample 不 #include 引擎内部
//     `src/render/builtin_shaders/include/shadow_pcf.glsl.inc`（那是 engine private
//     头）。3×3 box filter 与 toon / rim_light 默认配置等价。

layout(set = 0, binding = 0) uniform sampler2D uShadowMap;

layout(set = 0, binding = 1, std140) uniform LightUbo
{
    mat4 uLightViewProj;
    vec4 uLightDirIntensity;
    vec4 uLightColor;
    vec4 uShadowParams;       // x = pcfKernelRadius, y = depthBias
    vec4 uCameraWorldPos;     // xyz = camera worldPos
    vec4 uFrameInfo;          // x = time seconds
} light;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec3 vNormal;

layout(location = 0) out vec4 outColor;

// 3×3 box-filter PCF shadow（与内置 shadow_pcf.glsl.inc 同形态，inline 一份
// 让 sample 的 shader 完全独立，不依赖 engine private 头）。
float SamplePcfShadow3x3(vec3 worldPos, mat4 lightVP, float depthBias)
{
    vec4 lightClip = lightVP * vec4(worldPos, 1.0);
    vec3 ndc       = lightClip.xyz / lightClip.w;
    vec2 uv        = ndc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0)
    {
        return 1.0;
    }
    float currentDepth = ndc.z - depthBias;
    vec2  texel        = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float closest = texture(uShadowMap, uv + vec2(x, y) * texel).r;
            shadow += (currentDepth <= closest) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void main()
{
    // world-space smooth normal（GAP-2026-05-17 起 MeshAsset 携带 per-vertex
    // normal，sphere mesh 也算好了 normalize(position)；vert 端乘 mat3(uModel)
    // 翻进 world space）。
    vec3 normal = normalize(vNormal);

    // 真 viewDir（surface → camera）。
    vec3 viewDir = normalize(light.uCameraWorldPos.xyz - vWorldPos);

    // fresnel 项。
    const float kFresnelPower     = 3.0;
    const vec3  kFresnelColor     = vec3(0.30, 0.85, 1.00);  // 冷青色，区别于 rim_light 的暖橙
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), kFresnelPower);

    // 时间脉动。kPulseSpeed = 1.8 rad/s 让 silhouette 大约 3.5s 一个完整脉动。
    const float kPulseSpeed = 1.8;
    float pulse = 0.5 + 0.5 * sin(light.uFrameInfo.x * kPulseSpeed);

    // 主光 NdotL + 阴影；fresnel 不受阴影抑制（边沿光在背光时反而更明显）。
    vec3  lightDir = normalize(-light.uLightDirIntensity.xyz);
    float NdotL    = max(dot(normal, lightDir), 0.0);
    float shadow   = SamplePcfShadow3x3(vWorldPos,
                                        light.uLightViewProj,
                                        light.uShadowParams.y);

    // 暗内（深蓝色低光）+ silhouette 亮青色脉动 rim + 主光弱衰减。
    const vec3 kAmbient = vec3(0.04, 0.05, 0.08);
    vec3 mainLit = light.uLightColor.rgb * light.uLightDirIntensity.w * NdotL * shadow;
    vec3 color   = kAmbient
                 + kFresnelColor * fresnel * (0.6 + 0.4 * pulse)
                 + mainLit * 0.20;

    outColor = vec4(color, 1.0);
}
