#include "FbxAxisConverter.h"

// OpenFBX vendor 头 —— 仅取声明（ofbx.cpp / libdeflate.c 作为独立 TU 编译）。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4244)  // narrowing
#  pragma warning(disable: 4267)  // size_t → smaller int
#endif
#include "ofbx.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

namespace Orange::Editor::Import
{

AxisConverter MakeAxisConverter(const ofbx::GlobalSettings* settings)
{
    AxisConverter conv;
    if (settings == nullptr)
    {
        return conv;  // 缺设置：恒等（Y-up + 米），保守不动几何
    }

    // 单位：MVP 信任已烘单位（unitScale=1）。不按 UnitScaleFactor/100 折算 ——
    // 那会把 Blender 烘好的米几何缩成 1/100。
    conv.unitScale = 1.0f;

    // up-axis：仅区分 Z-up vs Y-up（FrontAxis 的细分朝向 MVP 不处理 —— 绝大多数
    // DCC 导出落在标准 Z-up/-Y-front 或 Y-up/-Z-front 两套）。
    //   Z-up（FBX 默认）→ 引擎 Y-up：(x,y,z) → (x, z, -y)
    //   Y-up（Maya）    → 引擎 Y-up：恒等
    const bool zUp = (settings->UpAxis == ofbx::CoordinateAxis::POSITIVE_Z ||
                      settings->UpAxis == ofbx::CoordinateAxis::NEGATIVE_Z);
    if (zUp)
    {
        // out.x = in.x ; out.y = in.z ; out.z = -in.y
        conv.rowX[0] = 1.0f; conv.rowX[1] = 0.0f; conv.rowX[2] = 0.0f;
        conv.rowY[0] = 0.0f; conv.rowY[1] = 0.0f; conv.rowY[2] = 1.0f;
        conv.rowZ[0] = 0.0f; conv.rowZ[1] = -1.0f; conv.rowZ[2] = 0.0f;
    }
    return conv;
}

}  // namespace Orange::Editor::Import
