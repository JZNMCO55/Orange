// BuiltinAssets 实现 —— 见 BuiltinAssets.h 注释。v1.0.1 c11 从 DemoWorld.cpp
// 拆出"启动期必备 builtin 资产"层（mesh 工厂 + InitializeEditorAssets +
// BuildNamedMaterialInstances）。零行为变化。

#include "BuiltinAssets.h"

#include "MaterialFileIO.h"

#include <orange/engine/core/Log.h>

#include <orange/engine/animation/AnimatorRegistry.h>
#include <orange/engine/animation/IAnimator.h>
#include <orange/engine/animation/ProceduralAnimator.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshLoader.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/asset/SkeletonAsset.h>
#include <orange/engine/asset/SkeletonLoader.h>
#include <orange/engine/asset/SoundAsset.h>
#include <orange/engine/asset/SoundLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/asset/TextureLoader.h>
#include <orange/engine/render/MaterialSystem.h>

#include <glm/vec4.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

// 内联的"叮"声 16-bit PCM WAV 生成 —— 与 samples/common/BeepWav.h 同算法，
// 复制一份避免跨目录 include（samples/common 不在编辑器 target include
// path 上）。生成的字节直接写盘 → assets/sounds/beep.wav，与 mesh
// lazy bake 同模式。
inline void AppendU32LE(std::vector<std::uint8_t>& buf, std::uint32_t v)
{
    buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}
inline void AppendU16LE(std::vector<std::uint8_t>& buf, std::uint16_t v)
{
    buf.push_back(static_cast<std::uint8_t>(v & 0xFF));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
inline void AppendBytes(std::vector<std::uint8_t>& buf, const char* s, std::size_t n)
{
    for (std::size_t i = 0; i < n; ++i)
    {
        buf.push_back(static_cast<std::uint8_t>(s[i]));
    }
}
std::vector<std::uint8_t> MakeBeepWavBytes(float        frequencyHz = 880.0f,
                                           int          durationMs  = 180,
                                           std::uint32_t sampleRate = 44100,
                                           float        volume      = 0.5f)
{
    const std::uint16_t channels      = 1;
    const std::uint16_t bitsPerSample = 16;
    const std::uint16_t blockAlign    = channels * (bitsPerSample / 8);
    const std::uint32_t byteRate      = sampleRate * blockAlign;
    const std::uint32_t numSamples    =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(sampleRate) *
                                   static_cast<std::uint64_t>(durationMs) / 1000ULL);
    const std::uint32_t dataSize      = numSamples * blockAlign;
    const std::uint32_t fmtChunkSize  = 16;
    const std::uint32_t riffSize      = 4 + (8 + fmtChunkSize) + (8 + dataSize);

    std::vector<std::uint8_t> buf;
    buf.reserve(8 + riffSize);
    AppendBytes(buf, "RIFF", 4);
    AppendU32LE(buf, riffSize);
    AppendBytes(buf, "WAVE", 4);
    AppendBytes(buf, "fmt ", 4);
    AppendU32LE(buf, fmtChunkSize);
    AppendU16LE(buf, 1);
    AppendU16LE(buf, channels);
    AppendU32LE(buf, sampleRate);
    AppendU32LE(buf, byteRate);
    AppendU16LE(buf, blockAlign);
    AppendU16LE(buf, bitsPerSample);
    AppendBytes(buf, "data", 4);
    AppendU32LE(buf, dataSize);

    const float twoPi = 6.28318530717958647692f;
    for (std::uint32_t i = 0; i < numSamples; ++i)
    {
        const float t        = static_cast<float>(i) / static_cast<float>(sampleRate);
        const float envelope = 1.0f - static_cast<float>(i) / static_cast<float>(numSamples);
        const float sample   = std::sin(t * twoPi * frequencyHz) * volume * envelope;
        const std::int16_t s = static_cast<std::int16_t>(sample * 32767.0f);
        AppendU16LE(buf, static_cast<std::uint16_t>(s));
    }
    return buf;
}

}  // anonymous namespace

