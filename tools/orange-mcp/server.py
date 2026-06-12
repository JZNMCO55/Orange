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


@mcp.tool()
def get_editor_state() -> dict[str, Any]:
    """读取编辑器的轻量状态快照（一次往返掌握全局，省试错）。

    返回 {playState, scenePath, sceneName, dirty, selectedGuid, selectedCount,
    gizmoMode, gizmoSpace, gizmoVisible}：
    - playState = "Edit" / "Play" / "Paused"——Play/Paused 期写操作（set_field 等）
      默认被拒，调用前先看这个；
    - dirty = 是否有未保存改动（决定 open_scene 是否需要 force / 提醒用户先存）；
    - selectedGuid / selectedCount = 当前选中实体（空串 = 没选）；
    - gizmoMode = "Translate"/"Rotate"/"Scale"，gizmoSpace = "World"/"Local"。
    """
    return _conn.send("get_editor_state")


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


# ---------------------------------------------------------------------------
# M2 写闭环 tools —— 全部走编辑器命令栈（除 delete/select/save，见各 docstring）。
# AI 的写操作用户在编辑器里实时可见；可 Undo 的操作用户 Ctrl+Z 即可撤销。
# ---------------------------------------------------------------------------


@mcp.tool()
def create_entity(name: str = "New Entity", parentGuid: str = "") -> dict[str, Any]:
    """创建一个空实体（带 Name + Transform），返回新实体的 guid。

    parentGuid 非空时挂到该父实体下（保持层级）。走命令栈，用户可 Ctrl+Z 撤销。
    建完通常接 add_component(Renderable) + set_field(Transform.position) 摆到位。
    """
    return _conn.send("create_entity", {"name": name, "parentGuid": parentGuid})


@mcp.tool()
def set_field(guid: str, component: str, field: str, value: Any,
              allowInPlay: bool = False) -> dict[str, Any]:
    """改某实体某组件的某字段（与 Inspector 拖字段完全同路径，可 Undo + 合并）。

    component/field 用 list_component_types 给出的 typeName/字段 name；value 的形态按
    字段 type：数值→number，bool→true/false，Vec2/3/4→number 数组，Quat→[w,x,y,z]，
    Enum→名字符串（或 int），String/AssetRef→字符串。有 range 的字段会被 clamp。
    Play 模式默认拒绝写（改动会被 Stop 还原）；确需 Play 中调参传 allowInPlay=True。
    """
    return _conn.send("set_field", {"guid": guid, "component": component,
                                    "field": field, "value": value,
                                    "allowInPlay": allowInPlay})


@mcp.tool()
def add_component(guid: str, component: str) -> dict[str, Any]:
    """给实体挂一个组件（component = list_component_types 里 addable=true 的 typeName）。

    ⚠️ 现状编辑器 Add Component 不进命令栈（会清空 undo 历史），故返回 undoable:false——
    加错了不能 Ctrl+Z 撤，需 remove 或手动处理。Renderable 会预置 cube+PBR 默认材质。
    """
    return _conn.send("add_component", {"guid": guid, "component": component})


@mcp.tool()
def delete_entity(guid: str) -> dict[str, Any]:
    """删除实体及其整个子树。

    现已**可撤销**（返回 undoable:true）：用户 Ctrl+Z 能恢复被删子树（含原层级位置）。
    实际删除在编辑器下一帧发生。删多实体子树仍建议先 select_entity 让用户看清范围。
    """
    return _conn.send("delete_entity", {"guid": guid})


@mcp.tool()
def select_entity(guid: str = "") -> dict[str, Any]:
    """设置编辑器当前选中实体（让用户在 Inspector / viewport 看到 AI 在操作谁）。

    guid 留空 = 清空选中。非命令栈操作（纯 UI 状态）。
    """
    return _conn.send("select_entity", {"guid": guid})


@mcp.tool()
def save_scene(path: str = "") -> dict[str, Any]:
    """保存当前场景。path 非空 = 另存为该路径（.scene.json）并切为当前场景。

    非命令栈（文件 IO，undoable:false）。实际写盘在编辑器下一帧执行（返回 queued:true）。
    path 留空且当前无场景路径时，编辑器会弹文件对话框（GUI），不适合自动化无人值守。
    """
    return _conn.send("save_scene", {"path": path})


# ---------------------------------------------------------------------------
# P1 工具 —— E1 协同效率包 / E2 资产管线 / E3 Play 调试。
# ---------------------------------------------------------------------------


