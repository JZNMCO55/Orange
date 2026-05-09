// Input 模块实现 —— ActionMap + InputContext + JSON 加载。
//
// 头隔离：本 .cpp 是 Input 模块**唯一**直接消费 nlohmann::json 的地方
// （透过 Core::Serialization 间接走，不直接 include nlohmann）；任何
// Input 公共头都不暴露 nlohmann。

#include "orange/engine/input/ActionMap.h"
#include "orange/engine/input/InputContext.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Orange::Engine::Input
{

// ---------------------------------------------------------------------------
// ActionMap
// ---------------------------------------------------------------------------

namespace
{

// 找同名 Action 的索引；未命中 → npos。
std::size_t FindIndex(const std::vector<Action>& v, std::string_view name) noexcept
{
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        if (v[i].name == name)
        {
            return i;
        }
    }
    return std::size_t{static_cast<std::size_t>(-1)};
}

}  // namespace

void ActionMap::AddAction(Action action)
{
    const auto idx = FindIndex(mActions, action.name);
    if (idx != static_cast<std::size_t>(-1))
    {
        // 替换：保留 state，但 binding / type 全换。这样 hot-reload
        // 配置文件时不会让玩家正在按下的动作"瞬间松手再按下"。
        const ActionState prevState = mActions[idx].state;
        mActions[idx] = std::move(action);
        mActions[idx].state = prevState;
        return;
    }
    action.state = ActionState::Idle;
    mActions.emplace_back(std::move(action));
}

const Action* ActionMap::Find(std::string_view name) const noexcept
{
    const auto idx = FindIndex(mActions, name);
    return idx == static_cast<std::size_t>(-1) ? nullptr : &mActions[idx];
}

std::size_t ActionMap::Size() const noexcept
{
    return mActions.size();
}

bool ActionMap::Empty() const noexcept
{
    return mActions.empty();
}

std::span<const Action> ActionMap::Actions() const noexcept
{
    return std::span<const Action>{mActions.data(), mActions.size()};
}

ActionState ActionMap::GetState(std::string_view name) const noexcept
{
    const Action* a = Find(name);
    return a == nullptr ? ActionState::Idle : a->state;
}

void ActionMap::BeginFrame() noexcept
{
    for (auto& a : mActions)
    {
        if (a.state == ActionState::Pressed)
        {
            a.state = ActionState::Held;
        }
        else if (a.state == ActionState::Released)
        {
            a.state = ActionState::Idle;
        }
    }
}

void ActionMap::PostKeyEvent(KeyCode key, bool isDown)
{
    DispatchEvent(ActionBinding::Source::Key, static_cast<std::int32_t>(key), isDown);
}

void ActionMap::PostMouseButton(MouseButton button, bool isDown)
{
    DispatchEvent(ActionBinding::Source::Mouse, static_cast<std::int32_t>(button), isDown);
}

void ActionMap::Reset() noexcept
{
    for (auto& a : mActions)
    {
        a.state = ActionState::Idle;
    }
}

