// AnimFsmFileIO round-trip 测试 —— 锁住 .anim_fsm（editor/anim_fsm schema）
// 的读写对称 + 节点坐标（editorPos = state.layoutX/Y）持久化 + 旧文件向后
// 兼容。
//
// 背景（B2.5 状态机图编辑器）：状态机图节点的画布坐标存在每条 state 的
// layout 字段里（schema 内序列化为 "layout":[x,y]，运行时忽略、仅供下次
// 打开恢复布局）。该字段与 parameters / transitions.conditions 早随 c2
// 期落地，但此前无 headless 测试守护其读写对称——本测试补上这一守护，
// 是 B2.5 唯一能 headless 真验的部分（节点图所有 ImGui 交互须 dogfood）。
//
// 为什么走 AnimFsmFileIO 而非 AnimFsmAssetInspectorPlugin：后者经 EditorHost
// + ImGui 接管 Inspector，无法 headless 链接。AnimFsmFileIO（ReadAnimFsmFile
// / WriteAnimFsmFile）是 plugin 真实落盘/加载用的同一份 IO seam，纯依赖
// Core::Serialization + AnimFsmModel POD，零 ImGui/GLFW/Vulkan，故可独立编
// 译进测试 exe（同 material_file_io_test 模式）。
//
// 覆盖 4 条路径：
//   1. 全量 round-trip：states（含 layout 坐标）+ transitions（含 conditions）
//      + parameters（4 种 type）+ initialState 写盘 → 读回逐字段比对
//   2. layout（editorPos）读写对称：精确锁住每条 state 的 layoutX/Y 落盘后
//      读回不漂移（含负坐标 / 小数）
//   3. 旧文件向后兼容：手写一个 v1.1 schema 但 states[] 缺 "layout" 字段的
//      JSON（等价 c2-2 早期/手改文件）→ 读回 layout 默认为 (0,0) 且不报错
//   4. 错误路径：文件不存在 / schema namespace 不匹配 → ReadAnimFsmFile 返回
//      nullopt

#include "AnimFsmFileIO.h" // include path 由 CMake 加 tools/OrangeEditor

#include <orange/engine/animation/AnimationStateMachine.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace AnimFsm = ::Orange::Editor::AnimFsm;
namespace Anim    = ::Orange::Engine::Animation;

namespace
{

    bool FloatEq(float a, float b) noexcept
    {
        return std::fabs(a - b) < 1e-5f;
    }

    std::string TempPath(const char* name)
    {
        auto p = std::filesystem::temp_directory_path() / "orange_anim_fsm_io_test" / name;
        std::filesystem::create_directories(p.parent_path());
        return p.string();
    }

    // 在指定路径写一个原始 JSON 字符串（用于构造 reader 兼容性 / 错误 fixture）。
    void WriteRawFile(const std::string& path, const std::string& content)
    {
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        ofs << content;
    }

