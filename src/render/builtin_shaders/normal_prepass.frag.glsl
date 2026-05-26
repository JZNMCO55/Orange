#version 450

// 内置法线预通道片元 shader：归一化插值后的 view-space 法线，编码到 RGBA8
// （n*0.5+0.5，因 unorm 只能存 [0,1]）。SSAO / SSR 采样后 *2-1 解码 + 再
// 归一化（8-bit 量化后非单位长）。
//
// 不在此做朝向翻转：cullMode=None 时背面法线会朝里，但 SSAO / SSR 消费端
// 各自已做"强制朝相机"（dot(N,-P)<0 → 翻），与之前深度差分法线的处理一致，
// 故这里只如实输出几何法线。

layout(location = 0) in  vec3 vViewNormal;
layout(location = 0) out vec4 outNormal;

void main()
{
    vec3 n = normalize(vViewNormal);
    outNormal = vec4(n * 0.5 + 0.5, 1.0);
}
