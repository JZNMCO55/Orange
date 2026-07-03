#ifndef ORANGE_ENGINE_INPUT_ACTION_MAP_H
#define ORANGE_ENGINE_INPUT_ACTION_MAP_H

// ---------------------------------------------------------------------------
// ActionMap —— 一组 Action 的集合 + 物理事件 → Action state 的路由层。
//
// 一个 ActionMap 通常对应游戏中的"一个语境"——主玩法 / 暂停菜单 / 调
// 试 overlay 等都用各自的 ActionMap，由 InputContext 栈管理。
//
// 调用约定：
//   * AddAction：注册 Action（含 bindings）；同名 Add 替换；
//   * BeginFrame：状态推进——把上一帧的 Pressed → Held、Released → Idle；
//   * PostKeyEvent / PostMouseButton：把物理事件喂给 map，命中 binding 的
//     Action 进 Pressed（若上一帧 Idle）/ Released（若上一帧 Pressed/Held）；
//   * GetState(name)：按名查当前状态；不存在 → Idle。
//
// 同一物理 key 绑到多条 Action：每条独立计状态——按下时所有命中条目
// 一起 Pressed，松开时一起 Released。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/input/Action.h>
#include <orange/engine/input/InputDevice.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine::Input
{

    class ORANGE_ENGINE_API ActionMap
    {
    public:
        ActionMap() = default;

        // 注册 / 替换 Action。同名直接覆盖（保留 bindings）；初始 state = Idle。
        void AddAction(Action action);

        // 按名查 Action。找不到 → nullptr。修改返回的 Action 不影响 map 内副本
        // （map 内是值存储）；要更新 binding 用 AddAction 替换。
        const Action* Find(std::string_view name) const noexcept;

        // 当前 actions 数量 + 全部 actions 的只读 span（顺序与 AddAction 调用
        // 顺序一致，方便顺序遍历）。
        std::size_t             Size() const noexcept;
        bool                    Empty() const noexcept;
        std::span<const Action> Actions() const noexcept;

        // 当前帧状态查询。找不到 / Idle 都返 Idle。
        ActionState GetState(std::string_view name) const noexcept;

        // 帧边界推进——把 Pressed → Held、Released → Idle。该在主循环
        // begin-frame 处调一次；之后再喂 PostXxx 事件，下一次 BeginFrame 才
        // 把刚发生的 Pressed 进 Held。
        void BeginFrame() noexcept;

        // 物理事件入口。命中 binding 的 Action 状态机走一步。
        void PostKeyEvent(KeyCode key, bool isDown);
        void PostMouseButton(MouseButton button, bool isDown);

        // 全部 Action 状态重置 Idle（典型用于"上下文切换 / 暂停时的清空"）。
        void Reset() noexcept;

    private:
        std::vector<Action> mActions;

        // 处理 binding 命中的核心：把按下 / 抬起翻成状态变更。
        void DispatchEvent(ActionBinding::Source src, std::int32_t code, bool isDown);
    };

} // namespace Orange::Engine::Input

#endif // ORANGE_ENGINE_INPUT_ACTION_MAP_H
