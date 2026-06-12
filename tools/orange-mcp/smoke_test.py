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

    # 3) 错误路径：未知 op
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
