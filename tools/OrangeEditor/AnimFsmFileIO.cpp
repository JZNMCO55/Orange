// AnimFsmFileIO 实现 —— .anim_fsm 文件 v1.1 schema 读写。详见同名 .h
// 头注释（schema 演化 v1.0 → v1.1 + ParameterType / ConditionOp 字符串
// 映射 + 跨引用检查策略）。

#include "AnimFsmFileIO.h"

#include <orange/engine/core/Serialization.h>

#include <cstdio>
#include <cstring>
#include <string_view>
#include <unordered_set>

namespace Orange::Editor::AnimFsm
{
namespace
{

using ::Orange::Engine::Animation::ConditionOp;
using ::Orange::Engine::Animation::ParameterType;
using ::Orange::Engine::JsonReader;
using ::Orange::Engine::JsonWriter;
using ::Orange::Engine::ResultCode;
using ::Orange::Engine::SchemaVersion;

constexpr const char* kSchemaNamespace = "editor/anim_fsm";
constexpr int         kSchemaMajor     = 1;
constexpr int         kSchemaMinor     = 1;  // c2-7-B bump：v1.0 → v1.1

// ----- ParameterType ↔ string -----

const char* ParameterTypeToString(ParameterType t) noexcept
{
    switch (t)
    {
        case ParameterType::Bool:    return "bool";
        case ParameterType::Int:     return "int";
        case ParameterType::Float:   return "float";
        case ParameterType::Trigger: return "trigger";
    }
    return "bool";
}

std::optional<ParameterType> ParseParameterType(std::string_view s) noexcept
{
    if (s == "bool")    return ParameterType::Bool;
    if (s == "int")     return ParameterType::Int;
    if (s == "float")   return ParameterType::Float;
    if (s == "trigger") return ParameterType::Trigger;
    return std::nullopt;
}

// ----- ConditionOp ↔ string -----

const char* ConditionOpToString(ConditionOp op) noexcept
{
    switch (op)
    {
        case ConditionOp::If:           return "if";
        case ConditionOp::IfNot:        return "ifNot";
        case ConditionOp::Greater:      return "greater";
        case ConditionOp::Less:         return "less";
        case ConditionOp::Equal:        return "equal";
        case ConditionOp::NotEqual:     return "notEqual";
        case ConditionOp::GreaterEqual: return "greaterEqual";
        case ConditionOp::LessEqual:    return "lessEqual";
    }
    return "if";
}

std::optional<ConditionOp> ParseConditionOp(std::string_view s) noexcept
{
    if (s == "if")           return ConditionOp::If;
    if (s == "ifNot")        return ConditionOp::IfNot;
    if (s == "greater")      return ConditionOp::Greater;
    if (s == "less")         return ConditionOp::Less;
    if (s == "equal")        return ConditionOp::Equal;
    if (s == "notEqual")     return ConditionOp::NotEqual;
    if (s == "greaterEqual") return ConditionOp::GreaterEqual;
    if (s == "lessEqual")    return ConditionOp::LessEqual;
    return std::nullopt;
}

// ----- variant<bool, int32, float> ↔ JSON {type, value} -----

// 写 variant 到 path/.type + path/.value 两个 JSON 字段。
void WriteValueVariant(JsonWriter& w,
                       const std::string& path,
                       const std::variant<bool, std::int32_t, float>& v)
{
    if (std::holds_alternative<bool>(v))
    {
        w.WriteString(path + "/type", "bool");
        w.WriteBool(path + "/value", std::get<bool>(v));
    }
    else if (std::holds_alternative<std::int32_t>(v))
    {
        w.WriteString(path + "/type", "int");
        w.WriteInt(path + "/value", static_cast<std::int64_t>(std::get<std::int32_t>(v)));
    }
    else
    {
        w.WriteString(path + "/type", "float");
        w.WriteFloat(path + "/value", static_cast<double>(std::get<float>(v)));
    }
}

// 读 path/.type + path/.value 解析回 variant。失败返回 default（bool false）。
std::variant<bool, std::int32_t, float> ReadValueVariant(
    const JsonReader& r, const std::string& path, bool& outOk)
{
    outOk = false;
    std::string typeStr;
    if (!r.ReadString(path + "/type", typeStr))
    {
        return false;
    }
    if (typeStr == "bool")
    {
        bool v = false;
        if (!r.ReadBool(path + "/value", v))
        {
            return false;
        }
        outOk = true;
        return v;
    }
    if (typeStr == "int")
    {
        std::int64_t v = 0;
        if (!r.ReadInt(path + "/value", v))
        {
            return std::int32_t{0};
        }
        outOk = true;
        return static_cast<std::int32_t>(v);
    }
    if (typeStr == "float")
    {
        double v = 0.0;
        if (!r.ReadFloat(path + "/value", v))
        {
            return 0.0f;
        }
        outOk = true;
        return static_cast<float>(v);
    }
    return false;
}

}  // anonymous namespace

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

