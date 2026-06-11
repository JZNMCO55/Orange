#!/usr/bin/env python3
# 生成 FbxSceneImportTest 用的"带相机"cube_with_camera.fbx fixture（自有几何，
# 可自由分发）。验 FbxSceneImporter 对 FBX camera node 的导入（→ Render::Camera）。
#
# 场景（Blender Z-up）：
#   RefCube（边长 1 cube，世界原点，红材质）—— 参照 mesh node，验相机与 mesh 共存
#   Cam（相机）—— 世界 location (0, 0, 5)，无旋转（Blender 相机默认看本地 -Z，
#       即从 +Z 俯看原点）。焦距 35mm + sensor_width 36mm（→ 已知 FOV）；
#       clip_start 0.1 / clip_end 100（→ near/far）。
#
# 为什么这样设计便于验：
#   * 相机投影：focal 35mm + sensor 36mm → 水平 FOV = 2*atan(36/(2*35)) ≈ 54.4°；
#     垂直 FOV 由 sensor_fit + aspect 决定。near/far = 0.1/100。importer 算出的
#     Render::Camera.projection 应与这些参数一致（具体公式 + 单位经探测日志实测后锁定）。
#   * 相机朝向：Blender 相机看本地 -Z；FBX 相机约定看本地 +X。Blender 导出器会烘进
#     node rotation 做桥接。经 Z-up→Y-up 换轴 (x,y,z)→(x,z,-y) 后，importer 需让
#     引擎相机的 forward（gizmo 取 rotation*(0,0,-1)，引擎/glTF 约定 -Z）落到正确世界
#     方向。Blender 相机在 (0,0,5) 看 -Z（俯看原点）→ 换轴后引擎相机在 (0,5,0) 看
#     -Y（俯看原点）。具体桥接旋转经探测日志实测后锁定。
#
# 导出轴 / 单位：与 gen_cube_hierarchy_fbx.py 一致（Z-up + FBX_SCALE_ALL +
# bake_space_transform，UnitScaleFactor=100 一致型文件）。object_types 含 CAMERA。
#
# 用法（blender.exe 不在 PATH，见 memory：D:\Software\Blender）：
#   D:\Software\Blender\blender.exe --background --python gen_camera_fbx.py -- --out <path>
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
    bpy.ops.wm.read_factory_settings(use_empty=True)


def make_principled(name, rgb):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (rgb[0], rgb[1], rgb[2], 1.0)
    mat.diffuse_color = (rgb[0], rgb[1], rgb[2], 1.0)
    return mat


def build_scene():
    # 参照 cube：原点，红材质。
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.0))
    cube = bpy.context.active_object
    cube.name = "RefCube"
    cube.data.materials.append(make_principled("RefCubeMat", (0.8, 0.1, 0.1)))

    # 相机：(0,0,5)，无旋转（看本地 -Z = 俯看原点）。已知焦距 / sensor / clip。
    bpy.ops.object.camera_add(location=(0.0, 0.0, 5.0), rotation=(0.0, 0.0, 0.0))
    cam = bpy.context.active_object
    cam.name = "Cam"
    cam.data.lens = 35.0                # 焦距 35mm
    cam.data.sensor_width = 36.0        # sensor 宽 36mm
    cam.data.sensor_height = 24.0       # sensor 高 24mm
    cam.data.sensor_fit = 'HORIZONTAL'  # 水平拟合（明确 aperture mode）
    cam.data.clip_start = 0.1
    cam.data.clip_end = 100.0
    return cube, cam


def main():
    args = parse_args()
    reset_scene()
    build_scene()

    # 与 gen_cube_hierarchy_fbx.py 同款"一致型"导出（UnitScaleFactor=100 + 轴/单位
    # 烘进数据），object_types 加 CAMERA。
    bpy.ops.export_scene.fbx(
        filepath=args.out,
        use_selection=False,
        apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_ALL',
        global_scale=1.0,
        axis_forward='Y',
        axis_up='Z',
        bake_space_transform=True,
        object_types={'MESH', 'CAMERA'},
        use_mesh_modifiers=True,
        mesh_smooth_type='FACE',
        bake_anim=False,
        path_mode='COPY',
        embed_textures=False,
    )
    sys.stderr.write("wrote FBX to %s\n" % args.out)


if __name__ == "__main__":
    main()