@mcp.tool()
def find_entities(name: str = "", component: str = "", underGuid: str = "") -> dict[str, Any]:
    """按条件过滤场景实体（多条件 AND，全可选），返回 {count, truncated, entities[]}。

    - name：名字子串（大小写不敏感）；
    - component：只保留挂了该组件的实体（用 list_component_types 的 typeName）；
    - underGuid：只保留该实体子树内的（含其自身）。
    无命中返回空数组（非错误）。比 get_scene_info 更省 token——调试"鳄梨在哪"先 find
    再 get_entity，避免整树 dump。
    """
    return _conn.send("find_entities",
                      {"name": name, "component": component, "underGuid": underGuid})


@mcp.tool()
def get_camera() -> dict[str, Any]:
    """读编辑器轨道相机参数：{pivot[x,y,z], azimuth, elevation, radius, fovYDegrees, zNear, zFar}。

    azimuth/elevation 是弧度球坐标，radius 是相机到 pivot 距离。配合 set_camera 做构图。
    """
    return _conn.send("get_camera")


@mcp.tool()
def set_camera(pivot: list[float] | None = None, azimuth: float | None = None,
               elevation: float | None = None, radius: float | None = None,
               fovYDegrees: float | None = None, zNear: float | None = None,
               zFar: float | None = None) -> dict[str, Any]:
    """写编辑器轨道相机（AI 自主构图后 capture_viewport）。所有参数可选，只改传入的。

    pivot=[x,y,z] 轨道中心；azimuth/elevation 弧度（elevation 自动 clamp 到 ±~89° 防翻面）；
    radius>0 距离；fovYDegrees∈[1,179]。非命令栈（相机非场景数据，不产 undo）。
    返回生效后的完整相机参数。
    """
    args: dict[str, Any] = {}
    if pivot is not None: args["pivot"] = pivot
    if azimuth is not None: args["azimuth"] = azimuth
    if elevation is not None: args["elevation"] = elevation
    if radius is not None: args["radius"] = radius
    if fovYDegrees is not None: args["fovYDegrees"] = fovYDegrees
    if zNear is not None: args["zNear"] = zNear
    if zFar is not None: args["zFar"] = zFar
    return _conn.send("set_camera", args)


@mcp.tool()
def frame_entity(guid: str = "") -> dict[str, Any]:
    """把相机对准实体（自动算距离看全包围盒）。guid 留空 = Frame All（看全场景）。

    不改用户选中（与 select_entity 正交）。非命令栈。返回生效后相机参数。
    截图前先 frame_entity 调好构图，AI 看得更清。
    """
    return _conn.send("frame_entity", {"guid": guid})


@mcp.tool()
def duplicate_entity(guid: str, allowInPlay: bool = False) -> dict[str, Any]:
    """复制实体子树（克隆体换新 guid + 新 prefab 实例 id），返回新根 guid。

    可 Undo（用户 Ctrl+Z 删掉克隆）。克隆体作原实体的兄弟（同父），并被自动选中。
    Play 模式默认拒绝（传 allowInPlay=True 覆盖）。
    """
    return _conn.send("duplicate_entity", {"guid": guid, "allowInPlay": allowInPlay})


@mcp.tool()
def reparent_entity(guid: str, newParentGuid: str = "", keepWorld: bool = True,
                    allowInPlay: bool = False) -> dict[str, Any]:
    """改实体的父节点。newParentGuid 留空 = 提为根。可 Undo（精确复位原层级位置）。

    keepWorld=True（默认）保持世界位姿不变（Unity "keep world position"）；False 则保
    local 不变（相对新父重新解释，可能跳位）。环检测：把祖先挂到后代下 / 挂到自己下
    → 报错拒绝。Play 模式默认拒绝（allowInPlay=True 覆盖）。
    """
    return _conn.send("reparent_entity", {"guid": guid, "newParentGuid": newParentGuid,
                                          "keepWorld": keepWorld, "allowInPlay": allowInPlay})


@mcp.tool()
def remove_component(guid: str, component: str, allowInPlay: bool = False) -> dict[str, Any]:
    """卸掉实体上的某组件（component = list_component_types 里 removable=true 的 typeName）。

    多数组件可 Undo（返回 undoable:true，撤销时重建组件并还原原字段值）；少数无法重建的
    （Name/Hierarchy/Animator 等）不可 Undo（undoable:false，会清空 undo 历史）。
    Play 模式默认拒绝（allowInPlay=True 覆盖）。
    """
    return _conn.send("remove_component", {"guid": guid, "component": component,
                                           "allowInPlay": allowInPlay})


