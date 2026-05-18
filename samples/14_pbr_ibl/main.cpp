// samples/14_pbr_ibl —— PBR + IBL 完整三件套 demo
//
// 与 13_pbr_direct 的差异点：
//   * 新增 EnvironmentComponent，cubemap 指向 PolyHaven CC0 HDR equirect
//     `assets/environments/default_outdoor.hdr`（用户按 README 步骤下载）
//   * 启动期调 `Pipeline::BakeIblFromWorld` 跑 IblBaker 三件套（cube/
//     irradiance/prefiltered/brdfLut），PBR shader IBL 段从 dummy 切到真实
//     烘焙产物
//   * DirectionalLight 强度下调 + cast shadow off：让 IBL 反射 / 环境填充
//     成为视觉主导，避免 direct 光把高光打飞
//
// **命令行参数**：
//   `14_pbr_ibl --furnace` —— white furnace test 模式。Skip 加载真实 HDR
//   asset，改用程序化 1×1 全白 RGBA32Float TextureAsset 作 cubemap。Cook-
//   Torrance + Lambert 在能量守恒前提下，全白环境照射任意材质球应输出近似
//   全白（包括纯金属 / 纯粗糙塑料 / 五颜六色 baseColor）。偏暗 = BRDF 能量
//   损失；偏亮 = 重复计入。视觉对照 wiki environment-lighting.md §furnace test。
//
//   `14_pbr_ibl`（无参数）—— 默认走真实 HDR 路径
//
// **资产前置**：默认模式需要 `assets/environments/default_outdoor.hdr`
// 实际存在；用户按 `assets/environments/README.md` 步骤从 PolyHaven 下载
// 1K CC0 HDR 重命名放入。文件缺失时 Pipeline::BakeIblFromWorld 走 graceful
// 降级（log warn + dummy IBL fallback），sample 仍能渲染（视觉等价 13_pbr_direct）。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/asset/TextureAsset.h>
#include <orange/engine/asset/TextureLoader.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/BuiltinPostProcessChain.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/EnvironmentComponent.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/PostProcessChain.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/render/ShadowConfig.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

using namespace Orange::Engine;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Asset::TextureAsset;
using Orange::Engine::Asset::TextureFormat;
using Orange::Engine::Asset::TextureLoader;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Render::BuiltinPostProcessChain::CreateDefault;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::EnvironmentComponent;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::PostProcessChain;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Render::ShadowConfig;
using Orange::Engine::Scene::TransformComponent;

