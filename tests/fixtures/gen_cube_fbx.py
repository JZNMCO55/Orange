#!/usr/bin/env python3
# 生成 FbxImportTest 用的已知几何 cube.fbx fixture（自有几何，可自由分发）。
#
# 几何：单位 cube（边长 1，中心原点，8 顶点 / 12 三角面）。两个材质：
#   * MatRed   —— diffuse (0.8, 0.1, 0.1) 红，贴 +X/-X/+Z/-Z 四个侧面
#   * MatGreen —— diffuse (0.1, 0.7, 0.2) 绿，贴 +Y/-Y 顶/底两面
# 便于 FbxImportTest 断言：顶点数 / 索引数 / AABB（验轴 + 单位缩放）/ 材质数 /
# baseColor 值 / sub-mesh slot（多材质拆段）。
#
# 导出轴：Blender 默认场景是 Z-up，FBX 导出器 axis_up='Z' 导出 Z-up FBX —— 这正是
# FBX 头号坑（DCC 常 Z-up + cm）。FbxImporter 端按 GlobalSettings.UpAxis 转引擎 Y-up。
# 故本 fixture 故意导出 Z-up，端到端验证 importer 的轴转换正确（cube +Z 面在引擎里
# 应落 +Y）。单位用米（apply_unit_scale=True，Blender 场景单位米 → FBX cm 数值，
# UnitScaleFactor=100，importer /100 转回米）。
#
# 用法（blender.exe 不在 PATH，见 memory：D:\Software\Blender）：
#   D:\Software\Blender\blender.exe --background --python gen_cube_fbx.py -- --out <path>
#
# blender 跑不通时：本脚本是 fixture 的唯一可信生成入口，**不要**手编二进制 FBX。

import argparse
import sys

import bpy  # noqa: E402（仅在 Blender 内可用）


def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="输出 .fbx 路径")
    return ap.parse_args(argv)


def reset_scene():
    # 清空默认场景（删 default cube / light / camera），从干净状态建几何。
    bpy.ops.wm.read_factory_settings(use_empty=True)


def make_cube():
    # 单位 cube：bpy 的 primitive_cube_add size=1 → 边长 1（-0.5..0.5）。
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.0))
    obj = bpy.context.active_object
    obj.name = "UnitCube"
    mesh = obj.data

    # 两个材质：红 + 绿。Blender FBX 导出器从 **Principled BSDF 的 Base Color**
    # 取 FBX diffuse（不是 legacy material.diffuse_color 视口色）。故必须用
    # use_nodes=True + 设 Principled BSDF Base Color，否则导出落默认灰 0.8。
    def make_principled(name, rgb):
        mat = bpy.data.materials.new(name=name)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf is not None:
            bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
        # legacy 视口色同步设一份（无害，部分导出路径也会参考）。
        mat.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
        return mat

    mat_red = make_principled("MatRed", (0.8, 0.1, 0.1))
    mat_green = make_principled("MatGreen", (0.1, 0.7, 0.2))
    mesh.materials.append(mat_red)    # slot 0
    mesh.materials.append(mat_green)  # slot 1

    # 给每个面分配材质：法线 Z 分量最大的两个面（顶/底，Blender Z-up）给绿，
    # 其余四个侧面给红。primitive_cube_add 出来是 6 个 quad 面。
    mesh.calc_loop_triangles()
    for poly in mesh.polygons:
        nz = poly.normal.z
        if abs(nz) > 0.5:
            poly.material_index = 1  # 绿（顶/底）
        else:
            poly.material_index = 0  # 红（侧面）

    return obj


def main():
    args = parse_args()
    reset_scene()
    make_cube()

    # FBX 导出：Z-up（Blender 默认，故意保留以验 importer 轴转换）、米单位
    # （apply_unit_scale → FBX UnitScaleFactor=100）。只导 mesh（无相机 / 灯光 /
    # 动画 / 骨骼）。use_mesh_modifiers=True 让材质 / 法线烘出。
    bpy.ops.export_scene.fbx(
        filepath=args.out,
        use_selection=False,
        apply_unit_scale=True,
        global_scale=1.0,
        axis_forward='-Z',
        axis_up='Z',
        object_types={'MESH'},
        use_mesh_modifiers=True,
        mesh_smooth_type='FACE',
        bake_anim=False,
        path_mode='COPY',
        embed_textures=False,
    )
    sys.stderr.write("wrote FBX to %s\n" % args.out)


if __name__ == "__main__":
    main()
