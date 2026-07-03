// samples/17_imgui_overlay —— GAP-2026-05-27-consumer-imgui-tuning-hook 端到端
// 验收。证明：一个**仅经公共 API** 的消费者能在画面上叠 ImGui 窗 + slider，
// 改值实时生效、不重编不重启。
//
// 这是无 play-in-editor 时游戏侧 live-debug / 调参的标准接法：
//   1. pipeline.Initialize(window, assets);
//   2. pipeline.EnableImGui();                       // 引擎托管 context+backend
//   3. pipeline.SetImGuiSubmit([h]{ h->DispatchImGui(); });  // 接到 LayerStack
//   4. Layer::OnImGui() 里直接调 ImGui API 提交调参窗口。
//
// 场景：一个旋转 cube + DirectionalLight + perspective camera。Tuning 面板的
// slider 实时改：cube 旋转速度 / 主光强度 / 场景 clear color —— 全部即时反映
// 到画面，无需重编。ESC 退出。
//
// 用法：直接运行 build/bin/Debug/17_imgui_overlay.exe。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/asset/AssetHandle.h>
#include <orange/engine/asset/AssetRegistry.h>
#include <orange/engine/asset/MeshAsset.h>
#include <orange/engine/asset/ShaderAsset.h>
#include <orange/engine/asset/ShaderLoader.h>
#include <orange/engine/platform/WindowEvent.h>
#include <orange/engine/render/Camera.h>
#include <orange/engine/render/LightComponent.h>
#include <orange/engine/render/MaterialInstance.h>
#include <orange/engine/render/MaterialSystem.h>
#include <orange/engine/render/Pipeline.h>
#include <orange/engine/render/RenderableComponent.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/TransformComponent.h>
#include <orange/engine/scene/World.h>

// 消费者自行 #include <imgui.h>（引擎把 imgui include 目录 PUBLIC 暴露，
// 公共头本身零 imgui 类型）。
#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using namespace Orange::Engine;
using Orange::Engine::Asset::AssetHandle;
using Orange::Engine::Asset::AssetRegistry;
using Orange::Engine::Asset::MeshAsset;
using Orange::Engine::Asset::ShaderAsset;
using Orange::Engine::Asset::ShaderLoader;
using Orange::Engine::Asset::VertexPosition3;
using Orange::Engine::Asset::VertexUV2;
using Orange::Engine::Render::Camera;
using Orange::Engine::Render::DirectionalLight;
using Orange::Engine::Render::MaterialInstance;
using Orange::Engine::Render::MaterialSystem;
using Orange::Engine::Render::Pipeline;
using Orange::Engine::Render::RenderableComponent;
using Orange::Engine::Scene::TransformComponent;

namespace
{