namespace
{

// 球体 mesh —— 与 13_pbr_direct 同款 lat/lon UV-sphere（共享路径下
// ComputeSmoothNormalsFromTriangles 给出 normalize(pos) 近似平滑法线）。
std::unique_ptr<MeshAsset> MakeSphereMesh(float radius, std::uint32_t lon, std::uint32_t lat)
{
    std::vector<VertexPosition3> positions;
    std::vector<VertexUV2>       uvs;
    std::vector<std::uint32_t>   indices;
    for (std::uint32_t i = 0; i <= lat; ++i)
    {
        const float v     = static_cast<float>(i) / static_cast<float>(lat);
        const float theta = v * glm::pi<float>();
        const float sinT  = std::sin(theta);
        const float cosT  = std::cos(theta);
        for (std::uint32_t j = 0; j <= lon; ++j)
        {
            const float u    = static_cast<float>(j) / static_cast<float>(lon);
            const float phi  = u * glm::two_pi<float>();
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
    pMesh->ComputeSmoothNormalsFromTriangles();
    return pMesh;
}

// 程序化生成 1×1 全白 RGBA32Float TextureAsset —— furnace test 用。
// 不走 stb / 文件路径，避免依赖磁盘上的白炉资产。
std::unique_ptr<TextureAsset> MakeFurnaceWhiteEquirect()
{
    // 单像素 RGBA = (1, 1, 1, 1) 浮点，16 bytes
    std::vector<std::uint8_t> bytes(16);
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::memcpy(bytes.data(), white, sizeof(white));
    return std::make_unique<TextureAsset>(1u, 1u, TextureFormat::R32G32B32A32_Float,
                                          std::move(bytes));
}

class RenderLayer : public Layer
{
public:
    RenderLayer(Pipeline& pipeline, World& world, AppHost* host = nullptr,
                std::filesystem::path capturePath = {}, int exitAfterFrames = -1)
        : Layer("RenderLayer"), mPipeline(pipeline), mWorld(world),
          mpHost(host), mCapturePath(std::move(capturePath)),
          mExitAfterFrames(exitAfterFrames)
    {
    }

    void OnUpdate(const FrameContext& /*frame*/) override
    {
        // capture 模式：第 mExitAfterFrames - 1 帧请求 capture（capture 走
        // Render 内部 Stage A 末尾 CopyTextureToBuffer + 本帧 WaitIdle 后
        // FinalizeCapture 把 PNG 写盘），下一帧（== mExitAfterFrames）RequestExit。
        if (!mCapturePath.empty() && mFrameIndex == mExitAfterFrames - 1)
        {
            mPipeline.RequestCapture(mCapturePath);
        }
        mPipeline.Render(mWorld);
        if (mpHost && mExitAfterFrames >= 0 && mFrameIndex >= mExitAfterFrames)
        {
            mpHost->RequestExit();
        }
        ++mFrameIndex;
    }

    bool OnEvent(const Platform::WindowEvent& event) override
    {
        if (auto* resize = std::get_if<Platform::WindowResizeEvent>(&event))
        {
            mPipeline.OnResize(resize->width, resize->height);
        }
        return false;
    }

private:
    Pipeline&             mPipeline;
    World&                mWorld;
    AppHost*              mpHost{nullptr};
    std::filesystem::path mCapturePath{};
    int                   mExitAfterFrames{-1};
    int                   mFrameIndex{0};
};

constexpr std::array<float, 3> kMetallicSteps  = {0.0f, 0.5f, 1.0f};
constexpr std::array<float, 3> kRoughnessSteps = {0.1f, 0.5f, 0.9f};
// 暖橙：metallic=1 行金属高光自带 baseColor 着色，肉眼可识别"非白"高光；
// IBL 在金属球上呈现 baseColor-tinted 环境反射。
constexpr glm::vec4            kBaseColor{1.0f, 0.78f, 0.34f, 1.0f};
constexpr float                kSphereSpacing = 1.4f;
constexpr float                kSphereRadius  = 0.5f;

constexpr std::string_view kDefaultEnvPath = "assets/environments/default_outdoor.hdr";
constexpr std::string_view kFurnaceEnvKey  = "builtin/furnace_white_1x1";

}  // namespace

int main(int argc, char** argv)
{
    // 命令行参数：
    //   --furnace                  white furnace test 模式
    //   --capture <path>           第 (exit-after - 1) 帧 RequestCapture 写 PNG 落盘
    //   --exit-after <N>           第 N 帧后 RequestExit（无人值守视觉验证）
    //   其它参数 silent ignore（与 sample 1-13 一致）
    bool                  furnaceMode      = false;
    std::filesystem::path capturePath{};
    int                   exitAfterFrames  = -1;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg{argv[i]};
        if (arg == "--furnace")
        {
            furnaceMode = true;
        }
        else if (arg == "--capture" && i + 1 < argc)
        {
            capturePath = std::filesystem::path{argv[++i]};
        }
        else if (arg == "--exit-after" && i + 1 < argc)
        {
            exitAfterFrames = std::atoi(argv[++i]);
        }
    }

    AppConfig cfg{};
    cfg.window.title  = furnaceMode
                      ? "OrangeEngine - 14 pbr_ibl (--furnace)"
                      : "OrangeEngine - 14 pbr_ibl";
    cfg.window.width  = 1280;
    cfg.window.height = 720;

    auto hostResult = AppHost::Create(cfg);
    if (hostResult.IsErr())
    {
        std::fprintf(stderr, "AppHost::Create failed (code=%u)\n",
                     static_cast<unsigned>(hostResult.Error()));
        return 1;
    }
    auto host = std::move(hostResult).Value();

    AssetRegistry assets;
    if (auto reg = assets.RegisterLoader<ShaderAsset>(std::make_unique<ShaderLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr, "RegisterLoader<ShaderAsset> failed\n");
        return 1;
    }
    if (auto reg = assets.RegisterLoader<TextureAsset>(std::make_unique<TextureLoader>());
        reg.IsErr())
    {
        std::fprintf(stderr, "RegisterLoader<TextureAsset> failed\n");
        return 1;
    }

    auto sphereRes = assets.Insert<MeshAsset>("builtin/sphere",
                                              MakeSphereMesh(kSphereRadius, 32, 16));
    if (sphereRes.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed\n");
        return 1;
    }
    AssetHandle<MeshAsset> sphereHandle = sphereRes.Value();

    // IBL cubemap handle：furnace 模式 = 程序化 1×1 白；其它 = 从磁盘加载 .hdr
    AssetHandle<TextureAsset> envHandle;
    if (furnaceMode)
    {
        auto whiteRes = assets.Insert<TextureAsset>(kFurnaceEnvKey, MakeFurnaceWhiteEquirect());
        if (whiteRes.IsErr())
        {
            std::fprintf(stderr, "Insert<TextureAsset> (furnace white) failed\n");
            return 1;
        }
        envHandle = whiteRes.Value();
        std::printf("[14_pbr_ibl] furnace mode：使用 1×1 全白 RGBA32F equirect 作 IBL 输入。\n"
                    "             正确实现下任意 PBR 材质球应输出近似全白（能量守恒验证）。\n");
    }
    else
    {
        auto loadRes = assets.Load<TextureAsset>(kDefaultEnvPath);
        if (loadRes.IsErr())
        {
            // 资产缺失（用户未按 assets/environments/README.md 下载 PolyHaven HDR）
            // 不阻断 sample：BakeIblFromWorld 会 fallback 到 dummy IBL，视觉等价
            // 13_pbr_direct（金属球反射黑、无环境填充）。明显提示一下让用户知道。
            std::fprintf(stderr,
                "[14_pbr_ibl] WARNING: 加载 %.*s 失败（code=%u）。\n"
                "             请按 assets/environments/README.md 步骤从 PolyHaven\n"
                "             下载 1K CC0 HDRI 并重命名放入 assets/environments/\n"
                "             default_outdoor.hdr。本次启动将走 dummy IBL fallback，\n"
                "             视觉接近 samples/13_pbr_direct（金属球反射黑）。\n",
                static_cast<int>(kDefaultEnvPath.size()), kDefaultEnvPath.data(),
                static_cast<unsigned>(loadRes.Error()));
        }
        else
        {
            envHandle = loadRes.Value();
        }
    }

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed\n");
        return 1;
    }

