# Blender headless 脚本：生成一个"transform 层级 + 灯光"的小场景并导出 .glb。
# 用于 dogfood GAP-2026-05-28 G1+G3（glTF scene-level 导入）：
#   * 层级（G1）—— 一个父 Empty(PropGroup) 下挂两个 parented mesh prop
#     (CrateProp / BarrelProp)，导入后应在引擎里保留父子结构 + 各 prop 在
#     DCC 世界位置（world-baked，见 GltfSceneImporter）。
#   * 灯光（G3）—— 一盏 Sun(directional) + 一盏 Point light，导入后应成
#     DirectionalLight / PointLight，方向 / 颜色对（intensity 单位未映射、待手调）。
#   * 真实 Blender 导出路径 —— Z-up→Y-up 坐标转换 + 真实 node matrix +
#     KHR_lights_punctual，合成单测覆盖不到，靠本 fixture 真机验证。
#
# 运行（headless）：
#   D:/Software/Blender/blender.exe --background --python dogfood_make_scene_hierarchy_lights.py
# 输出 .glb 到本脚本同目录（OUT_GLB 环境变量可覆盖）。
# 再 headless 导入：build/bin/Debug/OrangeEditor.exe import-scene scripts/scene_hierarchy_lights.glb
# 然后 GUI File→Open assets/scenes/scene_hierarchy_lights.scene.json 看层级 + 摆位 + 灯光。
import bpy
import os


def main():
    # 干净空场景（避免默认 cube/灯/相机残留）。
    bpy.ops.wm.read_factory_settings(use_empty=True)

    # 父节点：Empty(PropGroup)，放在世界 (2,0,0)。
    bpy.ops.object.empty_add(location=(2.0, 0.0, 0.0))
    group = bpy.context.active_object
    group.name = "PropGroup"

    # 子 1：cube prop，世界 (2,1,0)，parent 到 group 且保持世界位姿。
    bpy.ops.mesh.primitive_cube_add(size=0.6, location=(2.0, 1.0, 0.0))
    crate = bpy.context.active_object
    crate.name = "CrateProp"
    crate.parent = group
    crate.matrix_parent_inverse = group.matrix_world.inverted()

    # 子 2：sphere prop，世界 (2,-1,0.5)，parent 到 group。
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.4, location=(2.0, -1.0, 0.5))
    barrel = bpy.context.active_object
    barrel.name = "BarrelProp"
    barrel.parent = group
    barrel.matrix_parent_inverse = group.matrix_world.inverted()

    # Sun（directional）—— 颜色暖白，朝下偏一点（旋转 X 让它不是纯垂直）。
    bpy.ops.object.light_add(type="SUN", location=(0.0, 0.0, 5.0))
    sun = bpy.context.active_object
    sun.name = "SunLight"
    sun.data.energy = 3.0
    sun.data.color = (1.0, 0.95, 0.85)
    sun.rotation_euler = (0.6, 0.0, 0.3)

    # Point light —— 蓝色，放在 prop 群上方。
    bpy.ops.object.light_add(type="POINT", location=(3.0, 2.0, 3.0))
    lamp = bpy.context.active_object
    lamp.name = "LampLight"
    lamp.data.energy = 100.0
    lamp.data.color = (0.4, 0.6, 1.0)

    out = os.environ.get(
        "OUT_GLB",
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "scene_hierarchy_lights.glb"))
    # export_lights 让 KHR_lights_punctual 写进 .glb（不同 Blender 版本参数名
    # 都是 export_lights；缺省可能不导灯光）。
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB",
                              export_lights=True)
    print("EXPORTED_GLB:", out)


if __name__ == "__main__":
    main()
