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

    # 4b) get_editor_state
    r = send_recv(sock, buf, {"id": 25, "op": "get_editor_state", "args": {}})
    res = r.get("result", {})
    print(f"[smoke] get_editor_state -> ok={r.get('ok')} playState={res.get('playState')!r} "
          f"dirty={res.get('dirty')} selectedGuid={res.get('selectedGuid','')[:8]!r} "
          f"selectedCount={res.get('selectedCount')} gizmoMode={res.get('gizmoMode')!r} "
          f"gizmoSpace={res.get('gizmoSpace')!r}")
    if not r.get("ok"):
        print("[smoke] FAIL: get_editor_state ok=false"); failures += 1
    else:
        # playState 必须是三态之一；gizmoMode / gizmoSpace 取值合法
        if res.get("playState") not in ("Edit", "Play", "Paused"):
            print(f"[smoke] FAIL: playState 非法 {res.get('playState')!r}"); failures += 1
        if res.get("gizmoMode") not in ("Translate", "Rotate", "Scale"):
            print(f"[smoke] FAIL: gizmoMode 非法 {res.get('gizmoMode')!r}"); failures += 1
        if res.get("gizmoSpace") not in ("World", "Local"):
            print(f"[smoke] FAIL: gizmoSpace 非法 {res.get('gizmoSpace')!r}"); failures += 1
        if not isinstance(res.get("dirty"), bool):
            print("[smoke] FAIL: dirty 非 bool"); failures += 1

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

    # ===== M2 写闭环往返 =====
    # 6) create_entity → set_field → get_entity 验证 → add_component → delete_entity → 验证消失
    r = send_recv(sock, buf, {"id": 40, "op": "create_entity",
                              "args": {"name": "MCP Test Box", "parentGuid": ""}})
    new_guid = r.get("result", {}).get("guid", "")
    print(f"[smoke] create_entity -> ok={r.get('ok')} undoable={r.get('result',{}).get('undoable')} "
          f"guid={new_guid[:8]}")
    if not r.get("ok") or not new_guid:
        print("[smoke] FAIL: create_entity 未返回 guid"); failures += 1
    else:
        # set_field Transform.position = [1,2,3]
        r = send_recv(sock, buf, {"id": 41, "op": "set_field",
                                  "args": {"guid": new_guid, "component": "Transform",
                                           "field": "position", "value": [1.0, 2.0, 3.0]}})
        print(f"[smoke] set_field(position=[1,2,3]) -> ok={r.get('ok')} "
              f"undoable={r.get('result',{}).get('undoable')}")
        if not r.get("ok") or r.get("result", {}).get("undoable") is not True:
            print("[smoke] FAIL: set_field"); failures += 1
        # get_entity 验证写入生效
        r = send_recv(sock, buf, {"id": 42, "op": "get_entity", "args": {"guid": new_guid}})
        comps = r.get("result", {}).get("components", {})
        pos = comps.get("Transform", {}).get("position")
        nm = r.get("result", {}).get("name")
        print(f"[smoke]   验证 -> name={nm!r} Transform.position={pos}")
        if pos != [1.0, 2.0, 3.0]:
            print(f"[smoke] FAIL: set_field 未生效（position={pos}）"); failures += 1
        if nm != "MCP Test Box":
            print(f"[smoke] FAIL: create name 不符（{nm!r}）"); failures += 1
        # set_field 错误组件名 → error
        r = send_recv(sock, buf, {"id": 43, "op": "set_field",
                                  "args": {"guid": new_guid, "component": "NoSuchComp",
                                           "field": "x", "value": 1}})
        if r.get("ok") is not False:
            print("[smoke] FAIL: set_field 未知组件未报错"); failures += 1
        else:
            print(f"[smoke] set_field(bad comp) -> error={r.get('error')!r}")
        # add_component Renderable（undoable:false）
        r = send_recv(sock, buf, {"id": 44, "op": "add_component",
                                  "args": {"guid": new_guid, "component": "Renderable"}})
        print(f"[smoke] add_component(Renderable) -> ok={r.get('ok')} "
              f"undoable={r.get('result',{}).get('undoable')}")
        if not r.get("ok"):
            print("[smoke] FAIL: add_component"); failures += 1
        r = send_recv(sock, buf, {"id": 45, "op": "get_entity", "args": {"guid": new_guid}})
        if "Renderable" not in r.get("result", {}).get("componentTypes", []):
            print("[smoke] FAIL: add_component 后无 Renderable"); failures += 1
        else:
            print("[smoke]   验证 -> Renderable 已挂")
        # select_entity
        r = send_recv(sock, buf, {"id": 46, "op": "select_entity", "args": {"guid": new_guid}})
        if not r.get("ok"):
            print("[smoke] FAIL: select_entity"); failures += 1
        else:
            print("[smoke] select_entity -> ok")
        # delete_entity（undoable:false）
        r = send_recv(sock, buf, {"id": 47, "op": "delete_entity", "args": {"guid": new_guid}})
        print(f"[smoke] delete_entity -> ok={r.get('ok')} undoable={r.get('result',{}).get('undoable')}")
        if not r.get("ok"):
            print("[smoke] FAIL: delete_entity"); failures += 1
        # 下一帧消费后验证消失（get_scene_info 不含该 guid）
        time.sleep(0.2)
        r = send_recv(sock, buf, {"id": 48, "op": "get_scene_info", "args": {}})
        guids_now = {e.get("guid") for e in r.get("result", {}).get("entities", [])}
        if new_guid in guids_now:
            print("[smoke] FAIL: delete 后实体仍在场景里"); failures += 1
        else:
            print(f"[smoke]   验证 -> 实体已删除，场景剩 {r.get('result',{}).get('entityCount')} 个")

    # ===== P1 往返 =====
    # 取一个仍存活、有 Transform 的实体作只读测试基准。
    r = send_recv(sock, buf, {"id": 50, "op": "get_scene_info", "args": {}})
    ents2 = r.get("result", {}).get("entities", [])
    base = next((e for e in ents2 if "Transform" in e.get("components", [])), None)

    # P1-a) find_entities（按名 / 按组件 / 失效组件报错）
    if base is not None and base.get("name"):
        sub = base["name"][:3]
        r = send_recv(sock, buf, {"id": 51, "op": "find_entities", "args": {"name": sub}})
        fents = r.get("result", {}).get("entities", [])
        print(f"[smoke] find_entities(name={sub!r}) -> ok={r.get('ok')} count={r.get('result',{}).get('count')}")
        if not r.get("ok") or not any(base["guid"] == e.get("guid") for e in fents):
            print("[smoke] FAIL: find_entities 没找到基准实体"); failures += 1
    r = send_recv(sock, buf, {"id": 52, "op": "find_entities", "args": {"component": "Transform"}})
    if not r.get("ok") or r.get("result", {}).get("count", 0) < 1:
        print("[smoke] FAIL: find_entities(component=Transform) 空"); failures += 1
    else:
        print(f"[smoke] find_entities(component=Transform) -> count={r.get('result',{}).get('count')}")
    r = send_recv(sock, buf, {"id": 53, "op": "find_entities", "args": {"component": "NoSuchComp"}})
    if r.get("ok") is not False:
        print("[smoke] FAIL: find_entities 未知组件未报错"); failures += 1

    # P1-b) get_camera / set_camera round-trip
    r = send_recv(sock, buf, {"id": 54, "op": "get_camera", "args": {}})
    cam0 = r.get("result", {})
    print(f"[smoke] get_camera -> radius={cam0.get('radius')} fov={cam0.get('fovYDegrees')}")
    if not r.get("ok") or "pivot" not in cam0:
        print("[smoke] FAIL: get_camera 缺字段"); failures += 1
    r = send_recv(sock, buf, {"id": 55, "op": "set_camera", "args": {"radius": 12.5, "fovYDegrees": 60.0}})
    cam1 = r.get("result", {})
    if not r.get("ok") or abs(cam1.get("radius", 0) - 12.5) > 1e-3 or abs(cam1.get("fovYDegrees", 0) - 60.0) > 1e-3:
        print(f"[smoke] FAIL: set_camera 未生效 {cam1}"); failures += 1
    else:
        print(f"[smoke] set_camera(radius=12.5,fov=60) -> radius={cam1.get('radius')} fov={cam1.get('fovYDegrees')}")
    # 越界 clamp：fov 999 → ≤179；radius -5 → ≥0.01
    r = send_recv(sock, buf, {"id": 56, "op": "set_camera", "args": {"fovYDegrees": 999.0, "radius": -5.0}})
    camC = r.get("result", {})
    if r.get("ok") and (camC.get("fovYDegrees", 999) > 179.0 or camC.get("radius", -1) < 0.0):
        print(f"[smoke] FAIL: set_camera 越界未 clamp {camC}"); failures += 1
    else:
        print(f"[smoke] set_camera(clamp) -> fov={camC.get('fovYDegrees')} radius={camC.get('radius')}")

    # P1-c) frame_entity（Frame All + 指定实体）
    r = send_recv(sock, buf, {"id": 57, "op": "frame_entity", "args": {}})
    print(f"[smoke] frame_entity(All) -> ok={r.get('ok')} radius={r.get('result',{}).get('radius')}")
    if not r.get("ok"):
        print(f"[smoke] FAIL: frame_entity(All) error={r.get('error')!r}"); failures += 1

    # P1-d) get_editor_state 反映 selectedGuid（先 select 一个）
    if base is not None:
        send_recv(sock, buf, {"id": 58, "op": "select_entity", "args": {"guid": base["guid"]}})
        r = send_recv(sock, buf, {"id": 59, "op": "get_editor_state", "args": {}})
        if r.get("result", {}).get("selectedGuid") != base["guid"]:
            print("[smoke] FAIL: get_editor_state.selectedGuid 未反映 select"); failures += 1
        else:
            print(f"[smoke] get_editor_state.selectedGuid 反映 select OK")

    # P1-e) duplicate → reparent → remove_component 全套（建临时实体，最后删掉）
    r = send_recv(sock, buf, {"id": 60, "op": "create_entity", "args": {"name": "P1 Parent"}})
    pg = r.get("result", {}).get("guid", "")
    r = send_recv(sock, buf, {"id": 61, "op": "create_entity", "args": {"name": "P1 Child"}})
    cg = r.get("result", {}).get("guid", "")
    if pg and cg:
        # duplicate child
        r = send_recv(sock, buf, {"id": 62, "op": "duplicate_entity", "args": {"guid": cg}})
        dg = r.get("result", {}).get("guid", "")
        print(f"[smoke] duplicate_entity -> ok={r.get('ok')} undoable={r.get('result',{}).get('undoable')} newGuid={dg[:8]}")
        if not r.get("ok") or not dg or dg == cg:
            print("[smoke] FAIL: duplicate 未返回新 guid"); failures += 1
        # reparent child under parent (keepWorld)
        r = send_recv(sock, buf, {"id": 63, "op": "reparent_entity",
                                  "args": {"guid": cg, "newParentGuid": pg}})
        print(f"[smoke] reparent_entity -> ok={r.get('ok')} undoable={r.get('result',{}).get('undoable')}")
        if not r.get("ok"):
            print("[smoke] FAIL: reparent_entity"); failures += 1
        else:
            r = send_recv(sock, buf, {"id": 64, "op": "get_entity", "args": {"guid": cg}})
            # parentGuid 在 get_scene_info 才有；这里用 find under 验证
            r = send_recv(sock, buf, {"id": 65, "op": "find_entities", "args": {"underGuid": pg}})
            under = {e.get("guid") for e in r.get("result", {}).get("entities", [])}
            if cg not in under:
                print("[smoke] FAIL: reparent 后 child 不在 parent 子树内"); failures += 1
            else:
                print("[smoke]   验证 -> child 已在 parent 子树内")
        # reparent 环检测：把 parent 挂到 child 下 → error
        r = send_recv(sock, buf, {"id": 66, "op": "reparent_entity",
                                  "args": {"guid": pg, "newParentGuid": cg}})
        if r.get("ok") is not False:
            print("[smoke] FAIL: reparent 环未被拒绝"); failures += 1
        else:
            print(f"[smoke] reparent(cycle) -> error={r.get('error')!r}")
        # remove_component：先给 parent 加 Renderable 再 remove
        send_recv(sock, buf, {"id": 67, "op": "add_component", "args": {"guid": pg, "component": "Renderable"}})
        r = send_recv(sock, buf, {"id": 68, "op": "remove_component",
                                  "args": {"guid": pg, "component": "Renderable"}})
        print(f"[smoke] remove_component(Renderable) -> ok={r.get('ok')} undoable={r.get('result',{}).get('undoable')}")
        if not r.get("ok"):
            print("[smoke] FAIL: remove_component"); failures += 1
        else:
            r = send_recv(sock, buf, {"id": 69, "op": "get_entity", "args": {"guid": pg}})
            if "Renderable" in r.get("result", {}).get("componentTypes", []):
                print("[smoke] FAIL: remove_component 后 Renderable 仍在"); failures += 1
            else:
                print("[smoke]   验证 -> Renderable 已移除")
        # 清理临时实体（parent 删掉会连子树；dg 是独立兄弟，单独删）
        send_recv(sock, buf, {"id": 70, "op": "delete_entity", "args": {"guid": pg}})
        if dg:
            send_recv(sock, buf, {"id": 71, "op": "delete_entity", "args": {"guid": dg}})

    # P1-f) begin/end_undo_group + 错误路径
    r = send_recv(sock, buf, {"id": 72, "op": "begin_undo_group", "args": {"label": "smoke batch"}})
    if not r.get("ok"):
        print("[smoke] FAIL: begin_undo_group"); failures += 1
    # 嵌套开组 → error
    r = send_recv(sock, buf, {"id": 73, "op": "begin_undo_group", "args": {}})
    if r.get("ok") is not False:
        print("[smoke] FAIL: 嵌套 begin_undo_group 未报错"); failures += 1
    # 组内建两个实体
    r = send_recv(sock, buf, {"id": 74, "op": "create_entity", "args": {"name": "Grouped A"}})
    ga = r.get("result", {}).get("guid", "")
    r = send_recv(sock, buf, {"id": 75, "op": "create_entity", "args": {"name": "Grouped B"}})
    gb = r.get("result", {}).get("guid", "")
    r = send_recv(sock, buf, {"id": 76, "op": "end_undo_group", "args": {}})
    print(f"[smoke] undo group(2 creates) -> end ok={r.get('ok')}")
    if not r.get("ok"):
        print("[smoke] FAIL: end_undo_group"); failures += 1
    # 重复 end → error
    r = send_recv(sock, buf, {"id": 77, "op": "end_undo_group", "args": {}})
    if r.get("ok") is not False:
        print("[smoke] FAIL: 无组 end_undo_group 未报错"); failures += 1
    # 清理
    for g in (ga, gb):
        if g:
            send_recv(sock, buf, {"id": 78, "op": "delete_entity", "args": {"guid": g}})

    # P1-g) list_assets
    r = send_recv(sock, buf, {"id": 80, "op": "list_assets", "args": {}})
    al = r.get("result", {}).get("assets", [])
    print(f"[smoke] list_assets(all) -> ok={r.get('ok')} count={r.get('result',{}).get('count')}")
    if not r.get("ok"):
        print("[smoke] FAIL: list_assets ok=false"); failures += 1
    else:
        for a in al[:5]:
            print(f"        - {a.get('kind'):14} {a.get('path')}")
        # kind 过滤：Material 只回 .material
        r = send_recv(sock, buf, {"id": 81, "op": "list_assets", "args": {"kind": "Material"}})
        mats = r.get("result", {}).get("assets", [])
        if any(not a.get("path", "").endswith(".material") for a in mats):
            print("[smoke] FAIL: list_assets(kind=Material) 含非 .material"); failures += 1
        else:
            print(f"[smoke] list_assets(kind=Material) -> {len(mats)} 个")

    # P1-h) open_scene / import_asset 错误路径（不真切场景 / 不真导入，避免污染）
    r = send_recv(sock, buf, {"id": 82, "op": "open_scene", "args": {"path": "no/such/scene.scene.json"}})
    if r.get("ok") is not False:
        print("[smoke] FAIL: open_scene 不存在文件未报错"); failures += 1
    else:
        print(f"[smoke] open_scene(bad path) -> error={r.get('error')!r}")
    r = send_recv(sock, buf, {"id": 83, "op": "import_asset", "args": {"srcPath": "no/such/file.png"}})
    if r.get("ok") is not False:
        print("[smoke] FAIL: import_asset 不存在源未报错"); failures += 1
    else:
        print(f"[smoke] import_asset(bad src) -> error={r.get('error')!r}")

    # P1-i) play → get_editor_state(Play) → stop → get_editor_state(Edit)
    r = send_recv(sock, buf, {"id": 84, "op": "play", "args": {}})
    print(f"[smoke] play -> ok={r.get('ok')}")
    if r.get("ok"):
        time.sleep(0.3)  # 等帧末 ApplyPendingPlayOp 执行状态迁移
        r = send_recv(sock, buf, {"id": 85, "op": "get_editor_state", "args": {}})
        ps = r.get("result", {}).get("playState")
        if ps not in ("Play", "Paused"):
            print(f"[smoke] FAIL: play 后 playState={ps!r} 非 Play"); failures += 1
        else:
            print(f"[smoke]   验证 -> playState={ps}")
        # set_field 在 Play 期默认被拒
        if base is not None:
            r = send_recv(sock, buf, {"id": 86, "op": "set_field",
                                      "args": {"guid": base["guid"], "component": "Transform",
                                               "field": "position", "value": [0, 0, 0]}})
            if r.get("ok") is not False:
                print("[smoke] WARN: Play 期 set_field 未被拒（检查 allowInPlay 逻辑）")
            else:
                print(f"[smoke]   Play 期 set_field 被拒 OK -> {r.get('error')!r}")
        # stop 还原
        r = send_recv(sock, buf, {"id": 87, "op": "stop", "args": {}})
        time.sleep(0.3)
        r = send_recv(sock, buf, {"id": 88, "op": "get_editor_state", "args": {}})
        if r.get("result", {}).get("playState") != "Edit":
            print(f"[smoke] FAIL: stop 后未回 Edit"); failures += 1
        else:
            print("[smoke]   验证 -> stop 后回 Edit")
    else:
        print(f"[smoke] WARN: play ok=false（可能场景不可进 Play）error={r.get('error')!r}")

    # ===== P2 往返（happy-path 安全项 + 其余错误路径；真实 prefab/动画/材质创作属 dogfood）=====
    # P2-a) set_entity_order：建两个根实体，把第二个上移，再清理
    r = send_recv(sock, buf, {"id": 90, "op": "create_entity", "args": {"name": "P2 Root A"}})
    ra = r.get("result", {}).get("guid", "")
    r = send_recv(sock, buf, {"id": 91, "op": "create_entity", "args": {"name": "P2 Root B"}})
    rb = r.get("result", {}).get("guid", "")
    if rb:
        r = send_recv(sock, buf, {"id": 92, "op": "set_entity_order",
                                  "args": {"guid": rb, "direction": "up"}})
        print(f"[smoke] set_entity_order(up) -> ok={r.get('ok')} undoable={r.get('result',{}).get('undoable')}")
        if not r.get("ok"):
            print(f"[smoke] FAIL: set_entity_order error={r.get('error')!r}"); failures += 1
        # 非法 direction → error
        r = send_recv(sock, buf, {"id": 93, "op": "set_entity_order",
                                  "args": {"guid": rb, "direction": "sideways"}})
        if r.get("ok") is not False:
            print("[smoke] FAIL: set_entity_order 非法 direction 未报错"); failures += 1
    for g in (ra, rb):
        if g:
            send_recv(sock, buf, {"id": 94, "op": "delete_entity", "args": {"guid": g}})

    # P2-b) get_editor_log（读，安全）
    r = send_recv(sock, buf, {"id": 95, "op": "get_editor_log", "args": {"lines": 20}})
    print(f"[smoke] get_editor_log -> ok={r.get('ok')} count={r.get('result',{}).get('count')}")
    if not r.get("ok"):
        print("[smoke] FAIL: get_editor_log ok=false"); failures += 1
    else:
        for ln in r.get("result", {}).get("lines", [])[-3:]:
            print(f"        [{ln.get('level')}] {ln.get('timestamp')} {ln.get('message','')[:60]}")

    # P2-c) prefab 错误路径（不真建文件/不污染 assets）
    if base is not None:
        # get_prefab_status / revert_override / apply_instance 在非 prefab 实体 → error
        for op_ in ("get_prefab_status", "apply_instance"):
            r = send_recv(sock, buf, {"id": 96, "op": op_, "args": {"guid": base["guid"]}})
            if r.get("ok") is not False:
                print(f"[smoke] FAIL: {op_} 非 prefab 未报错"); failures += 1
            else:
                print(f"[smoke] {op_}(non-prefab) -> error={r.get('error')!r}")
    r = send_recv(sock, buf, {"id": 97, "op": "instantiate_prefab",
                              "args": {"path": "no/such/x.prefab.json"}})
    if r.get("ok") is not False:
        print("[smoke] FAIL: instantiate_prefab 不存在路径未报错"); failures += 1

    # P2-d) 动画错误路径（无 ClipAnimator 实体 → error）。若场景里有挂 Animator 的实体，
    #       顺带做一次 get_animation_clip round-trip。
    anim_target = next((e for e in ents2 if "Animator" in e.get("components", [])), None)
    if anim_target is not None:
        r = send_recv(sock, buf, {"id": 98, "op": "get_animation_clip",
                                  "args": {"guid": anim_target["guid"]}})
        print(f"[smoke] get_animation_clip({anim_target['name']!r}) -> ok={r.get('ok')}")
        if r.get("ok"):
            cj = r.get("result", {}).get("clipJson", "")
            try:
                json.loads(cj)  # clipJson 必须是合法 JSON
                print(f"        clipJson 长度={len(cj)}（合法 JSON）")
                # round-trip：原样写回应成功
                r2 = send_recv(sock, buf, {"id": 99, "op": "set_animation_clip",
                                           "args": {"guid": anim_target["guid"], "clipJson": cj}})
                if not r2.get("ok"):
                    print(f"[smoke] FAIL: set_animation_clip round-trip error={r2.get('error')!r}"); failures += 1
                else:
                    print(f"[smoke] set_animation_clip(round-trip) -> ok undoable={r2.get('result',{}).get('undoable')}")
            except json.JSONDecodeError:
                print("[smoke] FAIL: get_animation_clip 返回非法 JSON"); failures += 1
    else:
        # 无 Animator 实体：用基准实体验证"无 ClipAnimator → error"
        if base is not None:
            r = send_recv(sock, buf, {"id": 98, "op": "get_animation_clip", "args": {"guid": base["guid"]}})
            if r.get("ok") is not False:
                print("[smoke] FAIL: get_animation_clip 无 animator 未报错"); failures += 1
            else:
                print(f"[smoke] get_animation_clip(no animator) -> error={r.get('error')!r}")
        print("[smoke] (场景无 Animator 实体，clip round-trip happy-path 属 dogfood)")

    # P2-e) set_script_field 错误路径（无 ScriptComponent → error）
    if base is not None:
        r = send_recv(sock, buf, {"id": 100, "op": "set_script_field",
                                  "args": {"guid": base["guid"], "fieldName": "jumpImpulse", "value": 5.0}})
        if r.get("ok") is not False:
            print("[smoke] FAIL: set_script_field 无 ScriptComponent 未报错"); failures += 1
        else:
            print(f"[smoke] set_script_field(no script) -> error={r.get('error')!r}")

    # P2-f) set_material_param 错误路径（不存在文件 → error）
    r = send_recv(sock, buf, {"id": 101, "op": "set_material_param",
                              "args": {"path": "no/such.material", "param": "ToonColor", "value": [1, 0, 0, 1]}})
    if r.get("ok") is not False:
        print("[smoke] FAIL: set_material_param 不存在文件未报错"); failures += 1
    else:
        print(f"[smoke] set_material_param(bad path) -> error={r.get('error')!r}")

    # 7) 错误路径：未知 op
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
