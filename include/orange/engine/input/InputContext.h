#ifndef ORANGE_ENGINE_INPUT_INPUT_CONTEXT_H
#define ORANGE_ENGINE_INPUT_INPUT_CONTEXT_H

// ---------------------------------------------------------------------------
// InputContext —— ActionMap 栈 + 物理事件分发。
//
// 模式：游戏典型场景下"主玩法 / 暂停菜单 / 调试 overlay"各自有独立
// 的 ActionMap，进入新语境时 Push 一个 map 上去，离开时 Pop 回到旧
// map——对栈语义最自然。
//
// 当前路由：物理事件**只**喂给栈顶 map（最高优先级），其它 map 不收
// 事件、状态被冻结在它们最后一次活跃时——这避免"暂停菜单弹出后玩
// 家手指还按着 W"导致返回主玩法时主角立刻向前跑的"按键卡住" bug。
//
// 进 / 退栈时 map.Reset() 一遍，确保状态从 Idle 起步。
//
// 用法：
//     InputContext ctx;
//     auto idx = ctx.Push(loadedMap);
//     // 主循环：
//     ctx.BeginFrame();
//     // glfw key callback → ctx.PostKeyEvent(key, isDown)
//     auto state = ctx.Top()->GetState("jump");
//     // 退出：
//     ctx.Pop();
//
// JSON 加载入口（同文件提供）：LoadActionMapFromJson(reader) → ActionMap，
// 由 src/input/InputContext.cpp 实现。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/core/Serialization.h>
#include <orange/engine/input/ActionMap.h>
#include <orange/engine/input/InputDevice.h>

#include <cstddef>
#include <string_view>
#include <vector>

namespace Orange::Engine::Input
{

class ORANGE_ENGINE_API InputContext
{
public:
    InputContext() = default;
    ~InputContext() = default;

    InputContext(const InputContext&)            = delete;
    InputContext& operator=(const InputContext&) = delete;

    InputContext(InputContext&&) noexcept            = default;
    InputContext& operator=(InputContext&&) noexcept = default;

    // 把一个 map 压入栈顶；map 按值存（context 持所有权）。
    // 返回新栈顶索引（可用于诊断 / 与 Pop 配对）。
    std::size_t Push(ActionMap map);

    // 弹出栈顶；空栈时 no-op。返回当前栈大小（弹后）。
    std::size_t Pop();

    // 当前栈顶 / 栈大小。空栈时 Top 返 nullptr。
    ActionMap*       Top() noexcept;
    const ActionMap* Top() const noexcept;
    std::size_t      Depth() const noexcept;
    bool             Empty() const noexcept;

    // 帧边界——把栈顶 map 的状态推进一步。其它 map 不动（被冻结）。
    void BeginFrame() noexcept;

    // 物理事件 → 仅栈顶 map。空栈时 no-op，避免事件丢到无处可去。
    void PostKeyEvent(KeyCode key, bool isDown);
    void PostMouseButton(MouseButton button, bool isDown);

private:
    std::vector<ActionMap> mStack;
};

// JSON 加载：从已经解析好的 reader 读 actions[] 数组，构造 ActionMap。
//
// Schema（v1）：
//     {
//       "schema_version": { "namespace": "input.action_map", "major": 1, "minor": 0 },
//       "actions": [
//         { "name": "jump",  "bindings": ["key:Space"] },
//         { "name": "shoot", "bindings": ["mouse:Left", "key:LeftControl"] }
//       ]
//     }
//
// binding 字符串语法：
//     "key:<KeyCode 名>"        e.g. "key:Space" / "key:W"
//     "mouse:<Left|Right|Middle>"
//     "gamepad:<South|East|...>"   (Phase 4 / Task 08 内不消费，留 hook)
//
// 失败码：InvalidArgument（schema_version 缺失 / actions 不是数组 /
// binding 字符串解析失败）。
ORANGE_ENGINE_API
Result<ActionMap, ResultCode> LoadActionMapFromJson(const JsonReader& reader);

// 直接从磁盘 .json 文件加载。文件不存在 / 解析失败 → 错误码透传。
ORANGE_ENGINE_API
Result<ActionMap, ResultCode> LoadActionMapFromFile(std::string_view path);

}  // namespace Orange::Engine::Input

#endif  // ORANGE_ENGINE_INPUT_INPUT_CONTEXT_H
