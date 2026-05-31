#!/usr/bin/env python3
# 生成一个最小内嵌 .glb fixture：单三角形 + 一张内嵌 baseColor PNG（经 GLB
# buffer_view，与 Blender 默认 .glb 导出同形态）。用于 GltfEmbeddedTextureImportTest
# 验证 importer 的内嵌贴图提取路径。
#
# 产物两种（默认两者都出）：
#   --out <path>   把 .glb 写到磁盘（untracked fixture，可选）。
#   --carray       打印 C 字节数组（static const unsigned char kEmbeddedGlb[]），
#                  供内嵌进测试 .cpp 做自包含测试（不依赖外部 fixture）。
#
# 依赖：pygltflib + numpy。
import argparse
import struct
import sys
import zlib

import numpy as np
from pygltflib import (
    GLTF2, Scene, Node, Mesh, Primitive, Attributes, Accessor, BufferView,
    Buffer, Material, PbrMetallicRoughness, TextureInfo, Texture, Image, Sampler,
)

# glTF / GL 常量
FLOAT = 5126
UNSIGNED_SHORT = 5123
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963


def make_1x1_png(r, g, b, a=255):
    """手写最小 1x1 RGBA PNG（无需 PIL）。"""
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)  # 1x1, 8-bit, RGBA
    raw = bytes([0, r, g, b, a])  # 单行：filter byte 0 + RGBA
    idat = zlib.compress(raw, 9)
    return sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def pad4(b):
    return b + b"\x00" * ((4 - len(b) % 4) % 4)


def build_glb():
    positions = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32)
    uvs = np.array([[0, 0], [1, 0], [0, 1]], dtype=np.float32)
    indices = np.array([0, 1, 2], dtype=np.uint16)
    png = make_1x1_png(200, 60, 30, 255)  # 红橙，肉眼可与 default 白贴图区分

    pos_b, uv_b, idx_b = positions.tobytes(), uvs.tobytes(), indices.tobytes()

    blob = b""
    pos_off = len(blob); blob += pad4(pos_b)
    uv_off = len(blob); blob += pad4(uv_b)
    idx_off = len(blob); blob += pad4(idx_b)
    png_off = len(blob); blob += pad4(png)

    g = GLTF2()
    g.scene = 0
    g.scenes = [Scene(nodes=[0])]
    g.nodes = [Node(mesh=0)]
    g.buffers = [Buffer(byteLength=len(blob))]
    g.bufferViews = [
        BufferView(buffer=0, byteOffset=pos_off, byteLength=len(pos_b), target=ARRAY_BUFFER),
        BufferView(buffer=0, byteOffset=uv_off, byteLength=len(uv_b), target=ARRAY_BUFFER),
        BufferView(buffer=0, byteOffset=idx_off, byteLength=len(idx_b), target=ELEMENT_ARRAY_BUFFER),
        BufferView(buffer=0, byteOffset=png_off, byteLength=len(png)),  # 内嵌 PNG
    ]
    g.accessors = [
        Accessor(bufferView=0, componentType=FLOAT, count=3, type="VEC3",
                 max=positions.max(0).tolist(), min=positions.min(0).tolist()),
        Accessor(bufferView=1, componentType=FLOAT, count=3, type="VEC2"),
        Accessor(bufferView=2, componentType=UNSIGNED_SHORT, count=3, type="SCALAR"),
    ]
    g.images = [Image(bufferView=3, mimeType="image/png", name="embedded_base")]
    g.samplers = [Sampler()]
    g.textures = [Texture(source=0, sampler=0)]
    g.materials = [Material(
        name="EmbeddedMat",
        pbrMetallicRoughness=PbrMetallicRoughness(
            baseColorTexture=TextureInfo(index=0), metallicFactor=0.0, roughnessFactor=1.0),
    )]
    g.meshes = [Mesh(primitives=[Primitive(
        attributes=Attributes(POSITION=0, TEXCOORD_0=1), indices=2, material=0)])]

    g.set_binary_blob(blob)
    return b"".join(g.save_to_bytes())


def emit_carray(glb):
    print("// 由 tests/fixtures/gen_embedded_glb.py --carray 生成；勿手改。")
    print("static const unsigned char kEmbeddedGlb[] = {")
    line = "    "
    for i, b in enumerate(glb):
        line += "0x%02x," % b
        if (i + 1) % 16 == 0:
            print(line)
            line = "    "
        else:
            line += " "
    if line.strip():
        print(line.rstrip())
    print("};")
    print("static const unsigned int kEmbeddedGlbSize = %d;" % len(glb))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None, help="把 .glb 写到磁盘")
    ap.add_argument("--carray", action="store_true", help="打印 C 字节数组")
    args = ap.parse_args()

    glb = build_glb()
    if args.out:
        with open(args.out, "wb") as f:
            f.write(glb)
        sys.stderr.write("wrote %d bytes to %s\n" % (len(glb), args.out))
    if args.carray or not args.out:
        emit_carray(glb)


if __name__ == "__main__":
    main()