    World world;

    // 9 个 PBR MaterialInstance：每球独立一份
    std::vector<std::unique_ptr<MaterialInstance>> instances;
    instances.reserve(kMetallicSteps.size() * kRoughnessSteps.size());

    const float xOffset = -kSphereSpacing
                        * static_cast<float>(kRoughnessSteps.size() - 1) * 0.5f;
    const float yOffset = -kSphereSpacing
                        * static_cast<float>(kMetallicSteps.size() - 1) * 0.5f;

    for (std::size_t row = 0; row < kMetallicSteps.size(); ++row)
    {
        for (std::size_t col = 0; col < kRoughnessSteps.size(); ++col)
        {
            auto inst = materials.CreateInstance("pbr");
            if (!inst)
            {
                std::fprintf(stderr, "CreateInstance(\"pbr\") failed\n");
                return 1;
            }
            // furnace test：把所有 baseColor 改成白，方便用纯灰输出验能量守恒；
            // metallic / roughness 仍走 3×3 阵列，保证全象限验证（金属 vs 漫反
            // 射 / 锐利高光 vs 漫反射 IBL 均覆盖）。
            inst->SetUniform("uBaseColor",
                             furnaceMode ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : kBaseColor);
            inst->SetUniform("uMRA",
                             glm::vec4(kMetallicSteps[row],
                                       kRoughnessSteps[col],
                                       1.0f,
                                       0.0f));
            instances.push_back(std::move(inst));

            Entity entity = world.CreateEntity();
            TransformComponent xf{};
            xf.position = {
                xOffset + static_cast<float>(col) * kSphereSpacing,
                yOffset + static_cast<float>(row) * kSphereSpacing,
                0.0f,
            };
            world.AddComponent(entity, xf);

            RenderableComponent r;
            r.mesh             = sphereHandle;
            r.materialInstance = instances.back().get();
            r.castsShadow      = false;
            world.AddComponent(entity, r);
        }
    }