    // 1. 全量 round-trip：states（含 layout）+ transitions（含 conditions）+
    //    parameters（4 种 type）+ initialState。
    void TestFullRoundTrip()
    {
        const std::string path = TempPath("full_round_trip.anim_fsm");

        AnimFsm::EditableStateMachine out;
        out.initialState = "idle";

        // parameters：覆盖 Bool / Int / Float / Trigger 四种 type + 各自 default
        {
            AnimFsm::EditableParameter pBool;
            pBool.name         = "isMoving";
            pBool.type         = Anim::ParameterType::Bool;
            pBool.defaultValue = true;
            out.parameters.push_back(pBool);

            AnimFsm::EditableParameter pInt;
            pInt.name         = "comboCount";
            pInt.type         = Anim::ParameterType::Int;
            pInt.defaultValue = std::int32_t{3};
            out.parameters.push_back(pInt);

            AnimFsm::EditableParameter pFloat;
            pFloat.name         = "speed";
            pFloat.type         = Anim::ParameterType::Float;
            pFloat.defaultValue = 1.25f;
            out.parameters.push_back(pFloat);

            AnimFsm::EditableParameter pTrigger;
            pTrigger.name         = "jump";
            pTrigger.type         = Anim::ParameterType::Trigger;
            pTrigger.defaultValue = false;
            out.parameters.push_back(pTrigger);
        }

        // states：带非平凡 layout 坐标（含负值 / 小数）
        {
            AnimFsm::EditableState idle;
            idle.name     = "idle";
            idle.clipName = "idle_loop";
            idle.layoutX  = 40.0f;
            idle.layoutY  = 80.0f;
            out.states.push_back(idle);

            AnimFsm::EditableState walk;
            walk.name     = "walk";
            walk.clipName = "walk_cycle";
            walk.layoutX  = 240.5f;
            walk.layoutY  = -16.25f;
            out.states.push_back(walk);
        }

        // transitions：idle→walk 带一条 If(isMoving) condition；walk→idle 无条件
        {
            AnimFsm::EditableTransition toWalk;
            toWalk.fromState = "idle";
            toWalk.toState   = "walk";
            AnimFsm::EditableCondition c;
            c.paramName = "isMoving";
            c.op        = Anim::ConditionOp::If;
            c.threshold = false; // If/IfNot 不消费 threshold
            toWalk.conditions.push_back(c);
            out.transitions.push_back(toWalk);

            AnimFsm::EditableTransition toIdle;
            toIdle.fromState = "walk";
            toIdle.toState   = "idle";
            // 带数值比较 condition：speed < 0.1
            AnimFsm::EditableCondition c2;
            c2.paramName = "speed";
            c2.op        = Anim::ConditionOp::Less;
            c2.threshold = 0.1f;
            toIdle.conditions.push_back(c2);
            out.transitions.push_back(toIdle);
        }

        const bool wrote = AnimFsm::WriteAnimFsmFile(path, out);
        assert(wrote && "WriteAnimFsmFile 应成功");

        auto inOpt = AnimFsm::ReadAnimFsmFile(path);
        assert(inOpt.has_value() && "ReadAnimFsmFile 应成功");
        const AnimFsm::EditableStateMachine& in = *inOpt;

        assert(in.initialState == "idle");

        // ---- parameters 比对 ----
        assert(in.parameters.size() == 4);
        assert(in.parameters[0].name == "isMoving");
        assert(in.parameters[0].type == Anim::ParameterType::Bool);
        assert(std::holds_alternative<bool>(in.parameters[0].defaultValue));
        assert(std::get<bool>(in.parameters[0].defaultValue) == true);

        assert(in.parameters[1].name == "comboCount");
        assert(in.parameters[1].type == Anim::ParameterType::Int);
        assert(std::holds_alternative<std::int32_t>(in.parameters[1].defaultValue));
        assert(std::get<std::int32_t>(in.parameters[1].defaultValue) == 3);

        assert(in.parameters[2].name == "speed");
        assert(in.parameters[2].type == Anim::ParameterType::Float);
        assert(std::holds_alternative<float>(in.parameters[2].defaultValue));
        assert(FloatEq(std::get<float>(in.parameters[2].defaultValue), 1.25f));

        assert(in.parameters[3].name == "jump");
        assert(in.parameters[3].type == Anim::ParameterType::Trigger);

        // ---- states 比对（含 layout）----
        assert(in.states.size() == 2);
        assert(in.states[0].name == "idle");
        assert(in.states[0].clipName == "idle_loop");
        assert(FloatEq(in.states[0].layoutX, 40.0f));
        assert(FloatEq(in.states[0].layoutY, 80.0f));

        assert(in.states[1].name == "walk");
        assert(in.states[1].clipName == "walk_cycle");
        assert(FloatEq(in.states[1].layoutX, 240.5f));
        assert(FloatEq(in.states[1].layoutY, -16.25f));

        // ---- transitions 比对（含 conditions）----
        assert(in.transitions.size() == 2);
        assert(in.transitions[0].fromState == "idle");
        assert(in.transitions[0].toState == "walk");
        assert(in.transitions[0].conditions.size() == 1);
        assert(in.transitions[0].conditions[0].paramName == "isMoving");
        assert(in.transitions[0].conditions[0].op == Anim::ConditionOp::If);

        assert(in.transitions[1].fromState == "walk");
        assert(in.transitions[1].toState == "idle");
        assert(in.transitions[1].conditions.size() == 1);
        assert(in.transitions[1].conditions[0].paramName == "speed");
        assert(in.transitions[1].conditions[0].op == Anim::ConditionOp::Less);
        assert(std::holds_alternative<float>(in.transitions[1].conditions[0].threshold));
        assert(FloatEq(std::get<float>(in.transitions[1].conditions[0].threshold), 0.1f));

        std::printf("[ok] TestFullRoundTrip\n");
    }