@mcp.tool()
def begin_undo_group(label: str = "") -> dict[str, Any]:
    """开始一个命令组：在 end_undo_group 之前的所有写操作合并为**一条** undo 记录。

    用于"搭一组灰盒平台"这类批量操作——用户一次 Ctrl+Z 即可整组回退，不必撤 N 次。
    嵌套开组会报错。**护栏**：组打开超过 30s 未关、或 MCP 连接断开，编辑器自动闭合该组
    （防忘调 end_undo_group 把命令栈卡死）。务必配对调用 end_undo_group。
    """
    return _conn.send("begin_undo_group", {"label": label})


@mcp.tool()
def end_undo_group() -> dict[str, Any]:
    """结束 begin_undo_group 开的命令组：组内命令打包成单条 undo 记录入栈。

    没有打开的组时报错。空组（其间没产生任何写）直接丢弃，不污染栈。
    """
    return _conn.send("end_undo_group")


@mcp.tool()
def open_scene(path: str, force: bool = False) -> dict[str, Any]:
    """打开一个 .scene.json 场景文件（切换当前编辑的场景）。

    **dirty 保护**：当前有未保存改动且 force=False 时报错（防默默丢用户工作）——先 save_scene
    或显式传 force=True 丢弃改动。仅 Edit 模式可开（Play 中报错）。文件不存在报错。
    实际切场景在编辑器下一帧执行。
    """
    return _conn.send("open_scene", {"path": path, "force": force})


@mcp.tool()
def play() -> dict[str, Any]:
    """进入 Play 模式（Edit→Play）：World 快照落盘 + 物理/VFX/动画开始 tick。

    已在 Play/Paused 时报错。Stop 时会从快照还原回 Edit 前状态（Play 期改动丢弃）。
    Play 期读类 tool（get_entity / capture_viewport）照常工作——这正是观察玩法行为的核心；
    写类 tool 默认被拒（改动会被 Stop 还原）。
    """
    return _conn.send("play")


@mcp.tool()
def pause() -> dict[str, Any]:
    """暂停 Play（Play→Paused）：tick 停在当前帧，便于 get_entity + 截图细看。需当前在 Play。"""
    return _conn.send("pause")


@mcp.tool()
def resume() -> dict[str, Any]:
    """从暂停恢复运行（Paused→Play）。需当前在 Paused。"""
    return _conn.send("resume")


@mcp.tool()
def stop() -> dict[str, Any]:
    """停止 Play 回 Edit（Play/Paused→Edit）：从快照还原场景，Play 期所有改动丢弃。

    需当前在 Play/Paused。还原后 AI 之前持有的实体 guid 仍有效（guid 跨快照往返稳定）。
    """
    return _conn.send("stop")


@mcp.tool()
def list_assets(kind: str = "", pathPrefix: str = "") -> dict[str, Any]:
    """枚举 assets/ 下的资产文件，返回 {count, truncated, assets[]}；每项 {path, kind}。

    kind 过滤（"Mesh"/"Material"/"Texture"/"Scene"/"Sound"/"AnimationClip"，留空=全部）；
    pathPrefix 子串过滤。返回的 path 是 forward-slash 相对路径，可直接喂给 set_field 的
    AssetRef 字段（如 Renderable.mesh / materialInstance）。给字段赋资产前先 list 查合法路径。
    """
    return _conn.send("list_assets", {"kind": kind, "pathPrefix": pathPrefix})


@mcp.tool()
def import_asset(srcPath: str, scale: float = 1.0) -> dict[str, Any]:
    """导入外部资产到 assets/（与编辑器拖拽 / File→Import 同路径）。

    支持 .png/.jpg/.jpeg/.tga/.hdr/.obj/.gltf/.glb/.fbx。返回 {destPath, message, materialPaths[]}
    ——destPath 是引擎自家格式产物路径（可接着 create_entity + set_field 挂上）；materialPaths
    是 gltf/fbx 多材质各 slot 的 .material 路径。scale 仅 FBX 用（真 cm 文件传 0.01，默认 1.0
    信任已烘米）。非命令栈（文件 IO，undoable:false）。配合 Blender MCP 做 DCC→引擎管线。
    """
    return _conn.send("import_asset", {"srcPath": srcPath, "scale": scale})


