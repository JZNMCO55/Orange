"""orange-mcp —— OrangeEditor 实时协同 MCP server（ADR-020）。

把 OrangeEditor 进程内 TCP 命令端（`--mcp-port <n>` 启动的 NDJSON socket）包成 MCP
tools，让 Claude 等 MCP 客户端实时操作正在运行的编辑器 GUI 实例。架构仿 Blender MCP：

    Claude ──MCP(stdio)──> orange-mcp(本文件) ──TCP/NDJSON(127.0.0.1)──> OrangeEditor

本文件只做协议转译 + socket 转发；不实现任何编辑器逻辑（那在 C++ 命令端）。
命令集随编辑器演进，改 tool schema 不用重编引擎（ADR-020 决策 1）。

M0（spike 通路）：ping / get_scene_info。M1/M2 在此追加读写 tool。

运行（通常由 MCP 客户端配置自动拉起，见 README）：
    ORANGE_MCP_PORT=8765 python server.py
或：
    python server.py --port 8765
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import os
import socket
import threading
from typing import Any

from mcp.server.fastmcp import FastMCP, Image

# ---------------------------------------------------------------------------
# 配置
# ---------------------------------------------------------------------------

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8765


def _resolve_port() -> int:
    """端口优先级：--port 命令行 > ORANGE_MCP_PORT 环境变量 > 默认 8765。"""
    parser = argparse.ArgumentParser(description="orange-mcp server")
    parser.add_argument("--port", type=int, default=None,
                        help="OrangeEditor MCP 命令端端口（默认取 ORANGE_MCP_PORT 或 8765）")
    parser.add_argument("--host", type=str, default=DEFAULT_HOST)
    args, _ = parser.parse_known_args()
    if args.port is not None:
        return args.port
    env = os.environ.get("ORANGE_MCP_PORT")
    if env:
        try:
            return int(env)
        except ValueError:
            pass
    return DEFAULT_PORT


HOST = os.environ.get("ORANGE_MCP_HOST", DEFAULT_HOST)
PORT = _resolve_port()

# 连接超时 / 命令响应超时（秒）。capture_viewport（M1）含 WaitIdle，可能数百 ms，
# 故给一个宽松的响应超时；M0 命令都很快。
CONNECT_TIMEOUT = 5.0
RESPONSE_TIMEOUT = 30.0


# ---------------------------------------------------------------------------
# TCP 命令端连接（持久连接 + 失败重连，单 in-flight，加锁串行化）
# ---------------------------------------------------------------------------

class EditorConnection:
    """与 OrangeEditor 命令端的单连接封装。

    NDJSON 协议：请求 `{"id":n,"op":...,"args":{...}}\\n`，响应一行 JSON。
    单 in-flight（编辑器命令端 M0 也是单客户端、单 in-flight）；用锁串行化所有
    tool 调用，避免多个 async tool 同时写同一 socket 串话。
    """

    def __init__(self, host: str, port: int) -> None:
        self._host = host
        self._port = port
        self._sock: socket.socket | None = None
        self._buf = b""
        self._next_id = 1
        self._lock = threading.Lock()

    def _connect(self) -> socket.socket:
        s = socket.create_connection((self._host, self._port), timeout=CONNECT_TIMEOUT)
        s.settimeout(RESPONSE_TIMEOUT)
        self._sock = s
        self._buf = b""
        return s

    def _ensure(self) -> socket.socket:
        if self._sock is None:
            return self._connect()
        return self._sock

    def _close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None
            self._buf = b""

    def _read_line(self, s: socket.socket) -> str:
        while b"\n" not in self._buf:
            chunk = s.recv(4096)
            if not chunk:
                raise ConnectionError("editor closed the connection")
            self._buf += chunk
        line, _, rest = self._buf.partition(b"\n")
        self._buf = rest
        return line.decode("utf-8", errors="replace")

    def send(self, op: str, args: dict[str, Any] | None = None) -> dict[str, Any]:
        """发一条命令，返回响应的 result 字典；ok:false 时抛 RuntimeError。"""
        with self._lock:
            payload = {"id": self._next_id, "op": op, "args": args or {}}
            self._next_id += 1
            line = (json.dumps(payload) + "\n").encode("utf-8")

            # 失败重连一次（编辑器可能在两次调用间重启）。
            last_err: Exception | None = None
            for attempt in range(2):
                try:
                    s = self._ensure()
                    s.sendall(line)
                    resp_text = self._read_line(s)
                    resp = json.loads(resp_text)
                    if not resp.get("ok", False):
                        raise RuntimeError(resp.get("error", "unknown editor error"))
                    return resp.get("result", {})
                except (OSError, ConnectionError, json.JSONDecodeError) as e:
                    last_err = e
                    self._close()
                    if attempt == 0:
                        continue
            raise RuntimeError(
                f"无法连接 OrangeEditor 命令端 {self._host}:{self._port}："
                f"{last_err}。请确认编辑器已用 `--mcp-port {self._port}` 启动。"
            )


_conn = EditorConnection(HOST, PORT)


# ---------------------------------------------------------------------------
# MCP server + tools
# ---------------------------------------------------------------------------

mcp = FastMCP("orange-mcp")


@mcp.tool()
def ping() -> dict[str, Any]:
    """连通性 / 版本握手。返回 {editorVersion, protocolVersion, sceneName}。

    用它确认 OrangeEditor 命令端在线、协议版本匹配、当前打开的场景名。
    """
    return _conn.send("ping")


@mcp.tool()
def get_scene_info() -> dict[str, Any]:
    """拉取当前场景的实体树。

    返回 {sceneName, entityCount, truncated, entities[]}；每个 entity 含
    {guid, name, parentGuid, components[], position[x,y,z]}。guid 是稳定句柄
    （EntityGuid），可用于后续按实体寻址的 tool。大场景会被截断（truncated=true）。
    """
    return _conn.send("get_scene_info")


@mcp.tool()
def get_entity(guid: str) -> dict[str, Any]:
    """读取单个实体的全部组件字段。

    入参 guid = get_scene_info 返回的实体句柄。返回
    {guid, name, componentTypes[], components}；components 是
    {组件名: {字段名: 值}} 结构化字典——向量/quat 是 number array，Enum 是名字符串，
    AssetRef 是资源路径，EntityRef 是目标实体 guid。用它精确排查某实体的字段值
    （如"为什么看不见"：Renderable.visible / Transform.scale / mesh 路径）。
    """
    return _conn.send("get_entity", {"guid": guid})


@mcp.tool()
def list_component_types() -> dict[str, Any]:
    """枚举编辑器已注册的全部组件类型及其字段元数据（能力自发现）。

    返回 {componentTypes[]}；每个 {typeName, displayName, addable, removable, fields[]}，
    每字段 {name, label, type, readable, min?, max?, enumNames?, assetKind?}。
    调它了解“有哪些组件可加、每个字段是什么类型/取值范围”，再决定 set_field /
    add_component（M2）怎么传值。新增组件 schema 后本表自动出现，无需改 MCP 代码。
    """
    return _conn.send("list_component_types")


# 截图返回给 vision 的最长边上限（控制图像大小，贴合 Claude vision 输入）。
CAPTURE_MAX_EDGE = 1280


@mcp.tool()
def capture_viewport() -> Image:
    """截取编辑器 viewport 当前画面（用户屏幕所见，含后处理）返回 PNG 图像。

    让 AI「看见」场景——据此判断布局/光照/材质是否符合预期，不再凭代码推测视觉
    结果。注意：含 WaitIdle，非高频操作（单次可达数百 ms，会让编辑器卡一帧）。
    尺寸 = viewport 当前尺寸；过大时按最长边 {} 等比缩小以贴合 vision 输入。
    """.format(CAPTURE_MAX_EDGE)
    try:
        from PIL import Image as PILImage
    except ImportError as e:
        raise RuntimeError(
            "capture_viewport 需要 Pillow：pip install pillow（或 -r requirements.txt）"
        ) from e

    res = _conn.send("capture_viewport")
    w, h = int(res["width"]), int(res["height"])
    raw = base64.b64decode(res["base64"])
    if len(raw) != w * h * 4:
        raise RuntimeError(f"截图字节数不符：期望 {w*h*4}，实得 {len(raw)}")

    # 命令端字节序 BGRA8 → PIL（用 raw BGRA decoder 直接读，再转 RGB）。
    img = PILImage.frombuffer("RGBA", (w, h), raw, "raw", "BGRA", 0, 1).convert("RGB")
    longest = max(w, h)
    if longest > CAPTURE_MAX_EDGE:
        scale = CAPTURE_MAX_EDGE / longest
        img = img.resize((max(1, int(w * scale)), max(1, int(h * scale))))
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return Image(data=buf.getvalue(), format="png")


def main() -> None:
    # stdio transport（MCP 客户端通过 stdin/stdout 拉起本进程）。
    mcp.run()


if __name__ == "__main__":
    main()
