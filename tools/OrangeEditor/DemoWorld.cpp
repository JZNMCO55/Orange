// DemoWorld 实现 —— 见 DemoWorld.h 的注释。

#include "DemoWorld.h"

#include "EditorHierarchy.h"
#include "MaterialFileIO.h"
#include "demo_game/HealthComponent.h"

#include <orange/engine/animation/AnimatorComponent.h>
#include <orange/engine/animation/AnimatorRegistry.h>
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
#include <orange/engine/core/Serialization.h>
#include <orange/engine/physics/ColliderComponent.h>
#include <orange/engine/physics/ColliderDesc.h>
#include <orange/engine/physics/RigidBodyComponent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/ParticleEmitterComponent.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/NameComponent.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <unordered_map>
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
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 1);
        indices.push_back(base + 0); indices.push_back(base + 3); indices.push_back(base + 2);
    };

    // 6 面 + 显式朝外 face normal。winding 与既有 sample 的 plane 同顺
    // （shadow caster / 主 pass 的 CullMode 当前是 None，winding 选向不
    // 影响可见性；normal 由本函数显式写，与 winding 解耦）。
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
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<ShaderAsset> 失败 "
                     "(code=%u)\n",
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
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<MeshAsset> 失败 "
                     "(code=%u)\n",
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
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<TextureAsset> 失败 "
                     "(code=%u)\n",
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
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<SkeletonAsset> 失败 "
                     "(code=%u)\n",
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
        std::fprintf(stderr,
                     "[OrangeEditor] AssetRegistry::RegisterLoader<SoundAsset> 失败 "
                     "(code=%u)\n",
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
                    std::fprintf(stderr,
                                 "[OrangeEditor] MeshLoader::Save '%s' 失败 (code=%u)\n",
                                 path.c_str(),
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
            std::fprintf(stderr,
                         "[OrangeEditor] Load<MeshAsset> '%s' 失败 (code=%u)\n",
                         path.c_str(),
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
                std::fprintf(stderr,
                             "[OrangeEditor] lazy bake beep.wav 写盘失败 '%s'\n",
                             beepPath.string().c_str());
            }
        }
    }

    host.assets.pMaterials = std::make_unique<MaterialSystem>(*host.assets.pAssets);
    if (auto rb = host.assets.pMaterials->RegisterBuiltins(); rb.IsErr())
    {
        // 通常意味着 shaders/orange_engine/*.spv 不在 .exe 同目录——in-tree
        // build 由 CMake 把 SPV 拷到 build/bin/$<CONFIG>/shaders/orange_engine/，
        // standalone install 还没有官方流程时这里会报，但不阻止编辑器启动。
        std::fprintf(stderr,
                     "[OrangeEditor] MaterialSystem::RegisterBuiltins 失败 "
                     "(code=%u) —— Scene 视口稍后可能不显示几何\n",
                     static_cast<unsigned>(rb.Error()));
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
            std::fprintf(stderr,
                         "[OrangeEditor] .material '%s' 读取失败，回退到 '%.*s'\n",
                         path.c_str(),
                         static_cast<int>(fallbackTemplate.size()),
                         fallbackTemplate.data());
            return host.assets.pMaterials->CreateInstance(fallbackTemplate);
        }

        auto inst = host.assets.pMaterials->CreateInstance(dataOpt->templateName);
        if (inst == nullptr)
        {
            std::fprintf(stderr,
                         "[OrangeEditor] .material '%s' template '%s' 未注册，回退\n",
                         path.c_str(),
                         dataOpt->templateName.c_str());
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
            std::fprintf(stderr,
                         "[OrangeEditor] AnimatorRegistry::RegisterBackend(procedural) "
                         "失败 (code=%u)\n",
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
    return m;
}

// demo 世界层级：
//   Root
//   ├── Camera           （2.5D 侧视角）
//   ├── Sun              （平行光 + 软阴影）
//   └── Geometry
//       ├── Ground       （大平面，textured，静态刚体）
//       ├── Backdrop     （竖立背景平面，rim_light）
//       ├── Platform L   （左台，toon，静态刚体）
//       ├── Platform R   （右台，toon，静态刚体）
//       ├── Tower        （高塔 scale×2Y，toon，静态刚体）
//       ├── Glow Box     （溶解方块，dissolve，自动动画）
//       ├── Emissive Pillar（自发光细柱，emissive）
//       ├── Dynamic Box  （动态刚体，Play Mode 物理演示，y=4 悬空下落）
//       ├── Fire Emitter （粒子：火焰，暖橙 HDR → bloom）
//       └── Sparkle Emitter（粒子：萤火，蓝白 HDR → bloom）
void SeedDemoWorld(EditorHost& host)
{
    using ::Orange::Engine::Entity;
    using ::Orange::Engine::Scene::NameComponent;
    using ::Orange::Engine::Scene::TransformComponent;
    using ::Orange::Engine::Render::Camera;
    using ::Orange::Engine::Render::DirectionalLight;
    using ::Orange::Engine::Render::RenderableComponent;
    using ::Orange::Engine::Render::ParticleEmitterComponent;
    using ::Orange::Engine::Render::ParticleEmitterDesc;
    using ::Orange::Engine::Physics::BodyType;
    using ::Orange::Engine::Physics::ColliderComponent;
    using ::Orange::Engine::Physics::BoxDesc;
    using ::Orange::Engine::Physics::CircleDesc;
    using ::Orange::Engine::Physics::EdgeChainDesc;
    using ::Orange::Engine::Physics::PolygonDesc;
    using ::Orange::Engine::Physics::RigidBodyComponent;
    using ::Orange::Engine::Animation::AnimatorComponent;

    auto& world = *host.scene.pWorld;
    auto make = [&](const char* name) -> Entity {
        Entity e = world.CreateEntity();
        world.AddComponent<NameComponent>(e, NameComponent{name});
        world.AddComponent<TransformComponent>(e, TransformComponent{});
        return e;
    };

    // ---- 层级容器 -------------------------------------------------------
    Entity root     = make("Root");
    Entity camera   = make("Camera");
    Entity sun      = make("Sun");
    Entity geometry = make("Geometry");

    // ---- Geometry 下的子节点 --------------------------------------------
    Entity ground          = make("Ground");
    Entity backdrop        = make("Backdrop");
    Entity platformLeft    = make("Platform L");
    Entity platformRight   = make("Platform R");
    Entity tower           = make("Tower");
    Entity glowBox         = make("Glow Box");
    Entity emissivePillar  = make("Emissive Pillar");
    Entity dynamicBox      = make("Dynamic Box");
    Entity fireEmitter     = make("Fire Emitter");
    Entity sparkleEmitter  = make("Sparkle Emitter");

    // v0.3 c1：演示 / 验收前置实体。
    //   * slimeDoll：Animator-only 实体，无 Renderable——专门展示 Animator schema
    //     段 + ReadOnly Backend 字段，Inspector 可直接观察。
    //   * staticCircle / staticPolygon / staticEdgeChain：Box 之外三种 shape
    //     的 Collider 验收前置实体；位置放右侧远端，无 Renderable，物理上
    //     仅作为 schema 段渲染样本——切换到这三个实体看 Inspector 即可验
    //     c8 三个 visibleIf 互斥段。
    Entity slimeDoll       = make("Slime Doll");
    Entity testFighter     = make("Test Fighter");
    Entity staticCircle    = make("Static Circle (demo)");
    Entity staticPolygon   = make("Static Polygon (demo)");
    Entity staticEdgeChain = make("Static EdgeChain (demo)");

    // ---- Camera：2.5D 侧视角 + 轻微俯角 --------------------------------
    // EditorCamera 会每帧覆写 view 矩阵；此处的 view 仅在非编辑器消费
    // （如 Play Mode 截图、非 editor host）时生效。
    {
        Camera cam = Camera::Perspective(glm::radians(45.0f),
                                         /*aspect=*/1.0f,
                                         /*zNear=*/0.1f,
                                         /*zFar=*/100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 2.0f, 8.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent<Camera>(camera, cam);
    }

    // ---- Sun：暖色平行光 + 软阴影开启 -----------------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(sun);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(5.0f, 8.0f, 5.0f);
            // 方向由 rotation 派生；identity 表示光向下，这里把传统
            // (0.4,-1,0.3) 朝向编码进 rotation 里。
            tc->rotation = ::Orange::Engine::Render::
                MakeDirectionalLightRotationFromDir(
                    glm::vec3(0.4f, -1.0f, 0.3f));
        }

        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.93f, 0.78f);  // 暖黄阳光
        dl.intensity   = 1.3f;
        dl.castsShadow = true;  // 开启软阴影
        world.AddComponent<DirectionalLight>(sun, dl);
    }

    // ---- Ground（大平面，textured，静态刚体）----------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(ground);
        if (tc != nullptr) { tc->position.y = -0.5f; }

        RenderableComponent rc{};
        rc.mesh             = host.assets.planeMeshHandle;
        rc.materialInstance = host.assets.pFloorMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(ground, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(ground, rb);

        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{5.0f, 0.5f}};
        cc.density  = 0.0f;
        cc.friction = 0.6f;
        world.AddComponent<ColliderComponent>(ground, cc);
    }

    // ---- Backdrop（竖立背景平面，rim_light）-----------------------------
    // 绕 +X 轴旋转 90°：平面法线 +Y → +Z，朝向相机，形成 5×5 背景幕布。
    // 位于 z=-3.5，Y 方向从地面 (-0.5) 延伸到上方 (4.5)。
    {
        auto* tc = world.GetComponent<TransformComponent>(backdrop);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(0.0f, 2.0f, -3.5f);
            tc->rotation = glm::angleAxis(glm::radians(90.0f),
                                          glm::vec3(1.0f, 0.0f, 0.0f));
        }
        RenderableComponent rc{};
        rc.mesh             = host.assets.planeMeshHandle;
        rc.materialInstance = host.assets.pRimLightMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(backdrop, rc);
    }

    // ---- Platform L（左侧平台，toon，静态刚体）--------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(platformLeft);
        if (tc != nullptr) { tc->position = glm::vec3(-2.0f, 0.0f, 0.0f); }

        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pToonMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = true;
        world.AddComponent<RenderableComponent>(platformLeft, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(platformLeft, rb);

        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{0.5f, 0.5f}};
        cc.density  = 0.0f;
        cc.friction = 0.4f;
        world.AddComponent<ColliderComponent>(platformLeft, cc);
    }

    // ---- Platform R（右侧平台，toon，静态刚体）--------------------------
    {
        auto* tc = world.GetComponent<TransformComponent>(platformRight);
        if (tc != nullptr) { tc->position = glm::vec3(2.0f, 0.0f, 0.0f); }

        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pToonMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = true;
        world.AddComponent<RenderableComponent>(platformRight, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(platformRight, rb);

        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{0.5f, 0.5f}};
        cc.density  = 0.0f;
        cc.friction = 0.4f;
        world.AddComponent<ColliderComponent>(platformRight, cc);
    }

    // ---- Tower（高塔，scale Y×2，toon，静态刚体）-----------------------
    // scale(1,2,1) → 实际半高 1.0；center y=0.5 → 底部 y=-0.5（齐地面）。
    {
        auto* tc = world.GetComponent<TransformComponent>(tower);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(0.0f, 0.5f, -1.0f);
            tc->scale    = glm::vec3(1.0f, 2.0f, 1.0f);
        }
        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pToonMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = true;
        world.AddComponent<RenderableComponent>(tower, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(tower, rb);

        // 物理碰撞盒需与 scale 后的实际半尺寸匹配
        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{0.5f, 1.0f}};
        cc.density  = 0.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(tower, cc);
    }

    // ---- Glow Box（dissolve 溶解方块，自动 pingpong 动画）---------------
    // dissolve shader 从 light UBO 的 uFrameInfo.x 自驱 dissolve_t，
    // 不需要 game 代码手动驱动——Edit 模式下即可看到溶解 + 发光边沿效果。
    {
        auto* tc = world.GetComponent<TransformComponent>(glowBox);
        if (tc != nullptr) { tc->position = glm::vec3(0.8f, 0.0f, 0.5f); }

        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pDissolveMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(glowBox, rc);
    }

    // ---- Emissive Pillar（自发光细柱，emissive，HDR→bloom）--------------
    // scale(0.5,2.5,0.5) → 细高柱；center y=0.75 → 底部 y=-0.5（齐地面）。
    {
        auto* tc = world.GetComponent<TransformComponent>(emissivePillar);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(-1.2f, 0.75f, 1.0f);
            tc->scale    = glm::vec3(0.5f, 2.5f, 0.5f);
        }
        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pLightObjectMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = false;
        world.AddComponent<RenderableComponent>(emissivePillar, rc);
    }

    // ---- Dynamic Box（演示 Play Mode 物理：悬空下落，落到地面上）---------
    // 位于 y=4，正上方无遮挡；Play 后受重力下落，碰 Ground 静止。
    // 使用 toon 材质，castsShadow=true，视觉上与静态台面区分。
    {
        auto* tc = world.GetComponent<TransformComponent>(dynamicBox);
        if (tc != nullptr) { tc->position = glm::vec3(0.0f, 4.0f, 0.0f); }

        RenderableComponent rc{};
        rc.mesh             = host.assets.cubeMeshHandle;
        rc.materialInstance = host.assets.pToonMaterial.get();
        rc.visible          = true;
        rc.castsShadow      = true;
        world.AddComponent<RenderableComponent>(dynamicBox, rc);

        RigidBodyComponent rb{};
        rb.type          = BodyType::Dynamic;
        rb.fixedRotation = false;
        rb.gravityScale  = 1.0f;
        rb.linearDamping = 0.05f;
        world.AddComponent<RigidBodyComponent>(dynamicBox, rb);

        ColliderComponent cc{};
        cc.shape    = BoxDesc{glm::vec2{0.5f, 0.5f}};
        cc.density  = 1.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(dynamicBox, cc);
    }

    // ---- Fire Emitter（火焰粒子：暖橙 HDR，bloom 自动触发光晕）----------
    {
        auto* tc = world.GetComponent<TransformComponent>(fireEmitter);
        if (tc != nullptr) { tc->position = glm::vec3(1.5f, -0.3f, 0.5f); }

        ParticleEmitterComponent pec{};
        pec.emitting               = true;
        pec.desc.emissionRate      = 30.0f;
        pec.desc.lifetimeMin       = 0.6f;
        pec.desc.lifetimeMax       = 1.2f;
        pec.desc.spawnOffsetMin    = glm::vec2{-0.12f, 0.0f};
        pec.desc.spawnOffsetMax    = glm::vec2{ 0.12f, 0.0f};
        pec.desc.initialVelocityMin = glm::vec2{-0.25f, 1.2f};
        pec.desc.initialVelocityMax = glm::vec2{ 0.25f, 2.2f};
        pec.desc.gravity           = glm::vec2{0.0f, -0.4f};
        pec.desc.colorStart        = glm::vec4{1.6f, 0.75f, 0.1f, 2.2f};  // HDR 橙黄
        pec.desc.colorEnd          = glm::vec4{0.7f, 0.15f, 0.0f, 0.0f};  // 红色熄灭
        pec.desc.sizeStart         = 0.04f;
        pec.desc.sizeEnd           = 0.09f;
        pec.desc.maxParticles      = 128u;
        world.AddComponent<ParticleEmitterComponent>(fireEmitter, pec);
    }

    // ---- Sparkle Emitter（萤火粒子：蓝白 HDR，飘浮上升）----------------
    {
        auto* tc = world.GetComponent<TransformComponent>(sparkleEmitter);
        if (tc != nullptr) { tc->position = glm::vec3(-1.5f, 1.2f, 0.8f); }

        ParticleEmitterComponent pec{};
        pec.emitting               = true;
        pec.desc.emissionRate      = 10.0f;
        pec.desc.lifetimeMin       = 2.0f;
        pec.desc.lifetimeMax       = 3.5f;
        pec.desc.spawnOffsetMin    = glm::vec2{-0.5f, -0.3f};
        pec.desc.spawnOffsetMax    = glm::vec2{ 0.5f,  0.3f};
        pec.desc.initialVelocityMin = glm::vec2{-0.15f, 0.05f};
        pec.desc.initialVelocityMax = glm::vec2{ 0.15f, 0.35f};
        pec.desc.gravity           = glm::vec2{0.0f, 0.08f};   // 轻微上浮
        pec.desc.colorStart        = glm::vec4{0.7f, 0.9f, 3.0f, 3.5f};  // HDR 蓝白（强 bloom）
        pec.desc.colorEnd          = glm::vec4{0.3f, 0.5f, 1.0f, 0.0f};  // 蓝色消散
        pec.desc.sizeStart         = 0.025f;
        pec.desc.sizeEnd           = 0.055f;
        pec.desc.maxParticles      = 64u;
        world.AddComponent<ParticleEmitterComponent>(sparkleEmitter, pec);
    }

    // ---- Slime Doll（Animator-only，无 Renderable）-----------------------
    // 位置贴在 Glow Box 上方，方便在 Inspector 选实体时 viewport 大致定位；
    // 没有 Renderable 是刻意决定（参 entity 创建段注释）。
    {
        auto* tc = world.GetComponent<TransformComponent>(slimeDoll);
        if (tc != nullptr) { tc->position = glm::vec3(0.8f, 1.2f, 0.5f); }

        AnimatorComponent ac{};
        if (host.assets.pAnimators != nullptr)
        {
            ac.animator = host.assets.pAnimators->Create("procedural");
        }
        world.AddComponent<AnimatorComponent>(slimeDoll, std::move(ac));
    }

    // ---- Test Fighter（HealthComponent 验收前置实体）----------------------
    // 演示 extraSerializers 扩展点的 Save/Load round-trip：挂非默认 hp 值，
    // Save → 重启 → Load 后 Inspector 应显示相同数值。无 Renderable / 物理体，
    // 仅作 Inspector 样本。
    {
        auto* tc = world.GetComponent<TransformComponent>(testFighter);
        if (tc != nullptr) { tc->position = glm::vec3(-8.0f, 0.5f, 0.0f); }

        DemoGame::HealthComponent health{};
        health.hp    = 75;
        health.maxHp = 100;
        world.AddComponent<DemoGame::HealthComponent>(testFighter, health);
    }

    // ---- Static Circle (demo)（Circle Collider 验收前置实体）-------------
    // 放右侧远端 (x=8) 避免与主场景视觉冲突；无 Renderable，仅供 Inspector
    // 段渲染样本。RigidBody Static + 与 Collider 配对，Load 时 PhysicsWorld
    // 能完整 AddBody（不出现 "single component" 警告）。
    {
        auto* tc = world.GetComponent<TransformComponent>(staticCircle);
        if (tc != nullptr) { tc->position = glm::vec3(8.0f, -0.5f, 0.0f); }

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(staticCircle, rb);

        ColliderComponent cc{};
        cc.shape    = CircleDesc{/*radius=*/0.5f, /*center=*/glm::vec2{0.0f, 0.0f}};
        cc.density  = 0.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(staticCircle, cc);
    }

    // ---- Static Polygon (demo)（Polygon Collider 验收前置实体）-----------
    // 4 顶点凸四边形（梯形），验证 Polygon shape schema 段在 Inspector 渲染。
    {
        auto* tc = world.GetComponent<TransformComponent>(staticPolygon);
        if (tc != nullptr) { tc->position = glm::vec3(8.5f, -0.5f, 0.0f); }

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(staticPolygon, rb);

        PolygonDesc pd{};
        pd.count = 4u;
        pd.vertices[0] = glm::vec2{-0.4f, -0.3f};
        pd.vertices[1] = glm::vec2{ 0.4f, -0.3f};
        pd.vertices[2] = glm::vec2{ 0.3f,  0.3f};
        pd.vertices[3] = glm::vec2{-0.3f,  0.3f};

        ColliderComponent cc{};
        cc.shape    = pd;
        cc.density  = 0.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(staticPolygon, cc);
    }

    // ---- Static EdgeChain (demo)（EdgeChain Collider 验收前置实体）-------
    // 4 顶点开放折线（不闭环），验证 EdgeChain shape schema 段在 Inspector
    // 渲染 + isLoop 字段。
    {
        auto* tc = world.GetComponent<TransformComponent>(staticEdgeChain);
        if (tc != nullptr) { tc->position = glm::vec3(9.0f, -0.5f, 0.0f); }

        RigidBodyComponent rb{};
        rb.type          = BodyType::Static;
        rb.fixedRotation = true;
        rb.gravityScale  = 0.0f;
        world.AddComponent<RigidBodyComponent>(staticEdgeChain, rb);

        EdgeChainDesc ed{};
        ed.count = 4u;
        ed.vertices[0] = glm::vec2{-0.5f,  0.0f};
        ed.vertices[1] = glm::vec2{-0.2f,  0.3f};
        ed.vertices[2] = glm::vec2{ 0.2f,  0.3f};
        ed.vertices[3] = glm::vec2{ 0.5f,  0.0f};
        ed.isLoop = false;

        ColliderComponent cc{};
        cc.shape    = ed;
        cc.density  = 0.0f;
        cc.friction = 0.5f;
        world.AddComponent<ColliderComponent>(staticEdgeChain, cc);
    }

    // ---- 构建父子层级 ---------------------------------------------------
    EditorHierarchy::LinkAsLastChild(world, root,     camera);
    EditorHierarchy::LinkAsLastChild(world, root,     sun);
    EditorHierarchy::LinkAsLastChild(world, root,     geometry);
    EditorHierarchy::LinkAsLastChild(world, geometry, ground);
    EditorHierarchy::LinkAsLastChild(world, geometry, backdrop);
    EditorHierarchy::LinkAsLastChild(world, geometry, platformLeft);
    EditorHierarchy::LinkAsLastChild(world, geometry, platformRight);
    EditorHierarchy::LinkAsLastChild(world, geometry, tower);
    EditorHierarchy::LinkAsLastChild(world, geometry, glowBox);
    EditorHierarchy::LinkAsLastChild(world, geometry, emissivePillar);
    EditorHierarchy::LinkAsLastChild(world, geometry, dynamicBox);
    EditorHierarchy::LinkAsLastChild(world, geometry, fireEmitter);
    EditorHierarchy::LinkAsLastChild(world, geometry, sparkleEmitter);
    EditorHierarchy::LinkAsLastChild(world, geometry, slimeDoll);
    EditorHierarchy::LinkAsLastChild(world, geometry, testFighter);
    EditorHierarchy::LinkAsLastChild(world, geometry, staticCircle);
    EditorHierarchy::LinkAsLastChild(world, geometry, staticPolygon);
    EditorHierarchy::LinkAsLastChild(world, geometry, staticEdgeChain);
}