    reader.ReadString("initialState", data.initialState);

    // ----- parameters 段（v1.1 新增；v1.0 缺字段视为空）-----
    const std::size_t parameterCount = reader.ArraySize("parameters");
    data.parameters.reserve(parameterCount);
    std::unordered_set<std::string> seenParameterNames;
    for (std::size_t i = 0; i < parameterCount; ++i)
    {
        const std::string base = "parameters/" + std::to_string(i) + "/";
        EditableParameter p;
        if (!reader.ReadString(base + "name", p.name) || p.name.empty())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' parameters[%zu].name 缺失，跳过\n",
                         path.c_str(), i);
            continue;
        }
        if (!seenParameterNames.insert(p.name).second)
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' parameters[%zu].name='%s' "
                         "与之前 parameter 重名，跳过\n",
                         path.c_str(), i, p.name.c_str());
            continue;
        }
        std::string typeStr;
        if (!reader.ReadString(base + "type", typeStr))
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' parameters[%zu].type 缺失，跳过\n",
                         path.c_str(), i);
            continue;
        }
        auto typeOpt = ParseParameterType(typeStr);
        if (!typeOpt.has_value())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' parameters[%zu].type='%s' 未知，跳过\n",
                         path.c_str(), i, typeStr.c_str());
            continue;
        }
        p.type = *typeOpt;

        bool dvOk = false;
        p.defaultValue = ReadValueVariant(reader, base + "default", dvOk);
        // dvOk == false 时按 type 选 sane default
        if (!dvOk)
        {
            switch (p.type)
            {
                case ParameterType::Bool:
                case ParameterType::Trigger: p.defaultValue = false;        break;
                case ParameterType::Int:     p.defaultValue = std::int32_t{0}; break;
                case ParameterType::Float:   p.defaultValue = 0.0f;         break;
            }
        }

        data.parameters.push_back(std::move(p));
    }

    // ----- states 段（v1.0 既有）-----
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
                         path.c_str(), i);
            continue;
        }
        if (!seenStateNames.insert(s.name).second)
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' states[%zu].name='%s' "
                         "与之前 state 重名，整个文件 reject\n",
                         path.c_str(), i, s.name.c_str());
            return std::nullopt;
        }
        reader.ReadString(base + "clipName", s.clipName);

        float layoutBuf[2]{0.0f, 0.0f};
        reader.ReadFloatArray(base + "layout", layoutBuf, 2);
        s.layoutX = layoutBuf[0];
        s.layoutY = layoutBuf[1];

        data.states.push_back(std::move(s));
    }

    if (!data.initialState.empty()
        && seenStateNames.find(data.initialState) == seenStateNames.end())
    {
        std::fprintf(stderr,
                     "[AnimFsmFileIO] '%s' initialState='%s' 未在 states[] 内\n",
                     path.c_str(), data.initialState.c_str());
    }

    // ----- transitions 段（v1.1 扩 conditions 子字段）-----
    const std::size_t transitionCount = reader.ArraySize("transitions");
    data.transitions.reserve(transitionCount);
    for (std::size_t i = 0; i < transitionCount; ++i)
    {
        const std::string base = "transitions/" + std::to_string(i) + "/";
        EditableTransition t;
        if (!reader.ReadString(base + "from", t.fromState) || t.fromState.empty())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' transitions[%zu].from 缺失，跳过\n",
                         path.c_str(), i);
            continue;
        }
        if (!reader.ReadString(base + "to", t.toState) || t.toState.empty())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' transitions[%zu].to 缺失，跳过\n",
                         path.c_str(), i);
            continue;
        }
        if (seenStateNames.find(t.fromState) == seenStateNames.end())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' transitions[%zu].from='%s' "
                         "未在 states[] 内，跳过\n",
                         path.c_str(), i, t.fromState.c_str());
            continue;
        }
        if (seenStateNames.find(t.toState) == seenStateNames.end())
        {
            std::fprintf(stderr,
                         "[AnimFsmFileIO] '%s' transitions[%zu].to='%s' "
                         "未在 states[] 内，跳过\n",
                         path.c_str(), i, t.toState.c_str());
            continue;
        }

        // conditions 子段（v1.1 新增；v1.0 文件缺字段 → ArraySize 0 → 空 conditions）
        const std::string condBase   = base + "conditions";
        const std::size_t condCount  = reader.ArraySize(condBase);
        t.conditions.reserve(condCount);
        for (std::size_t j = 0; j < condCount; ++j)
        {
            const std::string cb = condBase + "/" + std::to_string(j) + "/";
            EditableCondition c;
            if (!reader.ReadString(cb + "paramName", c.paramName) || c.paramName.empty())
            {
                std::fprintf(stderr,
                             "[AnimFsmFileIO] '%s' transitions[%zu].conditions[%zu]"
                             ".paramName 缺失，跳过\n",
                             path.c_str(), i, j);
                continue;
            }
            if (seenParameterNames.find(c.paramName) == seenParameterNames.end())
            {
                std::fprintf(stderr,
                             "[AnimFsmFileIO] '%s' transitions[%zu].conditions[%zu]"
                             ".paramName='%s' 未在 parameters[] 内，跳过\n",
                             path.c_str(), i, j, c.paramName.c_str());
                continue;
            }
            std::string opStr;
            if (!reader.ReadString(cb + "op", opStr))
            {
                std::fprintf(stderr,
                             "[AnimFsmFileIO] '%s' transitions[%zu].conditions[%zu]"
                             ".op 缺失，跳过\n",
                             path.c_str(), i, j);
                continue;
            }
            auto opOpt = ParseConditionOp(opStr);
            if (!opOpt.has_value())
            {
                std::fprintf(stderr,
                             "[AnimFsmFileIO] '%s' transitions[%zu].conditions[%zu]"
                             ".op='%s' 未知，跳过\n",
                             path.c_str(), i, j, opStr.c_str());
                continue;
            }
            c.op = *opOpt;

            bool thOk = false;
            c.threshold = ReadValueVariant(reader, cb + "threshold", thOk);
            // thOk == false 时按 If/IfNot 不消费的语义保留默认（bool false）

            t.conditions.push_back(std::move(c));
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

    // parameters
    writer.BeginArray("parameters", data.parameters.size());
    for (std::size_t i = 0; i < data.parameters.size(); ++i)
    {
        const auto&       p    = data.parameters[i];
        const std::string base = "parameters/" + std::to_string(i) + "/";
        writer.WriteString(base + "name", p.name);
        writer.WriteString(base + "type", ParameterTypeToString(p.type));
        WriteValueVariant(writer, base + "default", p.defaultValue);
    }

    // states
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

    // transitions
    writer.BeginArray("transitions", data.transitions.size());
    for (std::size_t i = 0; i < data.transitions.size(); ++i)
    {
        const auto&       t    = data.transitions[i];
        const std::string base = "transitions/" + std::to_string(i) + "/";
        writer.WriteString(base + "from", t.fromState);
        writer.WriteString(base + "to",   t.toState);

        // conditions 子段
        const std::string condBase = base + "conditions";
        writer.BeginArray(condBase, t.conditions.size());
        for (std::size_t j = 0; j < t.conditions.size(); ++j)
        {
            const auto&       c  = t.conditions[j];
            const std::string cb = condBase + "/" + std::to_string(j) + "/";
            writer.WriteString(cb + "paramName", c.paramName);
            writer.WriteString(cb + "op", ConditionOpToString(c.op));
            WriteValueVariant(writer, cb + "threshold", c.threshold);
        }
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