std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakePlaneMesh(float halfSize)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    std::vector<VertexPosition3> positions = {
        {-halfSize, 0.0f, -halfSize},
        { halfSize, 0.0f, -halfSize},
        { halfSize, 0.0f,  halfSize},
        {-halfSize, 0.0f,  halfSize},
    };
    std::vector<VertexUV2> uvs = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f},
    };
    std::vector<std::uint32_t> indices = {0, 2, 1, 0, 3, 2};
    auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                             std::move(uvs),
                                             std::move(indices));
    // GAP-2026-05-17：lazy bake 路径写盘前补算 smooth normal —— 让
    // 首次产出的 assets/meshes/plane.mesh 直接是 v3 带 normal 版本；
    // 既有 v2 文件不会被改写，Load 时由 MeshLoader fallback 补算，
    // 渲染端始终拿到非空 Normals()。
    pMesh->ComputeSmoothNormalsFromTriangles();
    return pMesh;
}

// 内置 cube mesh（6 面 × 4 顶点，共 24 vertices / 12 triangles）。每面单独
// 一组顶点是为了让 UV 在 face 边界不连续 —— textured material 在 face 间
// 看起来才正常（共享 8 顶点的方案 UV 必然拉伸 / 接缝错位）。
//
// **face normal 显式 push**（不依赖 ComputeSmoothNormalsFromTriangles）：
// MakeCubeMesh 的 quad 顶点顺序与 ComputeSmoothNormalsFromTriangles 的 cross
// 约定（Cross(b - a, c - a)）反向 —— smooth-from-triangles 算出的 normal
// 指向 cube 内部，PBR `max(0, NoL)` 直接 zero out direct light 让 cube 几乎
// 全黑；toon 走 fallback kCool 蓝色看着正常（实际是 NoL ≤ 0 的 brunched
// 分支）。修法：让 MakeCubeMesh 自己显式写 6 面 face normal 朝外，绕开
// 算法约定问题。重 bake 后 cube.mesh v3 段直接持正确 normal。
std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeCubeMesh(float halfSize)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexNormal3;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    const float h = halfSize;
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<VertexNormal3>   normals;
    std::vector<std::uint32_t>   indices;
    positions.reserve(24);
    uvs.reserve(24);
    normals.reserve(24);
    indices.reserve(36);

    auto addFace = [&](VertexPosition3 a, VertexPosition3 b,
                       VertexPosition3 c, VertexPosition3 d,
                       VertexNormal3 faceNormal) {
        const std::uint32_t base = static_cast<std::uint32_t>(positions.size());
        positions.push_back(a); positions.push_back(b);
        positions.push_back(c); positions.push_back(d);
        uvs.push_back({0.0f, 0.0f}); uvs.push_back({1.0f, 0.0f});
        uvs.push_back({1.0f, 1.0f}); uvs.push_back({0.0f, 1.0f});
        normals.push_back(faceNormal); normals.push_back(faceNormal);
        normals.push_back(faceNormal); normals.push_back(faceNormal);
        // v1.0.1 c7：triangle winding 修复 —— 原 0-2-1 / 0-3-2 是 CW
        // （从 face 外侧朝内看顺时针），主 pass FrontFace = CCW + CullMode
        // = Back 把"朝外的 face"全部剔除，只渲染朝内壁的面 → "cube
        // 透过正面看到内部"视觉 bug。v1.0.1 之前 dummy IBL 0.25 灰整体偏暗，
        // 内壁亮度低被误解为"暗面"；v1.0.1 c5 把 fallback 提到 0.5 灰后
        // 内壁亮度提高，bug 立显。
        //
        // 修法：改为 CCW 0-1-2 / 0-2-3，让 face 的两个三角形按 (a→b→c) +
        // (a→c→d) 绕 face normal 反方向（即从 normal 朝向看 CCW）排列，
        // 主 pass 正确把"朝外"识别为 front face 通过 cull 测试。
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    };

    // 6 面 + 显式朝外 face normal。winding 已修为 CCW（与 Vulkan 标准
    // FrontFace = CounterClockwise 约定一致），主 pass Back-face cull 正确
    // 保留朝外面、剔除朝内面。
    addFace({ h,-h, h}, { h,-h,-h}, { h, h,-h}, { h, h, h}, { 1.0f, 0.0f, 0.0f});  // +X
    addFace({-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h}, {-1.0f, 0.0f, 0.0f});  // -X
    addFace({-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h}, { 0.0f, 1.0f, 0.0f});  // +Y (top)
    addFace({-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h}, { 0.0f,-1.0f, 0.0f});  // -Y (bottom)
    addFace({-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}, { 0.0f, 0.0f, 1.0f});  // +Z
    addFace({ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h}, { 0.0f, 0.0f,-1.0f});  // -Z

    return std::make_unique<MeshAsset>(std::move(positions),
                                       std::move(uvs),
                                       std::move(normals),
                                       std::move(indices));
}