// PBR showcase scene 种植：与 sample 13_pbr_direct / 14_pbr_ibl 同款 3×3 球
// 阵布局，但放成两组并排——左侧 warm 暖橙（对应 13_pbr_direct），右侧
// white furnace（对应 14_pbr_ibl）。Camera 正前方 + Sun 暖光 + Environment
// 占位 entity（cubemap 留空，用户拖 HDR 进去激活 IBL）。
//
// 球阵布局：每组 3×3，spacing 1.4，组间留 ~1.4 间距让两组明显分开。
// 行（Y 自下而上）= metallic [0.0, 0.5, 1.0]；列（X 自左向右）= roughness
// [0.1, 0.5, 0.9]。两组共 18 球，与 InitializeEditorAssets 内 lazy bake 的
// 18 个 MaterialInstance 一一对应（路径键 warm_m{0..2}r{0..2} / white_m{0..2}r{0..2}）。
void SeedPbrShowcaseWorld(Orange::Engine::World& targetWorld,
                          const EditorAssetContext& assets)
{
    using ::Orange::Engine::Entity;
    using ::Orange::Engine::Scene::NameComponent;
    using ::Orange::Engine::Scene::TransformComponent;
    using ::Orange::Engine::Render::Camera;
    using ::Orange::Engine::Render::DirectionalLight;
    using ::Orange::Engine::Render::EnvironmentComponent;
    using ::Orange::Engine::Render::RenderableComponent;

    auto& world = targetWorld;
    auto make = [&](const char* name) -> Entity {
        Entity e = world.CreateEntity();
        world.AddComponent<NameComponent>(e, NameComponent{name});
        world.AddComponent<TransformComponent>(e, TransformComponent{});
        return e;
    };

    Entity root        = make("Root");
    Entity camera      = make("Camera");
    Entity sun         = make("Sun");
    Entity environment = make("Environment");
    Entity warmGroup   = make("Warm Spheres (13_pbr_direct)");
    Entity whiteGroup  = make("White Spheres (14_pbr_ibl furnace)");

    // Camera：正前方稍高俯视，与 sample 14_pbr_ibl 视角同款 + 拉远适应两组
    // 并排球阵的宽度。
    {
        Camera cam = Camera::Perspective(glm::radians(40.0f), 1.0f, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 0.3f, 8.0f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent<Camera>(camera, cam);
    }

    // Sun：暖光平行光从右上前斜下打，与 demo 同款方向。
    {
        auto* tc = world.GetComponent<TransformComponent>(sun);
        if (tc != nullptr)
        {
            tc->position = glm::vec3(5.0f, 8.0f, 5.0f);
            tc->rotation = ::Orange::Engine::Render::
                MakeDirectionalLightRotationFromDir(
                    glm::vec3(0.4f, -1.0f, 0.3f));
        }
        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 0.95f, 0.85f);  // 偏白暖色，furnace 球阵不偏色
        dl.intensity   = 1.2f;
        dl.castsShadow = false;  // showcase 无地面，不需要阴影
        world.AddComponent<DirectionalLight>(sun, dl);
    }

    // Environment：默认空 cubemap。用户拖 HDR 进去后 Pipeline auto re-bake，
    // 14_pbr_ibl 那组 furnace 球会显示真实 IBL specular 反射。
    {
        EnvironmentComponent env{};
        world.AddComponent<EnvironmentComponent>(environment, env);
    }

    // 18 个球 entity：左 warm 9 + 右 white 9
    constexpr float kSphereSpacing = 1.4f;
    // 两组中心 X 距离 = 单组宽 (2 * spacing) + 组间 spacing = 4.2 → 左右对称
    // 让中心 0 ≈ 两组中间。
    constexpr float kGroupOffset = 2.5f;

    auto spawnSphereGrid =
        [&](Entity parent, const char* variantKey, float groupCenterX,
            std::size_t materialBaseIndex)
        {
            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t col = 0; col < 3; ++col)
                {
                    char name[64];
                    std::snprintf(name, sizeof(name), "%s m%zur%zu",
                                  variantKey, row, col);
                    Entity sphere = make(name);

                    auto* tc = world.GetComponent<TransformComponent>(sphere);
                    if (tc != nullptr)
                    {
                        tc->position = glm::vec3(
                            groupCenterX + (static_cast<float>(col) - 1.0f) * kSphereSpacing,
                            (static_cast<float>(row) - 1.0f) * kSphereSpacing,
                            0.0f);
                    }

                    RenderableComponent rc{};
                    rc.mesh    = assets.sphereMeshHandle;
                    rc.visible = true;
                    rc.castsShadow = false;
                    const std::size_t matIdx = materialBaseIndex + row * 3 + col;
                    if (matIdx < assets.pbrShowcaseMaterials.size())
                    {
                        rc.materialInstance = assets.pbrShowcaseMaterials[matIdx].get();
                    }
                    world.AddComponent<RenderableComponent>(sphere, rc);

                    EditorHierarchy::LinkAsLastChild(world, parent, sphere);
                }
            }
        };

    spawnSphereGrid(warmGroup,  "warm",  -kGroupOffset, /*matBase=*/0);
    spawnSphereGrid(whiteGroup, "white",  kGroupOffset, /*matBase=*/9);

    EditorHierarchy::LinkAsLastChild(world, root, camera);
    EditorHierarchy::LinkAsLastChild(world, root, sun);
    EditorHierarchy::LinkAsLastChild(world, root, environment);
    EditorHierarchy::LinkAsLastChild(world, root, warmGroup);
    EditorHierarchy::LinkAsLastChild(world, root, whiteGroup);
}
