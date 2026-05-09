// OrangeEditor —— Phase 6 / Task 06-01 起步骨架。
//
// 当前阶段刻意保持最小可执行体：
//   * 开窗（GLFW + Vulkan，由 AppHost 内部接通）
//   * Esc / 关窗按钮退出
//   * 不渲染任何场景内容 —— viewport / scene 预览要等内置 shader (SPV)
//     部署到 editor exe 旁边后再上，那是 Phase 6 / Task 06-04 真要画
//     viewport 时才解决的事
//   * 不接 ImGui —— 留给 Task 06-02
//
// 本 main.cpp 唯一证明的事：
//   * `find_package(OrangeEngine)` 的消费链通了
//   * 引擎公共 API 足以从零起步搭一个独立 target
//
// 后续 Task 06-02..07 的 UI / 实体树 / 检视器 / 工具栏 / 场景持久化都
// 在这个文件之上增量建。

#include <orange/engine/app/AppConfig.h>
#include <orange/engine/app/AppHost.h>
#include <orange/engine/app/FrameContext.h>
#include <orange/engine/app/Layer.h>
#include <orange/engine/platform/WindowEvent.h>

#include <cstdio>
#include <memory>
#include <variant>

namespace
{

// GLFW 的 Esc raw key code。Window 层不做 KeyCode 翻译（参见
// platform/WindowEvent.h 注释）；本编辑器目前只需识别 Esc 一个键，
// 直接对原始整数比对即可，不必为此引入 InputContext 整套栈。
// 与 input/InputDevice.h 的 `KeyCode::Escape` 同值。
constexpr std::int32_t kEscapeKeyRaw = 256;

class EscQuitLayer : public Orange::Engine::Layer
{
public:
    explicit EscQuitLayer(Orange::Engine::AppHost& host)
        : Layer("EscQuit"), mHost(host) {}

    bool OnEvent(const Orange::Engine::Platform::WindowEvent& event) override
    {
        const auto* key = std::get_if<Orange::Engine::Platform::KeyEvent>(&event);
        if (key == nullptr) { return false; }
        // 只响应按下，不响应 Repeat / Release —— 后两者在玩家长按 Esc 时
        // 反复触发会扰乱 RequestExit 语义。
        if (key->action != Orange::Engine::Platform::KeyAction::Press) { return false; }
        if (key->key != kEscapeKeyRaw)                                  { return false; }
        std::fprintf(stdout, "[OrangeEditor] Esc 按下，请求退出\n");
        mHost.RequestExit();
        return true;
    }

private:
    Orange::Engine::AppHost& mHost;
};

}  // namespace

int main()
{
    using namespace Orange::Engine;

    AppConfig cfg{};
    cfg.window.title  = "OrangeEditor v0.0 (scaffold)";
    cfg.window.width  = 1280;
    cfg.window.height = 720;

    auto hostResult = AppHost::Create(cfg);
    if (hostResult.IsErr())
    {
        std::fprintf(stderr,
                     "[OrangeEditor] AppHost::Create 失败 (code=%u)\n",
                     static_cast<unsigned>(hostResult.Error()));
        return 1;
    }
    auto host = std::move(hostResult).Value();

    host->PushLayer(std::make_unique<EscQuitLayer>(*host));

    std::fprintf(stdout,
                 "[OrangeEditor] v0.0 scaffold 启动 —— 关窗或 Esc 退出。\n"
                 "[OrangeEditor] 后续 Task 06-02 接入 ImGui dock space。\n");

    return host->Run();
}