    // 2. layout（editorPos）读写对称 —— 单独锁住坐标这一条，覆盖多组坐标值
    //    （正/负/零/小数）跑两次 round-trip（写→读→再写→再读）保证幂等。
    void TestLayoutRoundTripStable()
    {
        const std::string path = TempPath("layout_round_trip.anim_fsm");

        struct Sample
        {
            const char* name;
            float       x;
            float       y;
        };
        const Sample samples[] = {
            {"a", 0.0f, 0.0f},
            {"b", 12.5f, -340.75f},
            {"c", -1000.0f, 0.125f},
            {"d", 333.333f, 999.999f},
        };

        AnimFsm::EditableStateMachine out;
        for (const auto& s : samples)
        {
            AnimFsm::EditableState st;
            st.name    = s.name;
            st.layoutX = s.x;
            st.layoutY = s.y;
            out.states.push_back(st);
        }
        out.initialState = "a";

        assert(AnimFsm::WriteAnimFsmFile(path, out));
        auto firstOpt = AnimFsm::ReadAnimFsmFile(path);
        assert(firstOpt.has_value());

        // 第二轮：把读回的再写一次再读，断言两轮坐标完全一致（幂等 / 无累积漂移）
        assert(AnimFsm::WriteAnimFsmFile(path, *firstOpt));
        auto secondOpt = AnimFsm::ReadAnimFsmFile(path);
        assert(secondOpt.has_value());

        const auto& first  = *firstOpt;
        const auto& second = *secondOpt;
        assert(first.states.size() == 4);
        assert(second.states.size() == 4);
        for (std::size_t i = 0; i < 4; ++i)
        {
            assert(first.states[i].name == samples[i].name);
            assert(FloatEq(first.states[i].layoutX, samples[i].x));
            assert(FloatEq(first.states[i].layoutY, samples[i].y));
            // 两轮一致
            assert(FloatEq(first.states[i].layoutX, second.states[i].layoutX));
            assert(FloatEq(first.states[i].layoutY, second.states[i].layoutY));
        }

        std::printf("[ok] TestLayoutRoundTripStable\n");
    }

    // 3. 向后兼容：states[] 缺 "layout" 字段（早期手改文件）→ 读回 layout 默认
    //    (0,0)，文件仍正常 load（不因缺字段 reject）。
    void TestMissingLayoutBackwardCompat()
    {
        const std::string path = TempPath("no_layout.anim_fsm");

        // 手写一个 schema namespace/major 正确、states 无 layout 字段的最小文件。
        // （reader 对 layout 走 ReadFloatArray 默认到 0；缺字段不报错）
        const std::string raw = R"({
  "schemaVersion": { "namespace": "editor/anim_fsm", "major": 1, "minor": 1 },
  "initialState": "idle",
  "parameters": [],
  "states": [
    { "name": "idle", "clipName": "idle_loop" },
    { "name": "walk", "clipName": "" }
  ],
  "transitions": [
    { "from": "idle", "to": "walk" }
  ]
})";
        WriteRawFile(path, raw);

        auto inOpt = AnimFsm::ReadAnimFsmFile(path);
        assert(inOpt.has_value() && "缺 layout 的合法文件仍应 load 成功");
        const auto& in = *inOpt;

        assert(in.states.size() == 2);
        // 缺 layout → 默认 (0,0)
        assert(FloatEq(in.states[0].layoutX, 0.0f));
        assert(FloatEq(in.states[0].layoutY, 0.0f));
        assert(FloatEq(in.states[1].layoutX, 0.0f));
        assert(FloatEq(in.states[1].layoutY, 0.0f));
        // transition（无 conditions）正常读入
        assert(in.transitions.size() == 1);
        assert(in.transitions[0].conditions.empty());

        std::printf("[ok] TestMissingLayoutBackwardCompat\n");
    }

    // 4. 错误路径：不存在文件 / schema namespace 不匹配 → nullopt。
    void TestErrorPaths()
    {
        // 不存在
        {
            auto opt = AnimFsm::ReadAnimFsmFile(TempPath("does_not_exist.anim_fsm"));
            assert(!opt.has_value());
        }
        // namespace 不匹配
        {
            const std::string path = TempPath("wrong_namespace.anim_fsm");
            const std::string raw  = R"({
  "schemaVersion": { "namespace": "editor/material", "major": 1, "minor": 0 },
  "states": []
})";
            WriteRawFile(path, raw);
            auto opt = AnimFsm::ReadAnimFsmFile(path);
            assert(!opt.has_value());
        }
        // 同名 state → 整文件 reject
        {
            const std::string path = TempPath("dup_state.anim_fsm");
            const std::string raw  = R"({
  "schemaVersion": { "namespace": "editor/anim_fsm", "major": 1, "minor": 1 },
  "states": [ { "name": "dup" }, { "name": "dup" } ]
})";
            WriteRawFile(path, raw);
            auto opt = AnimFsm::ReadAnimFsmFile(path);
            assert(!opt.has_value());
        }

        std::printf("[ok] TestErrorPaths\n");
    }

} // namespace

int main()
{
    TestFullRoundTrip();
    TestLayoutRoundTripStable();
    TestMissingLayoutBackwardCompat();
    TestErrorPaths();
    std::printf("AnimFsmFileIORoundTripTest: all passed\n");
    return 0;
}
