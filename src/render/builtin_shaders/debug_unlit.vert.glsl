#version 450

// debug-view unlit 顶点 shader（DebugViewMode::Unlit）：直出 material base color，
// 无光照。push constant 复用 PBR {uMVP, uModel, uBaseColor, uMRA} = 160B
// （drawable loop 的 pcSize>=160 分支喂 drawable material instance 的 uBaseColor
// override，即 drawable 的 albedo）。inUV / inNormal / uMRA 读入但 unlit 不用——
// 透传保证 location 1/2 被 vertex shader 消费，不触发 unconsumed 警告。

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec4 vBaseColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vNormal;

layout(push_constant, std430) uniform Push {
    mat4 uMVP;        //   0  64
    mat4 uModel;      //  64  64
    vec4 uBaseColor;  // 128  16
    vec4 uMRA;        // 144  16
} pc;

void main()
{
    vBaseColor  = pc.uBaseColor;
    vUV         = inUV;                        // 透传保证 location 1 消费
    vNormal     = mat3(pc.uModel) * inNormal;  // 透传保证 location 2 消费
    gl_Position = pc.uMVP * vec4(inPosition, 1.0);
}
