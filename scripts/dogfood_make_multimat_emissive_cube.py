# Blender headless 脚本：生成一个"多 material slot + emissive"立方体并导出 .glb。
# 用于 dogfood：
#   * 多 material per mesh（Task 1 / GAP-2026-05-30）—— cube 一半面用 OrangeMat、
#     另一半用 GlowMat，导入后应拆 2 个 sub-mesh slot + 2 个 .material。
#   * PBR emissive 通道（Task 3 / GAP-2026-05-25 gap ①）—— GlowMat 是自发光蓝光，
#     emissiveStrength=3 触发 KHR_materials_emissive_strength，应进 uEmissive，
#     viewport 里那半面应发蓝光（经 bloom）。
#
# 运行（headless）：
#   blender.exe --background --python make_multimat_emissive_cube.py
# 输出 .glb 到本脚本同目录（OUT_GLB 环境变量可覆盖）。
import bpy
import os


def set_emission(bsdf, color_rgba, strength):
    """跨 Blender 版本设自发光：4.x/5.x 是 'Emission Color' + 'Emission Strength'，
    3.x 是 'Emission'（色）+ 'Emission Strength'。两个名都试。"""
    col = bsdf.inputs.get("Emission Color") or bsdf.inputs.get("Emission")
    if col is not None:
        col.default_value = color_rgba
    s = bsdf.inputs.get("Emission Strength")
    if s is not None:
        s.default_value = strength


def main():
    # 干净空场景（避免默认 cube/灯/相机残留干扰导出）。
    bpy.ops.wm.read_factory_settings(use_empty=True)

    bpy.ops.mesh.primitive_cube_add(size=1.0)
    obj = bpy.context.active_object
    obj.name = "MultiMatEmissiveCube"
    mesh = obj.data

    # slot 0：橙色不发光（普通 PBR）。
    mat_a = bpy.data.materials.new("OrangeMat")
    mat_a.use_nodes = True
    bsdf_a = mat_a.node_tree.nodes.get("Principled BSDF")
    bsdf_a.inputs["Base Color"].default_value = (1.0, 0.45, 0.10, 1.0)
    bsdf_a.inputs["Roughness"].default_value = 0.5
    bsdf_a.inputs["Metallic"].default_value = 0.0

    # slot 1：蓝色自发光（emissiveStrength=3 → HDR glow，应触发 KHR ext）。
    mat_b = bpy.data.materials.new("GlowMat")
    mat_b.use_nodes = True
    bsdf_b = mat_b.node_tree.nodes.get("Principled BSDF")
    bsdf_b.inputs["Base Color"].default_value = (0.05, 0.10, 0.20, 1.0)
    bsdf_b.inputs["Roughness"].default_value = 0.4
    set_emission(bsdf_b, (0.10, 0.60, 1.00, 1.0), 3.0)

    mesh.materials.append(mat_a)  # slot 0
    mesh.materials.append(mat_b)  # slot 1

    # 前 3 个面给 slot 0，后 3 个面给 slot 1（cube 6 面，按面分两组）。
    for poly in mesh.polygons:
        poly.material_index = 0 if poly.index < 3 else 1

    out = os.environ.get(
        "OUT_GLB",
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     "multimat_emissive_cube.glb"))
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB")
    print("EXPORTED_GLB:", out)


if __name__ == "__main__":
    main()