# ---------------------------------------------------------------------------
# P2 工具 —— E5 prefab / E6 动画 / E7 脚本 / E8 杂项（场景 / 材质 / 可观测）。
# ---------------------------------------------------------------------------


@mcp.tool()
def set_entity_order(guid: str, direction: str, allowInPlay: bool = False) -> dict[str, Any]:
    """调整**根实体**在 Hierarchy 里的同级顺序（对应右键 Move Up / Move Down）。可 Undo。

    direction = "up" / "down"。仅作用于根节点（无父）；非根 / 单根 / 已在边界 → 报错。
    """
    return _conn.send("set_entity_order", {"guid": guid, "direction": direction,
                                           "allowInPlay": allowInPlay})


@mcp.tool()
def new_scene(force: bool = False) -> dict[str, Any]:
    """新建一个空场景。

    **dirty 保护**：当前有未保存改动且 force=False 时报错——先 save_scene 或传 force=True
    丢弃。仅 Edit 模式可新建。非命令栈（清空 undo 历史）。实际切换在下一帧执行。
    """
    return _conn.send("new_scene", {"force": force})


@mcp.tool()
def create_prefab(rootGuid: str, path: str) -> dict[str, Any]:
    """把一棵实体子树落盘成 .prefab.json 文件（prefab 资产）。

    rootGuid = 子树根；path = 目标 .prefab.json 路径（父目录须已存在）。prefabName 自动
    取文件名。非命令栈（文件 IO，undoable:false）。建完可用 instantiate_prefab 实例化。
    """
    return _conn.send("create_prefab", {"rootGuid": rootGuid, "path": path})


@mcp.tool()
def instantiate_prefab(path: str, parentGuid: str = "", allowInPlay: bool = False) -> dict[str, Any]:
    """实例化一个 .prefab.json 到场景，返回新实例根 guid。可 Undo。

    parentGuid 给定时实例化后挂到该父下（keep-world）。⚠️ MVP 限制：undo 销毁整树正确，
    但 undo 后 redo 会重建为根（不重放 reparent）。文件不存在 / 加载失败 → 报错。
    """
    return _conn.send("instantiate_prefab", {"path": path, "parentGuid": parentGuid,
                                             "allowInPlay": allowInPlay})


@mcp.tool()
def get_prefab_status(guid: str) -> dict[str, Any]:
    """查 prefab 实例的 override 状态：{isPrefabInstance, templatePath, isInstanceRoot,
    overrideCount, overriddenPaths[]}。

    overriddenPaths 是实例相对模板真实改过的字段叶子路径（"componentName/fieldPath"）。
    非 prefab 实例 → 报错。用它审计哪些实例字段偏离了模板。
    """
    return _conn.send("get_prefab_status", {"guid": guid})


@mcp.tool()
def revert_override(guid: str, component: str = "", field: str = "") -> dict[str, Any]:
    """把 prefab 实例的 override 回退到模板值。

    component + field 都给 = 只回退该字段；都不给 = 回退**所有** override。⚠️ 引擎层缺
    typed 逆写原语，**不可 Undo**（undoable:false）——回退前确认。非 prefab 实例 / 该字段
    无 override → 报错。
    """
    return _conn.send("revert_override", {"guid": guid, "component": component, "field": field})


@mcp.tool()
def apply_instance(guid: str) -> dict[str, Any]:
    """把 prefab 实例的当前态推回模板（重写 .prefab.json，所有同模板实例下次会跟随）。

    ⚠️ 资产文件 IO，**不可 Undo**（undoable:false）——会改磁盘上的 .prefab。应用前确认。
    非 prefab 实例 → 报错。
    """
    return _conn.send("apply_instance", {"guid": guid})


@mcp.tool()
def get_animation_clip(guid: str = "", path: str = "") -> dict[str, Any]:
    """读取动画 clip 的 JSON（schema animation/Clip：tracks / keyframes / interp / events）。

    guid = 读该实体 ClipAnimator 当前 clip；path = 读 .anim 文件（guid 优先）。返回
    {clipJson}（字符串，json.loads 解析）。配合 set_animation_clip 做关键帧创作。
    """
    return _conn.send("get_animation_clip", {"guid": guid, "path": path})