    // 6 面 24 顶点立方体（与 sample 15 同款）。
    std::unique_ptr<MeshAsset> MakeUnitCubeMesh()
    {
        constexpr float                               h     = 0.5f;
        std::array<std::array<VertexPosition3, 4>, 6> faces = {{
            {{{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}},     // +X
            {{{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}}, // -X
            {{{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}},     // +Y
            {{{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}}, // -Y
            {{{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},     // +Z
            {{{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}}, // -Z
        }};

        std::vector<VertexPosition3> positions;
        std::vector<VertexUV2>       uvs;
        std::vector<std::uint32_t>   indices;
        positions.reserve(24);
        uvs.reserve(24);
        indices.reserve(36);

        for (const auto& face : faces)
        {
            const std::uint32_t base = static_cast<std::uint32_t>(positions.size());
            for (int i = 0; i < 4; ++i)
            {
                positions.push_back(face[i]);
                uvs.push_back({0.0f, 0.0f});
            }
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }

        auto pMesh = std::make_unique<MeshAsset>(std::move(positions),
                                                 std::move(uvs),
                                                 std::move(indices));
        pMesh->ComputeSmoothNormalsFromTriangles();
        return pMesh;
    }

    // 调参 + 渲染 Layer。OnImGui 提交 Tuning 面板，OnUpdate 把面板状态实时写进
    // 场景（旋转速度 / 主光强度 / clear color），证明改值即时生效。
    class TuningLayer : public Layer
    {
    public:
        TuningLayer(Pipeline& pipeline, World& world, Entity cube, Entity light)
            : Layer("TuningLayer"), mPipeline(pipeline), mWorld(world), mCube(cube), mLight(light) {}

        void OnImGui() override
        {
            ImGui::Begin("Tuning (live, no recompile)");
            ImGui::TextUnformatted("调 slider 看画面实时变化：");
            ImGui::SliderFloat("rotation speed", &mRotationSpeed, 0.0f, 4.0f);
            ImGui::SliderFloat("light intensity", &mLightIntensity, 0.0f, 5.0f);
            ImGui::ColorEdit3("clear color", mClearColor);
            ImGui::Separator();
            ImGui::Text("FPS: %.1f (%.3f ms/frame)",
                        static_cast<double>(ImGui::GetIO().Framerate),
                        1000.0 / static_cast<double>(ImGui::GetIO().Framerate));
            ImGui::End();
        }

        void OnUpdate(const FrameContext& frame) override
        {
            mAngle += mRotationSpeed * frame.time.deltaSeconds;
            if (auto* xf = mWorld.GetComponent<TransformComponent>(mCube))
            {
                const glm::vec3 axis = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
                xf->rotation         = glm::angleAxis(mAngle, axis);
            }
            if (auto* light = mWorld.GetComponent<DirectionalLight>(mLight))
            {
                light->intensity = mLightIntensity;
            }
            mPipeline.SetSceneClearColor(mClearColor[0], mClearColor[1], mClearColor[2]);

            mPipeline.Render(mWorld);
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
        Pipeline& mPipeline;
        World&    mWorld;
        Entity    mCube;
        Entity    mLight;

        float mAngle          = 0.0f;
        float mRotationSpeed  = 0.8f;
        float mLightIntensity = 1.0f;
        float mClearColor[3]  = {0.05f, 0.07f, 0.10f};
    };

} // namespace

int main()
{
    AppConfig cfg{};
    cfg.window.title  = "OrangeEngine - 17 imgui_overlay";
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
        std::fprintf(stderr, "RegisterLoader<ShaderAsset> failed (code=%u)\n",
                     static_cast<unsigned>(reg.Error()));
        return 1;
    }

    auto meshHandleResult = assets.Insert<MeshAsset>("builtin/cube", MakeUnitCubeMesh());
    if (meshHandleResult.IsErr())
    {
        std::fprintf(stderr, "Insert<MeshAsset> failed (code=%u)\n",
                     static_cast<unsigned>(meshHandleResult.Error()));
        return 1;
    }
    AssetHandle<MeshAsset> meshHandle = meshHandleResult.Value();

    MaterialSystem materials(assets);
    if (auto rb = materials.RegisterBuiltins(); rb.IsErr())
    {
        std::fprintf(stderr, "RegisterBuiltins failed (code=%u)\n",
                     static_cast<unsigned>(rb.Error()));
        return 1;
    }
    auto cubeInstance = materials.CreateInstance("textured");
    if (!cubeInstance)
    {
        std::fprintf(stderr, "CreateInstance(\"textured\") returned null\n");
        return 1;
    }

    World world;

    Entity cubeEntity = world.CreateEntity();
    world.AddComponent(cubeEntity, TransformComponent{});
    {
        RenderableComponent r;
        r.mesh             = meshHandle;
        r.materialInstance = cubeInstance.get();
        world.AddComponent(cubeEntity, r);
    }

    Entity lightEntity = world.CreateEntity();
    {
        TransformComponent xf;
        xf.rotation = glm::quatLookAt(
            glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)),
            glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(lightEntity, xf);

        DirectionalLight light;
        light.color     = glm::vec3(1.0f, 0.95f, 0.85f);
        light.intensity = 1.0f;
        world.AddComponent(lightEntity, light);
    }

    Entity camEntity = world.CreateEntity();
    {
        const float aspect = static_cast<float>(cfg.window.width) / static_cast<float>(cfg.window.height);
        Camera      cam    = Camera::Perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        cam.view           = glm::lookAt(glm::vec3(2.8f, 2.0f, 3.0f),
                                         glm::vec3(0.0f, 0.0f, 0.0f),
                                         glm::vec3(0.0f, 1.0f, 0.0f));
        world.AddComponent(camEntity, cam);
    }

    Pipeline pipeline;
    auto     initResult = pipeline.Initialize(host->GetWindow(), assets);
    if (initResult.IsErr())
    {
        std::fprintf(stderr, "Pipeline::Initialize failed (code=%u)\n",
                     static_cast<unsigned>(initResult.Error()));
        return 1;
    }
    pipeline.SetMaterialSystem(&materials);

    // 接通引擎托管 ImGui overlay + 把 LayerStack 的 OnImGui 派发接进来。
    if (auto im = pipeline.EnableImGui(); im.IsErr())
    {
        std::fprintf(stderr, "Pipeline::EnableImGui failed (code=%u)\n",
                     static_cast<unsigned>(im.Error()));
        return 1;
    }
    pipeline.SetImGuiSubmit([h = host.get()]()
                            { h->DispatchImGui(); });

    host->PushLayer(std::make_unique<TuningLayer>(pipeline, world, cubeEntity, lightEntity));

    const int rc = host->Run();
    pipeline.Shutdown();
    return rc;
}
