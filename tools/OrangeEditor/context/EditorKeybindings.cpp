#include "EditorKeybindings.h"

#include <orange/engine/core/Serialization.h>

#include <cstdint>
#include <string>

using Orange::Engine::JsonReader;
using Orange::Engine::JsonWriter;

namespace
{

// 读 int → ImGuiKey 并 clamp 到合法范围（ImGuiKey_None .. ImGuiKey_COUNT）。
void ReadKey(const JsonReader& in, std::string_view path, ImGuiKey& out)
{
    std::int64_t v = static_cast<std::int64_t>(out);
    if (in.ReadInt(path, v))
    {
        if (v < ImGuiKey_None || v >= ImGuiKey_NamedKey_END)
        {
            return;  // 越界保留默认
        }
        out = static_cast<ImGuiKey>(v);
    }
}

void WriteKey(JsonWriter& out, std::string_view path, ImGuiKey key)
{
    out.WriteInt(path, static_cast<std::int64_t>(key));
}

}  // anonymous namespace

void ReadEditorKeybindings(const JsonReader& in, EditorKeybindings& out)
{
    ReadKey(in, "keybindings/gizmoTranslate", out.gizmoTranslate);
    ReadKey(in, "keybindings/gizmoRotate",    out.gizmoRotate);
    ReadKey(in, "keybindings/gizmoScale",     out.gizmoScale);
    ReadKey(in, "keybindings/renameEntity",   out.renameEntity);
    ReadKey(in, "keybindings/deleteEntity",   out.deleteEntity);
}

void WriteEditorKeybindings(JsonWriter& out, const EditorKeybindings& kb)
{
    WriteKey(out, "keybindings/gizmoTranslate", kb.gizmoTranslate);
    WriteKey(out, "keybindings/gizmoRotate",    kb.gizmoRotate);
    WriteKey(out, "keybindings/gizmoScale",     kb.gizmoScale);
    WriteKey(out, "keybindings/renameEntity",   kb.renameEntity);
    WriteKey(out, "keybindings/deleteEntity",   kb.deleteEntity);
}

const char* KeyNameFromImGuiKey(ImGuiKey key)
{
    return ImGui::GetKeyName(key);
}