void ActionMap::DispatchEvent(ActionBinding::Source src, std::int32_t code, bool isDown)
{
    for (auto& a : mActions)
    {
        if (a.type != ActionType::Button)
        {
            continue;  // 其它类型 Phase 4 不消费
        }
        bool hit = false;
        for (const auto& b : a.bindings)
        {
            if (b.source == src && b.codeOrButton == code)
            {
                hit = true;
                break;
            }
        }
        if (!hit)
        {
            continue;
        }
        if (isDown)
        {
            // 只有 Idle / Released 才接受新一轮 Pressed；已 Held 不重复触发。
            if (a.state == ActionState::Idle || a.state == ActionState::Released)
            {
                a.state = ActionState::Pressed;
            }
        }
        else
        {
            // 抬起：从 Pressed / Held 进 Released；Idle 状态收到松开事件
            // 视为"未按过即收到 release" 的虚假事件，忽略。
            if (a.state == ActionState::Pressed || a.state == ActionState::Held)
            {
                a.state = ActionState::Released;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// InputContext
// ---------------------------------------------------------------------------

std::size_t InputContext::Push(ActionMap map)
{
    map.Reset();  // 进栈即把状态清回 Idle，避免上层"残留按键"窜入
    mStack.emplace_back(std::move(map));
    return mStack.size();
}

std::size_t InputContext::Pop()
{
    if (mStack.empty())
    {
        return 0;
    }
    mStack.pop_back();
    if (!mStack.empty())
    {
        // 退栈时把回到栈顶的 map 状态清空——避免"按住 W 进暂停菜单 +
        // 退暂停后主玩法瞬间向前冲" 的卡键 bug。
        mStack.back().Reset();
    }
    return mStack.size();
}

ActionMap* InputContext::Top() noexcept
{
    return mStack.empty() ? nullptr : &mStack.back();
}

const ActionMap* InputContext::Top() const noexcept
{
    return mStack.empty() ? nullptr : &mStack.back();
}

std::size_t InputContext::Depth() const noexcept
{
    return mStack.size();
}

bool InputContext::Empty() const noexcept
{
    return mStack.empty();
}

void InputContext::BeginFrame() noexcept
{
    if (auto* t = Top())
    {
        t->BeginFrame();
    }
}

void InputContext::PostKeyEvent(KeyCode key, bool isDown)
{
    if (auto* t = Top())
    {
        t->PostKeyEvent(key, isDown);
    }
}

void InputContext::PostMouseButton(MouseButton button, bool isDown)
{
    if (auto* t = Top())
    {
        t->PostMouseButton(button, isDown);
    }
}

// ---------------------------------------------------------------------------
// JSON loader
// ---------------------------------------------------------------------------

namespace
{

bool StartsWith(std::string_view s, std::string_view prefix) noexcept
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

// 大小写不敏感比较——binding 字符串解析时容忍 "key:space" / "key:Space"。
bool IEquals(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca != cb)
        {
            return false;
        }
    }
    return true;
}

// 把 "key:Space" 这种字符串解析成 ActionBinding。失败返 false。
bool ParseBinding(std::string_view text, ActionBinding& out)
{
    auto colon = text.find(':');
    if (colon == std::string_view::npos)
    {
        return false;
    }
    const std::string_view srcPart  = text.substr(0, colon);
    const std::string_view namePart = text.substr(colon + 1);
    if (namePart.empty())
    {
        return false;
    }

    if (IEquals(srcPart, "key"))
    {
        out.source = ActionBinding::Source::Key;
#define ORANGE_KEY_CASE(N) if (IEquals(namePart, #N)) { out.codeOrButton = static_cast<std::int32_t>(KeyCode::N); return true; }
        ORANGE_KEY_CASE(Space)
        ORANGE_KEY_CASE(Apostrophe)
        ORANGE_KEY_CASE(Comma)
        ORANGE_KEY_CASE(Minus)
        ORANGE_KEY_CASE(Period)
        ORANGE_KEY_CASE(Slash)
        ORANGE_KEY_CASE(Num0)
        ORANGE_KEY_CASE(Num1)
        ORANGE_KEY_CASE(Num2)
        ORANGE_KEY_CASE(Num3)
        ORANGE_KEY_CASE(Num4)
        ORANGE_KEY_CASE(Num5)
        ORANGE_KEY_CASE(Num6)
        ORANGE_KEY_CASE(Num7)
        ORANGE_KEY_CASE(Num8)
        ORANGE_KEY_CASE(Num9)
        ORANGE_KEY_CASE(Semicolon)
        ORANGE_KEY_CASE(Equal)
        ORANGE_KEY_CASE(A) ORANGE_KEY_CASE(B) ORANGE_KEY_CASE(C) ORANGE_KEY_CASE(D)
        ORANGE_KEY_CASE(E) ORANGE_KEY_CASE(F) ORANGE_KEY_CASE(G) ORANGE_KEY_CASE(H)
        ORANGE_KEY_CASE(I) ORANGE_KEY_CASE(J) ORANGE_KEY_CASE(K) ORANGE_KEY_CASE(L)
        ORANGE_KEY_CASE(M) ORANGE_KEY_CASE(N) ORANGE_KEY_CASE(O) ORANGE_KEY_CASE(P)
        ORANGE_KEY_CASE(Q) ORANGE_KEY_CASE(R) ORANGE_KEY_CASE(S) ORANGE_KEY_CASE(T)
        ORANGE_KEY_CASE(U) ORANGE_KEY_CASE(V) ORANGE_KEY_CASE(W) ORANGE_KEY_CASE(X)
        ORANGE_KEY_CASE(Y) ORANGE_KEY_CASE(Z)
        ORANGE_KEY_CASE(Escape)
        ORANGE_KEY_CASE(Enter)
        ORANGE_KEY_CASE(Tab)
        ORANGE_KEY_CASE(Backspace)
        ORANGE_KEY_CASE(Right) ORANGE_KEY_CASE(Left) ORANGE_KEY_CASE(Down) ORANGE_KEY_CASE(Up)
        ORANGE_KEY_CASE(F1) ORANGE_KEY_CASE(F2) ORANGE_KEY_CASE(F3) ORANGE_KEY_CASE(F4)
        ORANGE_KEY_CASE(F5) ORANGE_KEY_CASE(F6) ORANGE_KEY_CASE(F7) ORANGE_KEY_CASE(F8)
        ORANGE_KEY_CASE(F9) ORANGE_KEY_CASE(F10) ORANGE_KEY_CASE(F11) ORANGE_KEY_CASE(F12)
        ORANGE_KEY_CASE(LeftShift) ORANGE_KEY_CASE(LeftControl) ORANGE_KEY_CASE(LeftAlt)
        ORANGE_KEY_CASE(RightShift) ORANGE_KEY_CASE(RightControl) ORANGE_KEY_CASE(RightAlt)
#undef ORANGE_KEY_CASE
        return false;
    }
    if (IEquals(srcPart, "mouse"))
    {
        out.source = ActionBinding::Source::Mouse;
        if (IEquals(namePart, "Left"))   { out.codeOrButton = static_cast<std::int32_t>(MouseButton::Left);   return true; }
        if (IEquals(namePart, "Right"))  { out.codeOrButton = static_cast<std::int32_t>(MouseButton::Right);  return true; }
        if (IEquals(namePart, "Middle")) { out.codeOrButton = static_cast<std::int32_t>(MouseButton::Middle); return true; }
        return false;
    }
    if (IEquals(srcPart, "gamepad"))
    {
        out.source = ActionBinding::Source::Gamepad;
        if (IEquals(namePart, "South")) { out.codeOrButton = static_cast<std::int32_t>(GamepadButton::South); return true; }
        if (IEquals(namePart, "East"))  { out.codeOrButton = static_cast<std::int32_t>(GamepadButton::East);  return true; }
        if (IEquals(namePart, "West"))  { out.codeOrButton = static_cast<std::int32_t>(GamepadButton::West);  return true; }
        if (IEquals(namePart, "North")) { out.codeOrButton = static_cast<std::int32_t>(GamepadButton::North); return true; }
        if (IEquals(namePart, "LB"))    { out.codeOrButton = static_cast<std::int32_t>(GamepadButton::LB);    return true; }
        if (IEquals(namePart, "RB"))    { out.codeOrButton = static_cast<std::int32_t>(GamepadButton::RB);    return true; }
        if (IEquals(namePart, "Back"))  { out.codeOrButton = static_cast<std::int32_t>(GamepadButton::Back);  return true; }
        if (IEquals(namePart, "Start")) { out.codeOrButton = static_cast<std::int32_t>(GamepadButton::Start); return true; }
        return false;
    }
    return false;
}

}  // namespace

Result<ActionMap, ResultCode> LoadActionMapFromJson(const JsonReader& reader)
{
    // Schema 版本校验：我们只接受 namespace="input.action_map" 的 v1.x。
    // 缺 schema_version → 直接拒；这条与 docs/extension-points.md 的
    // "所有可序列化类型必须显式带 schema" 约定一致。
    if (!reader.Has("schema_version"))
    {
        return ResultCode::InvalidArgument;
    }

    const std::size_t actionCount = reader.ArraySize("actions");

    ActionMap map;
    for (std::size_t i = 0; i < actionCount; ++i)
    {
        const std::string base = "actions/" + std::to_string(i);

        Action action;
        std::string nameField;
        if (!reader.ReadString(base + "/name", nameField))
        {
            return ResultCode::InvalidArgument;
        }
        action.name = std::move(nameField);
        action.type = ActionType::Button;  // Phase 4 / Task 08 唯一类型

        const std::size_t bindingCount = reader.ArraySize(base + "/bindings");
        action.bindings.reserve(bindingCount);
        for (std::size_t b = 0; b < bindingCount; ++b)
        {
            std::string bstr;
            if (!reader.ReadString(base + "/bindings/" + std::to_string(b), bstr))
            {
                return ResultCode::InvalidArgument;
            }
            ActionBinding bind;
            if (!ParseBinding(bstr, bind))
            {
                return ResultCode::InvalidArgument;
            }
            action.bindings.emplace_back(bind);
        }

        map.AddAction(std::move(action));
    }
    return map;
}

Result<ActionMap, ResultCode> LoadActionMapFromFile(std::string_view path)
{
    auto rd = JsonReader::FromFile(path);
    if (rd.IsErr())
    {
        return rd.Error().code;
    }
    return LoadActionMapFromJson(rd.Value());
}

}  // namespace Orange::Engine::Input
