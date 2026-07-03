// BuiltinShadowShaders 实现：与 BuiltinMaterials.cpp 同思路（.exe-相对
// 路径解析 + AssetRegistry::Load<ShaderAsset> dedup）。当前 helper 在
// 两处复制（BuiltinMaterials.cpp + BuiltinShadowShaders.cpp）；等到
// 3+ 处复用再提到 Platform 模块统一。

#include "orange/engine/render/BuiltinShadowShaders.h"

#include "orange/engine/asset/AssetRegistry.h"
#include "orange/engine/asset/ShaderAsset.h"
#include "orange/engine/core/Log.h"

#include <filesystem>
#include <string>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace Orange::Engine::Render::BuiltinShadowShaders
{
    namespace
    {

        std::filesystem::path GetExecutableDir()
        {
#if defined(_WIN32)
            wchar_t     buffer[MAX_PATH];
            const DWORD len = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (len == 0 || len == MAX_PATH)
            {
                return std::filesystem::current_path();
            }
            return std::filesystem::path(std::wstring(buffer, len)).parent_path();
#else
            return std::filesystem::current_path();
#endif
        }

        std::string ResolveBuiltinShaderPath(const char* relative)
        {
            return (GetExecutableDir() / relative).string();
        }

    } // namespace

    ShaderPair LoadShadowCaster(Asset::AssetRegistry& registry)
    {
        const auto vertPath = ResolveBuiltinShaderPath("shaders/orange_engine/shadow_caster.vert.spv");
        const auto fragPath = ResolveBuiltinShaderPath("shaders/orange_engine/shadow_caster.frag.spv");

        ShaderPair pair;

        auto vsResult = registry.Load<Asset::ShaderAsset>(vertPath);
        if (vsResult.IsErr())
        {
            ORANGE_LOG_ERROR("BuiltinShadowShaders: 加载 shadow_caster vertex SPIR-V 失败 (path={}, code={})",
                             vertPath, static_cast<unsigned>(vsResult.Error()));
        }
        else
        {
            pair.vertex = vsResult.Value();
        }

        auto fsResult = registry.Load<Asset::ShaderAsset>(fragPath);
        if (fsResult.IsErr())
        {
            ORANGE_LOG_ERROR("BuiltinShadowShaders: 加载 shadow_caster fragment SPIR-V 失败 (path={}, code={})",
                             fragPath, static_cast<unsigned>(fsResult.Error()));
        }
        else
        {
            pair.fragment = fsResult.Value();
        }

        return pair;
    }

} // namespace Orange::Engine::Render::BuiltinShadowShaders