// lat/lon UV-sphere（共享顶点路径下 ComputeSmoothNormalsFromTriangles 自然
// 得到 normalize(position) 平滑法线）。与 sample 13_pbr_direct / 14_pbr_ibl
// 同款构造。
std::unique_ptr<Orange::Engine::Asset::MeshAsset>
MakeSphereMesh(float radius, std::uint32_t lon, std::uint32_t lat)
{
    using ::Orange::Engine::Asset::MeshAsset;
    using ::Orange::Engine::Asset::VertexPosition3;
    using ::Orange::Engine::Asset::VertexUV2;

    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;
    const float kPi = 3.14159265358979323846f;
    for (std::uint32_t i = 0; i <= lat; ++i)
    {
        const float v     = static_cast<float>(i) / static_cast<float>(lat);
        const float theta = v * kPi;
        const float sinT  = std::sin(theta);
        const float cosT  = std::cos(theta);
        for (std::uint32_t j = 0; j <= lon; ++j)
        {
            const float u    = static_cast<float>(j) / static_cast<float>(lon);
            const float phi  = u * 2.0f * kPi;
            const float sinP = std::sin(phi);
            const float cosP = std::cos(phi);
            positions.push_back({radius * sinT * cosP,
                                 radius * cosT,
                                 radius * sinT * sinP});
            uvs.push_back({u, 1.0f - v});
        }
    }
    for (std::uint32_t i = 0; i < lat; ++i)
    {
        for (std::uint32_t j = 0; j < lon; ++j)
        {
            const std::uint32_t a = i       * (lon + 1) + j;
            const std::uint32_t b = (i + 1) * (lon + 1) + j;
            const std::uint32_t c = (i + 1) * (lon + 1) + (j + 1);
            const std::uint32_t d = i       * (lon + 1) + (j + 1);
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
            indices.push_back(a); indices.push_back(d); indices.push_back(c);
        }
    }
    auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                             std::move(uvs),
                                             std::move(indices));
    // 球面共享顶点，smooth-from-triangles 算出 normalize(position) 的近似
    // 平滑法线 —— 与 cube 不同，sphere 顶点序列与算法约定一致，输出方向
    // 朝外（指向球心外）。
    pMesh->ComputeSmoothNormalsFromTriangles();
    return pMesh;
}

