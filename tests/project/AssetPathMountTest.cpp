// AssetPathMount —— 资产虚拟路径 scheme 的解析 / 虚拟化 headless 单元测试（M6）。
//
// 锁住的不变量：
//   (a) project:// 往返恒等：VirtualizeProjectPath(X) → project://X → ResolveVirtualPath → X；
//   (b) engine:// 解析：engine://X + root → root/X；root 空 → X；
//   (c) plain 透传：无 scheme 的路径解析 / 虚拟化的往返恒等（= 旧 1.19 文件向后兼容核心）；
//   (d) 空串透传：两向都不动空串（"未设资产" 不该变成 project://）；
//   (e) 已带 scheme 的路径再虚拟化是幂等（不二次包装）。
//
// 纯 Core 无关逻辑（只依赖标准库 + AssetPathMount）：AssetPathMount.cpp 直接编进本测试
// exe，只链 orange_engine（同 project_file_round_trip_test 模式）。

#include "project/AssetPathMount.h"

#include <cassert>
#include <cstdio>
#include <string>

using Orange::Editor::Project::ResolveVirtualPath;
using Orange::Editor::Project::VirtualizeProjectPath;

namespace
{

    // (a) project:// 往返恒等。
    void TestProjectRoundTrip()
    {
        const std::string real = "assets/x.mesh";
        const std::string virt = VirtualizeProjectPath(real);
        assert(virt == "project://assets/x.mesh");
        // engineResourceRoot 对 project:// 无意义（传空即可）。
        assert(ResolveVirtualPath(virt, "") == real);
        // 往返恒等（多种项目相对路径）。
        for (const std::string& p : {std::string("assets/scenes/a.scene.json"),
                                     std::string("meshes/square.mesh"),
                                     std::string("builtin/toon")})
        {
            assert(ResolveVirtualPath(VirtualizeProjectPath(p), "") == p);
        }
        std::printf("  [ok] project:// 往返恒等\n");
    }

    // (b) engine:// 解析：root 非空 → root/X；root 空 → X。
    void TestEngineResolution()
    {
        assert(ResolveVirtualPath("engine://shaders/blit.frag", "D:/sdk/orange-engine/resources") ==
               "D:/sdk/orange-engine/resources/shaders/blit.frag");
        // root 空 → 退化为相对 rest（不凭空拼绝对路径）。
        assert(ResolveVirtualPath("engine://icons/play.png", "") == "icons/play.png");
        std::printf("  [ok] engine:// 解析（root 拼接 / root 空退化）\n");
    }

    // (c) plain 透传（无 scheme）—— 旧 1.19 文件的相对路径经 resolver 恒等，向后兼容核心。
    void TestPlainPassthrough()
    {
        assert(ResolveVirtualPath("assets/x.mesh", "") == "assets/x.mesh");
        assert(ResolveVirtualPath("assets/x.mesh", "D:/engine/res") == "assets/x.mesh");
        // Windows 盘符不是 scheme（":/" 单斜杠，非 "://"）—— 不被误当虚拟路径包装。
        assert(VirtualizeProjectPath("assets/x.mesh") == "project://assets/x.mesh");
        std::printf("  [ok] plain 无 scheme 透传（1.19 向后兼容）\n");
    }

    // (d) 空串透传：两向都不动空串。
    void TestEmptyPassthrough()
    {
        assert(VirtualizeProjectPath("") == "");
        assert(ResolveVirtualPath("", "") == "");
        assert(ResolveVirtualPath("", "D:/engine/res") == "");
        std::printf("  [ok] 空串两向透传\n");
    }

    // (e) 已带 scheme 的路径再虚拟化幂等（不二次包装）。
    void TestAlreadySchemedPassthrough()
    {
        assert(VirtualizeProjectPath("project://assets/x.mesh") == "project://assets/x.mesh");
        assert(VirtualizeProjectPath("engine://shaders/blit.frag") == "engine://shaders/blit.frag");
        std::printf("  [ok] 已带 scheme 再虚拟化幂等\n");
    }

} // namespace

int main()
{
    std::printf("[asset_path_mount_test]\n");
    TestProjectRoundTrip();
    TestEngineResolution();
    TestPlainPassthrough();
    TestEmptyPassthrough();
    TestAlreadySchemedPassthrough();
    std::printf("[asset_path_mount_test] all passed\n");
    return 0;
}
