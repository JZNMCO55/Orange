"""orange-mcp 命令端裸 TCP smoke 测试（无需 mcp 包）。

直连 OrangeEditor 的 `--mcp-port` NDJSON socket，跑 M0 命令并校验响应，用于在没有
完整 MCP 客户端时快速验证 C++ 命令端端到端通路（ping / get_scene_info / 错误路径）。

用法：
    1. 先启动编辑器：OrangeEditor.exe --mcp-port 8765
    2. python smoke_test.py [--port 8765]
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time


def send_recv(sock: socket.socket, buf: bytearray, payload: dict) -> dict:
    line = (json.dumps(payload) + "\n").encode("utf-8")
    sock.sendall(line)
    while b"\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("editor closed connection")
        buf += chunk
    nl = buf.index(b"\n")
    resp = bytes(buf[:nl])
    del buf[: nl + 1]
    return json.loads(resp.decode("utf-8"))


def connect_with_retry(host: str, port: int, timeout_s: float = 30.0) -> socket.socket:
    deadline = time.time() + timeout_s
    last = None
    while time.time() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=2.0)
            s.settimeout(30.0)
            return s
        except OSError as e:
            last = e
            time.sleep(0.5)
    raise RuntimeError(f"无法连接 {host}:{port}（{timeout_s}s 超时）：{last}")


def main() -> int:
    # Windows 控制台默认 GBK，强制 UTF-8 输出避免中文 / 非 ASCII 字符编码崩溃。
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except (AttributeError, OSError):
        pass

    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--host", type=str, default="127.0.0.1")
    args = ap.parse_args()

    print(f"[smoke] 连接 {args.host}:{args.port} ...")
    sock = connect_with_retry(args.host, args.port)
    buf = bytearray()
    failures = 0

    # 1) ping
    r = send_recv(sock, buf, {"id": 1, "op": "ping", "args": {}})
    print("[smoke] ping ->", json.dumps(r, ensure_ascii=False))
    if not r.get("ok") or "editorVersion" not in r.get("result", {}):
        print("[smoke] FAIL: ping 响应缺字段")
        failures += 1
    if r.get("id") != 1:
        print("[smoke] FAIL: ping id 未回显")
        failures += 1

    # 2) get_scene_info
    r = send_recv(sock, buf, {"id": 2, "op": "get_scene_info", "args": {}})
    res = r.get("result", {})
    ents = res.get("entities", [])
    print(f"[smoke] get_scene_info -> ok={r.get('ok')} "
          f"entityCount={res.get('entityCount')} truncated={res.get('truncated')} "
          f"sceneName={res.get('sceneName')}")
    if not r.get("ok"):
        print("[smoke] FAIL: get_scene_info ok=false")
        failures += 1
    else:
        # 抽样打印前几个实体，便于人工与 Hierarchy 面板对照
        for e in ents[:8]:
            print(f"        - {e.get('name','')!r:24} guid={e.get('guid','')[:8]} "
                  f"parent={e.get('parentGuid','')[:8]:8} comps={e.get('components')}")
        if len(ents) > 8:
            print(f"        ... 共 {len(ents)} 个实体")
        # 不变量校验：所有实体有 guid；有 parentGuid 的能在集合里找到
        guids = {e.get("guid") for e in ents}
        for e in ents:
            if not e.get("guid"):
                print(f"[smoke] FAIL: 实体 {e.get('name')!r} 无 guid")
                failures += 1
            pg = e.get("parentGuid")
            if pg and pg not in guids:
                print(f"[smoke] FAIL: 实体 {e.get('name')!r} 的 parentGuid 悬空")
                failures += 1

    # 3) get_entity（取上一步第一个有 Transform 的实体）
    target = next((e for e in ents if "Transform" in e.get("components", [])), None)
    if target is not None:
        r = send_recv(sock, buf, {"id": 10, "op": "get_entity",
                                  "args": {"guid": target["guid"]}})
        res = r.get("result", {})
        comps = res.get("components", {})
        print(f"[smoke] get_entity({target['name']!r}) -> ok={r.get('ok')} "
              f"componentTypes={res.get('componentTypes')}")
        tr = comps.get("Transform", {})
        print(f"        Transform={tr}")
        if not r.get("ok"):
            print("[smoke] FAIL: get_entity ok=false"); failures += 1
        elif "position" not in tr or len(tr.get("position", [])) != 3:
            print("[smoke] FAIL: get_entity Transform.position 缺失/非3维"); failures += 1
        # 失效 guid → error
        r = send_recv(sock, buf, {"id": 11, "op": "get_entity",
                                  "args": {"guid": "00000000000000000000000000000001"}})
        if r.get("ok") is not False:
            print("[smoke] FAIL: 失效 guid 未报错"); failures += 1
        else:
            print(f"[smoke] get_entity(失效 guid) -> error={r.get('error')!r}")

    # 4) list_component_types
    r = send_recv(sock, buf, {"id": 20, "op": "list_component_types", "args": {}})
    types = r.get("result", {}).get("componentTypes", [])
    names = [t.get("typeName") for t in types]
    print(f"[smoke] list_component_types -> {len(types)} 个组件: {names}")
    if not r.get("ok") or "Transform" not in names:
        print("[smoke] FAIL: list_component_types 缺 Transform"); failures += 1
    else:
        tdef = next(t for t in types if t.get("typeName") == "Transform")
        fnames = [f.get("name") for f in tdef.get("fields", [])]
        print(f"        Transform fields={fnames} addable={tdef.get('addable')}")
        if "position" not in fnames:
            print("[smoke] FAIL: Transform schema 缺 position 字段"); failures += 1

    # 5) capture_viewport（裸校验：base64 解码字节数 == w*h*4；像素视觉对错属 dogfood）
    import base64 as _b64
    r = send_recv(sock, buf, {"id": 30, "op": "capture_viewport", "args": {}})
    res = r.get("result", {})
    print(f"[smoke] capture_viewport -> ok={r.get('ok')} "
          f"fmt={res.get('format')} {res.get('width')}x{res.get('height')} "
          f"b64len={len(res.get('base64',''))}")
    if not r.get("ok"):
        print(f"[smoke] FAIL: capture_viewport ok=false error={r.get('error')!r}")
        failures += 1
    else:
        w0, h0 = int(res.get("width", 0)), int(res.get("height", 0))
        try:
            raw = _b64.b64decode(res.get("base64", ""))
        except Exception as e:  # noqa: BLE001
            raw = b""
            print(f"[smoke] FAIL: base64 解码失败 {e}"); failures += 1
        if w0 <= 0 or h0 <= 0:
            print("[smoke] FAIL: capture 尺寸非正"); failures += 1
        elif len(raw) != w0 * h0 * 4:
            print(f"[smoke] FAIL: 像素字节数 {len(raw)} != {w0*h0*4}"); failures += 1
        else:
            # 抽样：非全黑（至少一个像素非零）——证明确实渲染了内容
            nonzero = any(raw[i] for i in range(0, min(len(raw), 40000), 1))
            print(f"        像素非全黑={nonzero}（全黑可能是 viewport 未渲染，dogfood 复核）")

    # 6) 错误路径：未知 op
    r = send_recv(sock, buf, {"id": 3, "op": "no_such_op", "args": {}})
    print("[smoke] unknown op ->", json.dumps(r, ensure_ascii=False))
    if r.get("ok") is not False or "unknown op" not in r.get("error", ""):
        print("[smoke] FAIL: 未知 op 未正确报错")
        failures += 1

    sock.close()
    if failures == 0:
        print("[smoke] PASS: 全部通过")
        return 0
    print(f"[smoke] FAIL: {failures} 项失败")
    return 1


if __name__ == "__main__":
    sys.exit(main())