// 一次性建好 AssetRegistry + 注册 ShaderLoader + 内置 mesh + MaterialSystem
// + 所有内置材质实例。失败仅 log，不抛；SeedDemoWorld 仍能工作（Renderable
// 退化到 nullptr material），只是 Scene 视口看不到几何。
void InitializeEditorAssets(EditorHost& host)
{
    using Orange::Engine::Asset::AssetRegistry;
    using Orange::Engine::Asset::MeshAsset;
    using Orange::Engine::Asset::ShaderAsset;
    using Orange::Engine::Asset::ShaderLoader;
    using Orange::Engine::Render::MaterialSystem;

    host.assets.pAssets = std::make_unique<AssetRegistry>();
    if (auto reg = host.assets.pAssets->RegisterLoader<ShaderAsset>(
            std::make_unique<ShaderLoader>());
        reg.IsErr())
    {
        ORANGE_LOG_ERROR("[OrangeEditor] AssetRegistry::RegisterLoader<ShaderAsset> 失败 "
                         "(code={})",
                         static_cast<unsigned>(reg.Error()));
    }
    // GAP-2026-05-16 G1：注册 MeshLoader 让 RenderableComponent.mesh 字段
    // 走 "assets/meshes/*.mesh" 磁盘路径 Load 路径（取代旧的内存 named
    // "editor/cube" Insert 路径）。MeshLoader v2 支持 UV 段（同 commit 落
    // 地的引擎扩展），textured / toon material 在烘焙后的 .mesh 上 UV 不
    // 丢失。
    using Orange::Engine::Asset::MeshLoader;
    if (auto reg = host.assets.pAssets->RegisterLoader<MeshAsset>(
            std::make_unique<MeshLoader>());
        reg.IsErr())
    {
        ORANGE_LOG_ERROR("[OrangeEditor] AssetRegistry::RegisterLoader<MeshAsset> 失败 "
                         "(code={})",
                         static_cast<unsigned>(reg.Error()));
    }

    // 注册 TextureLoader 让 EnvironmentComponent.cubemap 字段 / 任何
    // AssetRef(Texture) 字段都能走 "assets/environments/*.hdr" 磁盘路径 Load。
    // 漏注册时 cubemap path 写入 schema set 路径调 Load<TextureAsset> 立刻返
    // 回 NotRegistered，handle 永远 invalid，Pipeline re-bake 看不到变化 →
    // 用户视觉上等同 "Environment 拖放和数值调节都没反应"。原 GAP-2026-05-19
    // -editor-environment-component-wiring fix 漏了这一步：当时只看 sample
    // 14_pbr_ibl 通了（sample 自己注册了 TextureLoader），编辑器没真走 GUI 验。
    using Orange::Engine::Asset::TextureAsset;
    using Orange::Engine::Asset::TextureLoader;
    if (auto reg = host.assets.pAssets->RegisterLoader<TextureAsset>(
            std::make_unique<TextureLoader>());
        reg.IsErr())
    {
        ORANGE_LOG_ERROR("[OrangeEditor] AssetRegistry::RegisterLoader<TextureAsset> 失败 "
                         "(code={})",
                         static_cast<unsigned>(reg.Error()));
    }

    // v0.7 c3：注册 SkeletonLoader 让 DragonBonesAssetInspectorPlugin
    // 可加载 _ske.json / _ske.dbbin 资源浏览 metadata。无参 ctor 内部
    // 自管 DragonBonesContext（c3 同 commit 落地的公共面扩展），公共
    // consumer 不需关心 context 生命周期。
    using Orange::Engine::Asset::SkeletonAsset;
    using Orange::Engine::Asset::SkeletonLoader;
    if (auto reg = host.assets.pAssets->RegisterLoader<SkeletonAsset>(
            std::make_unique<SkeletonLoader>());
        reg.IsErr())
    {
        ORANGE_LOG_ERROR("[OrangeEditor] AssetRegistry::RegisterLoader<SkeletonAsset> 失败 "
                         "(code={})",
                         static_cast<unsigned>(reg.Error()));
    }

    // 注册 SoundLoader —— AudioSource schema 的 sound AssetRef 字段 +
    // AudioAssetInspectorPlugin 资源预览 + Play Mode 实例化路径全数走
    // AssetRegistry::Load<SoundAsset>(path)，没注册则全部 fail-silent。
    using Orange::Engine::Asset::SoundAsset;
    using Orange::Engine::Asset::SoundLoader;
    if (auto reg = host.assets.pAssets->RegisterLoader<SoundAsset>(
            std::make_unique<SoundLoader>());
        reg.IsErr())
    {
        ORANGE_LOG_ERROR("[OrangeEditor] AssetRegistry::RegisterLoader<SoundAsset> 失败 "
                         "(code={})",
                         static_cast<unsigned>(reg.Error()));
    }

    // 内置 mesh lazy bake：检测 assets/meshes/X.mesh，缺失则程序化构造 +
    // MeshLoader::Save 写盘后再 Load；存在直接 Load。lazy bake 让首次跑
    // OrangeEditor 自动产出 .mesh 文件让开发者手动 git add commit 入仓；
    // 之后 CI 跑或其他人拉仓直接走盘上的 .mesh。
    //
    // 不在 InitializeEditorAssets 内 fall back 到 Insert("editor/cube",...)
    // 路径——那是 G1 之前的兼容残留，G1 ✅ 后所有 mesh 引用必须走磁盘路径。
    auto bakeIfMissingThenLoad = [&](const std::string& path,
                                     auto buildFn) -> Orange::Engine::Asset::AssetHandle<MeshAsset>
    {
        if (!std::filesystem::exists(path))
        {
            auto pMesh = buildFn();
            if (pMesh != nullptr)
            {
                // 确保目录存在；MeshLoader::Save 不创建目录。
                std::filesystem::create_directories(
                    std::filesystem::path(path).parent_path());
                if (auto sv = MeshLoader::Save(path, *pMesh); sv.IsErr())
                {
                    ORANGE_LOG_ERROR("[OrangeEditor] MeshLoader::Save '{}' 失败 (code={})",
                                     path,
                                     static_cast<unsigned>(sv.Error()));
                    // 仍然 Insert 一份内存版本作为最后兜底，让本次会话能继续渲染
                    if (auto h = host.assets.pAssets->Insert<MeshAsset>(path, std::move(pMesh));
                        h.IsOk())
                    {
                        return h.Value();
                    }
                    return {};
                }
            }
        }
        auto lr = host.assets.pAssets->Load<MeshAsset>(path);
        if (lr.IsErr())
        {
            ORANGE_LOG_ERROR("[OrangeEditor] Load<MeshAsset> '{}' 失败 (code={})",
                             path,
                             static_cast<unsigned>(lr.Error()));
            return {};
        }
        return lr.Value();
    };

    host.assets.cubeMeshHandle  = bakeIfMissingThenLoad(
        "assets/meshes/cube.mesh",  [] { return MakeCubeMesh(0.5f); });
    host.assets.planeMeshHandle = bakeIfMissingThenLoad(
        "assets/meshes/plane.mesh", [] { return MakePlaneMesh(2.5f); });
    // PBR showcase 用的 sphere mesh —— 与 sample 13_pbr_direct / 14_pbr_ibl
    // 同款半径 0.5、lon 32 / lat 16 lat/lon tessellation。
    host.assets.sphereMeshHandle = bakeIfMissingThenLoad(
        "assets/meshes/sphere.mesh", [] { return MakeSphereMesh(0.5f, 32u, 16u); });

    // 内置 beep.wav lazy bake —— 与 mesh lazy bake 同模式：检测
    // assets/sounds/beep.wav 缺失则用 BeepWav helper 程序生成 16-bit PCM
    // 字节 + 写盘。让首次跑 OrangeEditor 自动产出 .wav 让开发者 git add
    // 入仓；之后拉仓直接走盘上文件。Asset 浏览器扫描 assets/ 即看到
    // [SND] beep.wav 可拖入 AudioSource.sound 字段或选中预览试播。
    {
        const std::filesystem::path beepPath = "assets/sounds/beep.wav";
        if (!std::filesystem::exists(beepPath))
        {
            std::filesystem::create_directories(beepPath.parent_path());
            const auto bytes = MakeBeepWavBytes(/*freq=*/880.0f,
                                                /*durationMs=*/180,
                                                /*sampleRate=*/44100,
                                                /*volume=*/0.5f);
            // 用 ofstream 避免 MSVC 把 std::fopen 视作 deprecated（_CRT
            // _SECURE_NO_WARNINGS 是工程级抑制开关，单点写盘走 ofstream
            // 更对题）。
            std::ofstream ofs(beepPath, std::ios::binary | std::ios::trunc);
            if (ofs.is_open())
            {
                ofs.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
            }
            else
            {
                ORANGE_LOG_ERROR("[OrangeEditor] lazy bake beep.wav 写盘失败 '{}'",
                                 beepPath.string());
            }
        }
    }

    host.assets.pMaterials = std::make_unique<MaterialSystem>(*host.assets.pAssets);
    // v1.2 T1：从 `assets/shaders/templates/*.template.json` 数据驱动注册
    // 6 个内置 template（pbr / textured / toon / rim_light / dissolve /
    // emissive），与历史 RegisterBuiltins() 行为完全等价。.template.json
    // 内 SPIR-V 路径走 .exe-相对（GetExecutableDir + shaders/orange_engine/
    // <name>.spv），由 RegisterTemplatesFromDirectory 内部 ResolveSpvPath
    // 统一解析。失败语义：单个 .template.json 解析失败 / SPIR-V 加载失败
    // 时该 template 半残落入表（与 RegisterBuiltins 旧路径一致），仍允许
    // 编辑器启动；整体 IoError 仅 log 不阻塞。
    {
        const std::filesystem::path templatesDir = "assets/shaders/templates";
        auto rb = host.assets.pMaterials->RegisterTemplatesFromDirectory(templatesDir);
        if (rb.IsErr())
        {
            ORANGE_LOG_WARN("[OrangeEditor] MaterialSystem::RegisterTemplatesFromDirectory "
                            "失败 (dir={}, code={}) —— Scene 视口稍后可能不显示几何",
                            templatesDir.string(),
                            static_cast<unsigned>(rb.Error()));
        }
    }

    // GAP-2026-05-16 G2：内置 MaterialInstance 落盘 .material + lazy bake。
    // schema v1.1 详见 tools/OrangeEditor/MaterialFileIO.h。
    //
    // 不引入 IAssetLoader<MaterialInstance>：MaterialInstance 构造依赖
    // MaterialSystem& 注入，IAssetLoader 接口没这种容器；走 helper 函数
    // 路径足够简单。namedMaterialInstances 仍是 host.assets 自己拥有 +
    // BuildNamedMaterialInstances 返回 path→ptr 映射。
    auto bakeAndLoadMaterial = [&](const std::string& path,
                                   std::string_view fallbackTemplate)
        -> std::unique_ptr<::Orange::Engine::Render::MaterialInstance>
    {
        // 文件不存在：用 fallbackTemplate 写一份默认 v1.1（uniforms /
        // textures 为空）落盘，避免下次启动时再走 fallback。
        if (!std::filesystem::exists(path))
        {
            std::filesystem::create_directories(
                std::filesystem::path(path).parent_path());
            ::Orange::Editor::Material::MaterialFileData defaultData;
            defaultData.templateName = std::string{fallbackTemplate};
            ::Orange::Editor::Material::WriteMaterialFile(path, defaultData);
        }

        auto dataOpt = ::Orange::Editor::Material::ReadMaterialFile(path);
        if (!dataOpt.has_value())
        {
            ORANGE_LOG_WARN("[OrangeEditor] .material '{}' 读取失败，回退到 '{}'",
                            path,
                            fallbackTemplate);
            return host.assets.pMaterials->CreateInstance(fallbackTemplate);
        }

        auto inst = host.assets.pMaterials->CreateInstance(dataOpt->templateName);
        if (inst == nullptr)
        {
            ORANGE_LOG_WARN("[OrangeEditor] .material '{}' template '{}' 未注册，回退",
                            path,
                            dataOpt->templateName);
            return host.assets.pMaterials->CreateInstance(fallbackTemplate);
        }
        ::Orange::Editor::Material::ApplyDataToInstance(
            *dataOpt, *inst, host.assets.pAssets.get());
        return inst;
    };

    // 地面 / 备用 textured 实例（路径风格 ID，namedMaterialInstances 用同款 key）
    host.assets.pFloorMaterial = bakeAndLoadMaterial(
        "assets/materials/builtin/floor.material", "textured");
    host.assets.pWallMaterial  = bakeAndLoadMaterial(
        "assets/materials/builtin/wall.material",  "textured");

    // v0.1.5 内置材质（失败时 unique_ptr 为 nullptr，Renderable 降级）
    host.assets.pToonMaterial     = bakeAndLoadMaterial(
        "assets/materials/builtin/toon.material",      "toon");
    host.assets.pRimLightMaterial = bakeAndLoadMaterial(
        "assets/materials/builtin/rim_light.material", "rim_light");
    host.assets.pDissolveMaterial = bakeAndLoadMaterial(
        "assets/materials/builtin/dissolve.material",  "dissolve");
    host.assets.pPbrMaterial      = bakeAndLoadMaterial(
        "assets/materials/builtin/pbr.material",       "pbr");

    // 编辑器默认 / 光物体材质
    host.assets.pDefaultRenderableMaterial = bakeAndLoadMaterial(
        "assets/materials/builtin/default.material",      "textured");
    host.assets.pLightObjectMaterial       = bakeAndLoadMaterial(
        "assets/materials/builtin/light_object.material", "emissive");

    // PBR showcase 18 个 MaterialInstance —— 两组 3×3 球阵的 per-instance
    // 配置。lazy bake 写到 assets/materials/pbr_showcase/，pbr_showcase.scene.json
    // 通过 materialInstanceId 字符串引用。第一次启动时若 .material 不存在则
    // 程序化写盘 + 覆盖 baseColor / uMRA uniform；后续启动直接从盘加载。
    //
    // 用户自行删 assets/materials/pbr_showcase/ 后，下次启动会重 bake 出
    // 默认值（baseColor 暖橙 / 白 + MRA 9 组合）；用户在 Inspector 改过
    // 的覆盖值已落盘 .material 文件，重 bake 不会回滚。
    //
    // baseColor 暖橙 (1.0, 0.78, 0.34) 是 sample 13_pbr_direct 同款；white
    // (1, 1, 1) 是 sample 14_pbr_ibl furnace 测试同款。
    {
        using ::Orange::Engine::Render::MaterialInstance;
        constexpr float kMetallicSteps[3]  = {0.0f, 0.5f, 1.0f};
        constexpr float kRoughnessSteps[3] = {0.1f, 0.5f, 0.9f};
        struct VariantDef
        {
            const char* keyPrefix;
            float       baseColor[4];
        };
        const VariantDef kVariants[2] = {
            {"warm",  {1.00f, 0.78f, 0.34f, 1.0f}},  // 暖橙 (13_pbr_direct)
            {"white", {1.00f, 1.00f, 1.00f, 1.0f}},  // 白 (14_pbr_ibl furnace)
        };

        host.assets.pbrShowcaseMaterials.reserve(18);
        host.assets.pbrShowcaseMaterialPaths.reserve(18);

        for (const auto& variant : kVariants)
        {
            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t col = 0; col < 3; ++col)
                {
                    char relPath[128];
                    std::snprintf(relPath, sizeof(relPath),
                                  "assets/materials/pbr_showcase/%s_m%zur%zu.material",
                                  variant.keyPrefix, row, col);

                    // lazy bake：文件不存在则程序化写一份带 uniform override 的
                    // .material 落盘，下次启动直接 Load 用户已编辑值。
                    if (!std::filesystem::exists(relPath))
                    {
                        std::filesystem::create_directories(
                            std::filesystem::path(relPath).parent_path());
                        ::Orange::Editor::Material::MaterialFileData data;
                        data.templateName = "pbr";
                        ::Orange::Editor::Material::UniformOverrideValue uBC;
                        uBC.name = "uBaseColor";
                        uBC.type = ::Orange::Engine::Render::MaterialUniformType::Vec4;
                        uBC.value = glm::vec4(variant.baseColor[0],
                                              variant.baseColor[1],
                                              variant.baseColor[2],
                                              variant.baseColor[3]);
                        data.uniforms.push_back(uBC);
                        ::Orange::Editor::Material::UniformOverrideValue uMRA;
                        uMRA.name = "uMRA";
                        uMRA.type = ::Orange::Engine::Render::MaterialUniformType::Vec4;
                        uMRA.value = glm::vec4(kMetallicSteps[row],
                                               kRoughnessSteps[col],
                                               1.0f,
                                               0.0f);
                        data.uniforms.push_back(uMRA);
                        ::Orange::Editor::Material::WriteMaterialFile(relPath, data);
                    }

                    auto inst = bakeAndLoadMaterial(relPath, "pbr");
                    if (inst != nullptr)
                    {
                        host.assets.pbrShowcaseMaterials.push_back(std::move(inst));
                        host.assets.pbrShowcaseMaterialPaths.emplace_back(relPath);
                    }
                }
            }
        }
    }

    // AnimatorRegistry —— Scene::Load 遇到 AnimatorComponent 时通过 backend
    // name 查 factory 创建 IAnimator。当前只注册引擎自带 "procedural" 后端；
    // dragonbones 后端依赖 DragonBonesContext + skeleton asset，编辑器 demo
    // 暂不消费，等 v0.7 Animation 子模式上线后再注册。
    //
    // factory 捕获 dissolve material 的裸指针——pDissolveMaterial 由本
    // context 拥有，生命周期 ≥ AnimatorRegistry，指针稳定。channel 列表
    // 故意只塞一条占位 dissolve_t，目的是让 c4 IEditorInspectorPlugin
    // mini-preview 能读到 ChannelCount > 0；UBO 通路未接通前 channel 写入
    // 不会真正影响 GPU 端 uniform（参 ProceduralAnimator.h 头注释）。
    host.assets.pAnimators = std::make_unique<Orange::Engine::Animation::AnimatorRegistry>();
    {
        auto* pDissolveTarget = host.assets.pDissolveMaterial.get();
        auto factory = [pDissolveTarget]()
            -> std::unique_ptr<Orange::Engine::Animation::IAnimator>
        {
            auto anim = std::make_unique<
                Orange::Engine::Animation::ProceduralAnimator>(pDissolveTarget);
            anim->AddChannel<float>("dissolve_t",
                                    [](float t) { return t * 0.5f; });
            return anim;
        };
        if (auto rb = host.assets.pAnimators->RegisterBackend("procedural", factory);
            rb.IsErr())
        {
            ORANGE_LOG_ERROR("[OrangeEditor] AnimatorRegistry::RegisterBackend(procedural) "
                             "失败 (code={})",
                             static_cast<unsigned>(rb.Error()));
        }
    }
}