@mcp.tool()
def set_animation_clip(guid: str, clipJson: str, allowInPlay: bool = False) -> dict[str, Any]:
    """把整个 clip 写回实体 ClipAnimator（与 timeline GUI 同一命令，可 Undo）。

    clipJson = animation/Clip schema 的 JSON 字符串（通常先 get_animation_clip 拿到再改）。
    校验失败（schema / key 乱序 / 未知 interp）→ 报错不落。实体须挂 ClipAnimator backend。
    """
    return _conn.send("set_animation_clip", {"guid": guid, "clipJson": clipJson,
                                             "allowInPlay": allowInPlay})


@mcp.tool()
def preview_animation(guid: str, action: str, time: float | None = None) -> dict[str, Any]:
    """编辑期预览实体动画（不进 Play 模式，单 animator tick）。

    action = "play"（开始预览播放）/ "pause"（停在当前帧）/ "seek"（跳到 time 秒，需传 time）。
    与 Play 模式互斥（Play/Paused 中调用报错）。连续 seek + capture_viewport 可抽帧看运动曲线。
    """
    args: dict[str, Any] = {"guid": guid, "action": action}
    if time is not None:
        args["time"] = time
    return _conn.send("preview_animation", args)


@mcp.tool()
def set_script_field(guid: str, fieldName: str, value: Any, type: str = "",
                     allowInPlay: bool = False) -> dict[str, Any]:
    """改 ScriptComponent 的 fieldOverride（authored tweakable，UC-5 调玩法参数入口）。

    fieldName = C# 脚本的 public 字段名；找不到同名则追加一条。type 可显式给
    （"Float"/"Int"/"Bool"/"String"），否则按 value 形态推断。⚠️ 直接 mutate，**不可 Undo**
    （undoable:false）。注：EnterPlay 接 ScriptSystem 后才会真正驱动脚本（当前为数据编辑）。
    """
    args: dict[str, Any] = {"guid": guid, "fieldName": fieldName, "value": value,
                            "allowInPlay": allowInPlay}
    if type:
        args["type"] = type
    return _conn.send("set_script_field", args)


@mcp.tool()
def create_material(path: str, templateName: str) -> dict[str, Any]:
    """创建一个 .material 文件（指定 shader 模板，uniforms 用模板默认）。

    path = 目标 .material 路径；templateName = shader 模板名（如 "toon" / "pbr"）。
    非命令栈（文件 IO，undoable:false）。建完可用 set_material_param 调参 / set_field 挂到 Renderable。
    """
    return _conn.send("create_material", {"path": path, "templateName": templateName})


@mcp.tool()
def set_material_param(path: str, param: str, value: Any) -> dict[str, Any]:
    """改 .material 文件里某个 uniform override 的值并落盘。

    param = uniform 名（必须已在该 .material 里）；value 形态按其类型：Float→number、
    Int→int、Vec2/3/4→number 数组。⚠️ 资产文件 IO，**不可 Undo**（undoable:false）。
    viewport 是否热生效按编辑器现状（可能需 reload）。
    """
    return _conn.send("set_material_param", {"path": path, "param": param, "value": value})


# 日志 level 名（与 Orange::Engine::Log::Level int 值对应，便于人读）。
_LOG_LEVEL_NAMES = ["Trace", "Debug", "Info", "Warn", "Error", "Critical"]


@mcp.tool()
def get_editor_log(lines: int = 100, minLevel: int = 0) -> dict[str, Any]:
    """拉取编辑器最近日志（AI 操作触发的引擎告警可见——资产加载失败 / shader 告警等）。

    lines = 最多返回条数（默认 100）；minLevel = 最低级别（0=Trace, 2=Info, 3=Warn, 4=Error）。
    返回 {count, lines[]}；每条 {level(int), levelName, timestamp, message}。AI 写操作后查它
    确认有没有引擎层报错。
    """
    res = _conn.send("get_editor_log", {"lines": lines, "minLevel": minLevel})
    # 给每条补一个人类可读 levelName，便于 AI / 用户判读。
    for ln in res.get("lines", []):
        lv = ln.get("level", 0)
        ln["levelName"] = _LOG_LEVEL_NAMES[lv] if 0 <= lv < len(_LOG_LEVEL_NAMES) else str(lv)
    return res


def main() -> None:
    # stdio transport（MCP 客户端通过 stdin/stdout 拉起本进程）。
    mcp.run()


if __name__ == "__main__":
    main()
