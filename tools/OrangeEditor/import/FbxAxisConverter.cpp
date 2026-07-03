#include "FbxAxisConverter.h"

#include <orange/engine/core/Log.h>

// OpenFBX vendor 头 —— 仅取声明（ofbx.cpp / libdeflate.c 作为独立 TU 编译）。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244) // narrowing
#pragma warning(disable : 4267) // size_t → smaller int
#endif
#include "ofbx.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace Orange::Editor::Import
{

    AxisConverter MakeAxisConverter(const ofbx::GlobalSettings* settings,
                                    float                       importScale)
    {
        AxisConverter conv;
        conv.unitScale = importScale; // 调用方/CLI 决定（默认 1.0 = 信任已烘米）
        if (settings == nullptr)
        {
            return conv; // 缺设置：恒等旋转 + importScale，保守不动几何朝向
        }

        // 单位：不自动按 UnitScaleFactor 折算 —— USF 是经典歧义（USF=1 的文件顶点既
        // 可能是米〔Blender 默认〕也可能是 cm〔Maya/Max〕，单凭它无法区分；盲目 /100
        // 会把 Blender 烘好的米缩成 1/100）。改由 importScale 显式控制（见头注释），
        // 这里只把文件的 UnitScaleFactor 打进日志，供调用方判断真 cm 文件该传多少。
        ORANGE_LOG_INFO(
            "FbxAxisConverter: 文件 UnitScaleFactor={} (cm/unit)，applying importScale={} "
            "(1.0=信任已烘米；真 cm 文件传约 0.01 折算到米)",
            settings->UnitScaleFactor, importScale);

        // up-axis：仅区分 Z-up vs Y-up（FrontAxis 的细分朝向 MVP 不处理 —— 绝大多数
        // DCC 导出落在标准 Z-up/-Y-front 或 Y-up/-Z-front 两套）。
        //   Z-up（FBX 默认）→ 引擎 Y-up：(x,y,z) → (x, z, -y)
        //   Y-up（Maya）    → 引擎 Y-up：恒等
        const bool zUp = (settings->UpAxis == ofbx::CoordinateAxis::POSITIVE_Z ||
                          settings->UpAxis == ofbx::CoordinateAxis::NEGATIVE_Z);
        if (zUp)
        {
            // out.x = in.x ; out.y = in.z ; out.z = -in.y
            conv.rowX[0] = 1.0f;
            conv.rowX[1] = 0.0f;
            conv.rowX[2] = 0.0f;
            conv.rowY[0] = 0.0f;
            conv.rowY[1] = 0.0f;
            conv.rowY[2] = 1.0f;
            conv.rowZ[0] = 0.0f;
            conv.rowZ[1] = -1.0f;
            conv.rowZ[2] = 0.0f;
        }
        return conv;
    }

} // namespace Orange::Editor::Import