std::unordered_map<std::string, Orange::Engine::Render::MaterialInstance*>
BuildNamedMaterialInstances(const EditorAssetContext& assets)
{
    using Orange::Engine::Render::MaterialInstance;
    std::unordered_map<std::string, MaterialInstance*> m;
    // GAP-2026-05-16 G2：key 从 "builtin/X" 改成磁盘路径 "assets/materials/
    // builtin/X.material"，与 InitializeEditorAssets 内 lazy bake 路径一致。
    // Scene Save/Load 路径 RenderableComponent.materialInstanceId 字段值
    // 同款迁移；ReadRenderable 内有 mapping fallback 容旧 ID。
    if (assets.pFloorMaterial)
        m["assets/materials/builtin/floor.material"]        = assets.pFloorMaterial.get();
    if (assets.pWallMaterial)
        m["assets/materials/builtin/wall.material"]         = assets.pWallMaterial.get();
    if (assets.pToonMaterial)
        m["assets/materials/builtin/toon.material"]         = assets.pToonMaterial.get();
    if (assets.pPbrMaterial)
        m["assets/materials/builtin/pbr.material"]          = assets.pPbrMaterial.get();
    if (assets.pRimLightMaterial)
        m["assets/materials/builtin/rim_light.material"]    = assets.pRimLightMaterial.get();
    if (assets.pDissolveMaterial)
        m["assets/materials/builtin/dissolve.material"]     = assets.pDissolveMaterial.get();
    if (assets.pDefaultRenderableMaterial)
        m["assets/materials/builtin/default.material"]      = assets.pDefaultRenderableMaterial.get();
    if (assets.pLightObjectMaterial)
        m["assets/materials/builtin/light_object.material"] = assets.pLightObjectMaterial.get();

    // PBR showcase 18 个 material —— 与 pbr_showcase.scene.json 的 Renderable
    // materialInstanceId 字段一一对应。InitializeEditorAssets 内 pbrShowcaseMaterials
    // 与 pbrShowcaseMaterialPaths 同 index 维护。
    for (std::size_t i = 0; i < assets.pbrShowcaseMaterials.size()
                         && i < assets.pbrShowcaseMaterialPaths.size(); ++i)
    {
        if (assets.pbrShowcaseMaterials[i])
        {
            m[assets.pbrShowcaseMaterialPaths[i]] = assets.pbrShowcaseMaterials[i].get();
        }
    }

    // v1.2.2 patch · 用户新建（v1.1.1 Create Material UI）/ 手动 copy 进
    // assets/ / 老 .material 等通过 MaterialAssetInspectorPlugin lazy
    // CreateInstance own 到 userMaterials 的实例。同 key 已被前面的 8 个
    // hardcode / PBR showcase 抢占时（罕见，用户故意用同名 path）以前面
    // 的为准；正常路径下 userMaterials 内的 path 与前面 hardcode 完全不
    // 重叠。
    for (const auto& [p, ptr] : assets.userMaterials)
    {
        if (ptr && m.find(p) == m.end()) { m[p] = ptr.get(); }
    }
    return m;
}

