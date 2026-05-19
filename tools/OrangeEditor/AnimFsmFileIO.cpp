// AnimFsmFileIO 实现 —— .anim_fsm 文件 v1.0 schema 读写。详见同名 .h
// 头注释（schema 结构 + 跨引用检查策略 + Condition DSL bump 路径）。

#include "AnimFsmFileIO.h"

#include <orange/engine/core/Serialization.h>

#include <cstdio>
#include <unordered_set>

namespace Orange::Editor::AnimFsm
{
namespace
{

using ::Orange::Engine::JsonReader;
using ::Orange::Engine::JsonWriter;
using ::Orange::Engine::ResultCode;
using ::Orange::Engine::SchemaVersion;

constexpr const char* kSchemaNamespace = "editor/anim_fsm";
constexpr int         kSchemaMajor     = 1;
constexpr int         kSchemaMinor     = 0;

}  // namespace

std::optional<EditableStateMachine> ReadAnimFsmFile(const std::string& path)
{
    auto rr = JsonReader::FromFile(path);
    if (rr.IsErr())
    {
        std::fprintf(stderr,
                     "[AnimFsmFileIO] 读 '%s' 失败 (code=%u)\n",
                     path.c_str(),
                     static_cast<unsigned>(rr.Error().code));
        return std::nullopt;
    }
    const JsonReader& reader = rr.Value();

    auto sv = reader.ReadSchemaVersion("schemaVersion");
    if (sv.IsErr())
    {
        std::fprintf(stderr,
                     "[AnimFsmFileIO] '%s' 缺 schemaVersion 或格式错\n",
                     path.c_str());
        return std::nullopt;
    }
    if (sv.Value().Namespace() != kSchemaNamespace
        || sv.Value().Major() != kSchemaMajor)
    {
        std::fprintf(stderr,
                     "[AnimFsmFileIO] '%s' schema namespace/major 不匹配 "
                     "(got %s v%u.x，期望 %s v%d.x)\n",
                     path.c_str(),
                     sv.Value().Namespace().c_str(),
                     static_cast<unsigned>(sv.Value().Major()),
                     kSchemaNamespace,
                     kSchemaMajor);
        return std::nullopt;
    }

    EditableStateMachine data;

    // initialState 段：缺字段视为空 string，跨引用合法性 reader 后置检查
    reader.ReadString("initialState", data.initialState);

    // states 段：缺字段视为空数组。同名 state → 整个文件 reject。
    const std::size_t stateCount = reader.ArraySize("states");
    data.states.reserve(stateCount);
    std::unordered_set<std::string> seenStateNames;
    for (std::size_t i = 0; i < stateCount; ++i)
    {
        const std::string base = "states/" + std::to_string(i) + "/";
        EditableState s;
        if (!reader.ReadString(base + "name", s.name) || s.name.empty())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' states[%zu].name 缺失，跳过\n",
                         path.c_str(),
                         i);
            continue;
        }
        if (!seenStateNames.insert(s.name).second)
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' states[%zu].name='%s' 与之前 "
                         "state 重名，整个文件 reject\n",
                         path.c_str(),
                         i,
                         s.name.c_str());
            return std::nullopt;
        }
        // clipName 允许空（c2 期允许只画拓扑、不绑 clip 也能保存）
        reader.ReadString(base + "clipName", s.clipName);

        // layout 数组 [x, y]；缺字段保留默认值 0,0
        float layoutBuf[2]{0.0f, 0.0f};
        reader.ReadFloatArray(base + "layout", layoutBuf, 2);
        s.layoutX = layoutBuf[0];
        s.layoutY = layoutBuf[1];

        data.states.push_back(std::move(s));
    }

    // initialState 跨引用警告（不命中仍保留原值）
    if (!data.initialState.empty()
        && seenStateNames.find(data.initialState) == seenStateNames.end())
    {
        std::fprintf(stderr,
                     "[AnimFsmFileIO] '%s' initialState='%s' 未在 states[] "
                     "内，runtime 翻译方需处理\n",
                     path.c_str(),
                     data.initialState.c_str());
    }

    // transitions 段
    const std::size_t transitionCount = reader.ArraySize("transitions");
    data.transitions.reserve(transitionCount);
    for (std::size_t i = 0; i < transitionCount; ++i)
    {
        const std::string base = "transitions/" + std::to_string(i) + "/";
        EditableTransition t;
        if (!reader.ReadString(base + "from", t.fromState)
            || t.fromState.empty())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' transitions[%zu].from 缺失，跳过\n",
                         path.c_str(),
                         i);
            continue;
        }
        if (!reader.ReadString(base + "to", t.toState) || t.toState.empty())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' transitions[%zu].to 缺失，跳过\n",
                         path.c_str(),
                         i);
            continue;
        }
        // 跨引用检查：端点必须在 states[] 内
        if (seenStateNames.find(t.fromState) == seenStateNames.end())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' transitions[%zu].from='%s' "
                         "未在 states[] 内，跳过\n",
                         path.c_str(),
                         i,
                         t.fromState.c_str());
            continue;
        }
        if (seenStateNames.find(t.toState) == seenStateNames.end())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' transitions[%zu].to='%s' "
                         "未在 states[] 内，跳过\n",
                         path.c_str(),
                         i,
                         t.toState.c_str());
            continue;
        }
        data.transitions.push_back(std::move(t));
    }

    return data;
}

bool WriteAnimFsmFile(const std::string& path, const EditableStateMachine& data)
{
    static const SchemaVersion kSchema{kSchemaNamespace, kSchemaMajor, kSchemaMinor};

    JsonWriter writer;
    writer.WriteSchemaVersion("schemaVersion", kSchema);
    writer.WriteString("initialState", data.initialState);

    writer.BeginArray("states", data.states.size());
    for (std::size_t i = 0; i < data.states.size(); ++i)
    {
        const auto&       s    = data.states[i];
        const std::string base = "states/" + std::to_string(i) + "/";
        writer.WriteString(base + "name", s.name);
        writer.WriteString(base + "clipName", s.clipName);
        const float layoutBuf[2] = {s.layoutX, s.layoutY};
        writer.WriteFloatArray(base + "layout", layoutBuf, 2);
    }

    writer.BeginArray("transitions", data.transitions.size());
    for (std::size_t i = 0; i < data.transitions.size(); ++i)
    {
        const auto&       t    = data.transitions[i];
        const std::string base = "transitions/" + std::to_string(i) + "/";
        writer.WriteString(base + "from", t.fromState);
        writer.WriteString(base + "to", t.toState);
    }

    auto sv = writer.SaveToFile(path, 2);
    if (sv.IsErr())
    {
        std::fprintf(stderr,
                     "[AnimFsmFileIO] 写 '%s' 失败 (code=%u)\n",
                     path.c_str(),
                     static_cast<unsigned>(sv.Error()));
        return false;
    }
    return true;
}

}  // namespace Orange::Editor::AnimFsm
