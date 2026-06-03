#!/usr/bin/env python3
# 生成 FbxSceneImportTest 用的"有层级"cube_hierarchy.fbx fixture（自有几何，
# 可自由分发）。区别于 gen_cube_fbx.py（单 mesh 无层级），本 fixture 有父子两个
# node，child 带**非平凡 local transform**（偏移 + 旋转），用来验 importer 对
# node local transform 的 R·M·R⁻¹ 共轭轴转换（FBX 层级导入头号坑）做对了没。
#
# 层级（Blender Z-up）：
#   Parent（cube）—— location (2, 0, 0)，无旋转。
#     └─ Child（cube）—— **parent 到 Parent**，相对父的 local：
#          location (0, 3, 0)（沿 Blender +Y）+ rotation 90° 绕 Blender 局部 X 轴。
#
# 为什么这样设计便于验共轭：
#   * child 的 local 平移沿 Blender +Y。Z-up→Y-up 换轴 (x,y,z)→(x,z,-y) 把
#     Blender (0,3,0) 映到引擎 (0,0,-3)。importer 对 node local matrix 做共轭后
#     decompose 出的 position 必须 ≈ (0,0,-3)——这验"平移被 R 正确旋转"。
#   * child 的 local 旋转是绕 Blender 局部 X 90°。共轭 R·M·R⁻¹ 后，旋转轴本身
#     也被换轴：Blender X 轴在引擎里仍是 X 轴（(x,y,z)→(x,z,-y) 下 x 不变），故
#     引擎 local rotation 应是绕引擎 X 轴 90°（同轴同角，因为 X 是换轴不动轴）。
#     若 importer 误用 R·M（不共轭），旋转轴 / 方向会歪——本 fixture 锁住它。
#   * 父子相对位姿（child 在 parent 局部系下的摆位）经引擎累积父变换后，应与
#     Blender 里看到的世界摆位一致（端到端验证）。
#
# 导出轴：Blender 默认 Z-up，axis_up='Z' 导出 Z-up FBX（故意，验轴转换）。
# 单位米（apply_unit_scale=True）。只导 mesh node（含 empty 会变 NULL_NODE，本
# fixture 用 cube 当父更直观）。
#
# 用法（blender.exe 不在 PATH，见 memory：D:\Software\Blender）：
#   D:\Software\Blender\blender.exe --background --python gen_cube_hierarchy_fbx.py -- --out <path>
#
# blender 跑不通时：本脚本是 fixture 的唯一可信生成入口，**不要**手编二进制 FBX。

import argparse
import math
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
    bpy.ops.wm.read_factory_settings(use_empty=True)


def make_principled(name, rgb):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    mat.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
    return mat


def add_cube(name, size, rgb):
    bpy.ops.mesh.primitive_cube_add(size=size, location=(0.0, 0.0, 0.0))
    obj = bpy.context.active_object
    obj.name = name
    obj.data.materials.append(make_principled(name + "Mat", rgb))
    return obj


def build_hierarchy():
    # Parent：边长 1 cube，世界 location (2,0,0)（Blender Z-up），红材质。
    parent = add_cube("Parent", 1.0, (0.8, 0.1, 0.1))
    parent.location = (2.0, 0.0, 0.0)
    parent.rotation_euler = (0.0, 0.0, 0.0)

    # Child：边长 0.5 cube，绿材质。先建后挂父，并设**相对父的 local** transform：
    #   local location (0, 3, 0)（Blender +Y）+ rotation 90° 绕 Blender 局部 X。
    child = add_cube("Child", 0.5, (0.1, 0.7, 0.2))

    # parent_set 后 Blender 用 matrix_parent_inverse 保持世界位姿；我们要的是
    # **干净的 local transform**（local = 直接相对父），故清掉 parent_inverse 再设
    # local TRS。这样导出的 FBX node 的 Lcl Translation/Rotation 就是我们设的值。
    child.parent = parent
    child.matrix_parent_inverse.identity()
    child.location = (0.0, 3.0, 0.0)          # 相对父的 local 平移（Blender +Y）
    child.rotation_euler = (math.radians(90.0), 0.0, 0.0)  # 绕 Blender 局部 X 90°
    child.scale = (1.0, 1.0, 1.0)

    return parent, child


def main():
    args = parse_args()
    reset_scene()
    build_hierarchy()

    # FBX 导出：Z-up（故意，验 importer 共轭轴转换）、米单位。只导 mesh node，
    # 保留父子层级（bake_anim=False，无动画 / 骨骼）。
    # 标准 Blender Z-up 导出：axis_forward='-Y' + axis_up='Z'（front/up 不可同轴；
    # 旧 gen_cube_fbx.py 用了 -Z/Z 这一非法组合，导致 node 与 vertex 落进不同的
    # 轴基，scene-level node transform 换轴会错。这里用合法的 -Y/Z 让 vertex 与
    # node 共用一致的 Z-up 基，importer 的 (x,y,z)→(x,z,-y) 共轭对二者同款成立）。
    bpy.ops.export_scene.fbx(
        filepath=args.out,
        use_selection=False,
        apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_ALL',  # 把单位缩放烘进几何，UnitScaleFactor=1
        global_scale=1.0,
        axis_forward='Y',
        axis_up='Z',
        bake_space_transform=True,  # 把轴/单位转换烘进数据，避免 node 上残留
                                    # front-axis 旋转 + X 翻转（CoordAxis 之坑）。
                                    # forward='Y'（非 -Y）避免整场 180°-Z 翻转，
                                    # Parent (2,0,0) 在引擎里仍落 (2,0,0)。
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