Orange::Engine::Render::MaterialInstance*
EnsureMaterialInstance(EditorHost& host, const std::string& materialPath)
{
    // 1) 先查既有（含 8 内置 + PBR showcase 18 + userMaterials 已 lazy 过的）
    const auto named = BuildNamedMaterialInstances(host.assets);
    auto it = named.find(materialPath);
    if (it != named.end()) { return it->second; }

    // 2) 不在 map → lazy create 兜底（v1.2.2 同款路径，提到 helper 让
    //    Inspector / DnD apply 两处都能复用，避免 v1.2.3 验收 bug 复现）。
    if (host.assets.pMaterials == nullptr)
    {
        ORANGE_LOG_WARN("EnsureMaterialInstance: MaterialSystem 未就绪 '{}'",
                        materialPath);
        return nullptr;
    }
    auto dataOpt = ::Orange::Editor::Material::ReadMaterialFile(materialPath);
    if (!dataOpt.has_value() || dataOpt->templateName.empty())
    {
        ORANGE_LOG_WARN("EnsureMaterialInstance: .material 解析失败或 "
                        "templateName 缺失 '{}'", materialPath);
        return nullptr;
    }
    auto inst = host.assets.pMaterials->CreateInstance(dataOpt->templateName);
    if (inst == nullptr)
    {
        ORANGE_LOG_WARN("EnsureMaterialInstance: template '{}' 未注册（path={}）",
                        dataOpt->templateName, materialPath);
        return nullptr;
    }
    ::Orange::Editor::Material::ApplyDataToInstance(
        *dataOpt, *inst, host.assets.pAssets.get());
    auto* rawPtr = inst.get();
    host.assets.userMaterials[materialPath] = std::move(inst);
    return rawPtr;
}