    // EnvironmentComponent —— 挂全局 IBL 输入。intensity / tint 走 (1,1,1) /
    // 1.0 默认；furnace 模式也保持默认，1×1 全白 cube 经烘焙后 irradiance /
    // prefiltered 都接近常数 1（理论值；GGX importance sampling 数值上有微小
    // 偏差，正是 furnace test 想看到的"是否真守恒"）。
    Entity envEntity = world.CreateEntity();
    {
        EnvironmentComponent env{};
        env.cubemap   = envHandle;     // furnace 模式 → 1×1 白；否则 → .hdr 加载结果
        env.tint      = glm::vec3{1.0f, 1.0f, 1.0f};
        env.intensity = 1.0f;
        world.AddComponent(envEntity, env);
    }

    // DirectionalLight：furnace 模式关掉 direct 光，纯靠 IBL 驱动验能量守恒；
    // 默认模式给一束适度强度让 direct + IBL 两个通道都有贡献。
    Entity lightEntity = world.CreateEntity();
    {
        TransformComponent lightXf{};
        lightXf.rotation = Orange::Engine::Render::MakeDirectionalLightRotationFromDir(
            glm::vec3(0.5f, -1.0f, -0.6f));
        world.AddComponent(lightEntity, lightXf);

        DirectionalLight dl{};
        dl.color       = glm::vec3(1.0f, 1.0f, 1.0f);
        dl.intensity   = furnaceMode ? 0.0f : 1.5f;
        dl.castsShadow = false;
        world.AddComponent(lightEntity, dl);
    }

    // Camera：与 13_pbr_direct 同款视角（球阵居中，略上斜俯视）。
    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width)
                           / static_cast<float>(cfg.window.height);
        Camera cam = Camera::Perspective(glm::radians(40.0f), aspect, 0.1f, 100.0f);
        cam.view = glm::lookAt(glm::vec3(0.0f, 0.3f, 6.5f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    Pipeline pipeline;
    if (auto r = pipeline.Initialize(host->GetWindow(), assets); r.IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(r.Error()));
        return 1;
    }
    PostProcessChain chain = CreateDefault();
    pipeline.SetPostProcessChain(&chain);
    pipeline.SetMaterialSystem(&materials);
    pipeline.SetShadowConfig(ShadowConfig{});

    // 触发 IBL 三件套烘焙 + SetIblTextures 接通。envHandle 失效时 Pipeline
    // 内部 graceful fallback 到 dummy IBL（不会 throw / 不阻断）。
    pipeline.BakeIblFromWorld(world, assets);

    host->PushLayer(std::make_unique<RenderLayer>(pipeline, world,
                                                  host.get(), capturePath,
                                                  exitAfterFrames));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
