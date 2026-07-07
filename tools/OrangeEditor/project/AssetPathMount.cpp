// AssetPathMount 实现 —— 纯字符串前缀操作（M6 路径虚拟化）。cwd == projectRoot 前提下
// 无需任何绝对路径解析。见 AssetPathMount.h 的 scheme 契约。

#include "project/AssetPathMount.h"

namespace Orange::Editor::Project
{
    namespace
    {

        constexpr std::string_view kProjectScheme = "project://";
        constexpr std::string_view kEngineScheme  = "engine://";
        constexpr std::string_view kSchemeSep     = "://";

        // str 是否以 prefix 开头。
        bool StartsWith(std::string_view str, std::string_view prefix)
        {
            return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
        }

    } // namespace

    std::string ResolveVirtualPath(std::string_view stored, std::string_view engineResourceRoot)
    {
        if (StartsWith(stored, kEngineScheme))
        {
            const std::string_view rest = stored.substr(kEngineScheme.size());
            if (engineResourceRoot.empty())
            {
                // root 未知 → 退化为相对 rest（cwd 相对），不凭空拼绝对路径。
                return std::string(rest);
            }
            std::string out;
            out.reserve(engineResourceRoot.size() + 1 + rest.size());
            out.append(engineResourceRoot);
            out.push_back('/');
            out.append(rest);
            return out;
        }
        if (StartsWith(stored, kProjectScheme))
        {
            return std::string(stored.substr(kProjectScheme.size()));
        }
        // plain / 空串 / 未识别的其它 scheme：原样返回（旧 1.19 plain 路径经此恒等透传）。
        return std::string(stored);
    }

    std::string VirtualizeProjectPath(std::string_view real)
    {
        // 空串不虚拟化（"未设资产"，别变成 "project://"）；已带 scheme 的（含 project:// /
        // engine:// 及任何 ://）原样返回，避免二次包装。
        if (real.empty() || real.find(kSchemeSep) != std::string_view::npos)
        {
            return std::string(real);
        }
        std::string out;
        out.reserve(kProjectScheme.size() + real.size());
        out.append(kProjectScheme);
        out.append(real);
        return out;
    }

} // namespace Orange::Editor::Project
