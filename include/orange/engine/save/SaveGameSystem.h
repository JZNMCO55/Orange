#ifndef ORANGE_ENGINE_SAVE_SAVE_GAME_SYSTEM_H
#define ORANGE_ENGINE_SAVE_SAVE_GAME_SYSTEM_H

// ---------------------------------------------------------------------------
// SaveGameSystem —— 玩家进度的 Save / Load 主流程。注意是 service（手动
// 调用），**不是** ECS system（不进 World 主循环）。
//
// 与 Scene 序列化的边界：
//   * Scene 持久化 **content**：关卡布局 / 美术资源引用 / 静态 entity，
//     由设计师产出，跟随版本一起发布。
//   * SaveGame 持久化 **player state**：玩家在游戏过程中产生的数据（位
//     置 / 物品 / 进度旗标），必须穿越引擎升级。
//   * 同一 World 里两套数据共存——Scene 加载摆好关卡，玩家进游戏后
//     SaveGame 再叠加上他自己的状态。
//
// "哪些 entity 算可入存档"由 `SaveableComponent` 标记决定：
//   * Save：只遍历挂着 SaveableComponent 的 entity；为每个这样的 entity
//     按 SaveGameRegistry 注册顺序检查每个组件类型，存在则写出。
//   * Load：每个 JSON entity 创建一个新 ECS entity 并 attach
//     SaveableComponent，再按 registry 尝试反序列化每个 component；
//     不清空 World——加载后的 entity 追加到现有世界中。
//
// 文件格式（防小白手改 + 完整性校验，但不当作真正反作弊）：
//
//     +------+------+------+------+------+------+------+------+
//     |  'O' |  'S' |  'A' |  'V' |          formatVersion    |   8 B
//     +------+------+------+------+----------+----------------+
//     |       payloadCrc32         |         payloadLength    |   8 B
//     +------+------+------+------+------+------+------+------+
//     |                  JSON payload (UTF-8) ...             |
//     +-------------------------------------------------------+
//
//   * magic 'O''S''A''V'：4-byte sentinel，前 4 个字节不匹配 → IoError；
//   * formatVersion = 1（uint32 LE）：header 自身格式版本号，bump 时硬墙
//     拒绝（不与 payload schemaVersion 复用——header 演化与 schema 演化
//     是两个轴）；
//   * payloadCrc32：IEEE 802.3 多项式 0xEDB88320，仅覆盖 payload 字节
//     （不含 header 自身）。Load 时先校验，不匹配 → InvalidArgument；
//   * payloadLength：uint32 LE，与文件 size - 16 必须一致；不一致（截断
//     / 填充）→ InvalidArgument；
//   * payload：用 `Core::Serialization::JsonWriter::Dump(2)` 产出的 UTF-8
//     文本，无 trailing newline。
//
// 顶层 JSON schema（namespace="save/game", major=1, minor=0）：
//
//     {
//       "schemaVersion": { "namespace": "save/game", "major": 1, "minor": 0 },
//       "entities": [
//         {
//           "id": <持久 int, 0-based 顺序分配>,
//           "components": {
//             "<componentName>": {
//               "version": { "namespace": "...", "major": N, "minor": M },
//               "data":    { ...游戏侧 typed write 落的字段... }
//             },
//             ...
//           }
//         }
//       ]
//     }
//
//   * 持久 entity ID：与 Scene 序列化同模式——按 view 顺序 0..N-1 分配，
//     与 EnTT entity 解耦。当前 schema v1 不持久化 entity 间引用，所以
//     该 ID 仅用于诊断 / 未来扩展时不破兼容。
//   * 每条 component 自带 `version` 与游戏侧 schema 一一对应——一旦发
//     给玩家就冻结；后续游戏侧 bump major/minor 时由 Task 05 的 migrator
//     hook 接管。Task 02 仅做 `SchemaVersion::CanRead` 严格检查，不通过
//     直接返回 SchemaMismatch。
//   * 未识别 component 名：JSON 里出现但 registry 没注册 → silent skip
//     （forward-compat：旧 reader 处理不动新字段时不崩）。
//
// 原子写入：先写到 `<path>.tmp` → 操作系统 flush（Windows 上调 Win32
// FlushFileBuffers）→ std::filesystem::rename(tmp, path)。任何中间步骤
// 失败时尝试清理 .tmp，原文件保持原样。
//
// 失败语义（Save / Load 都保证调用方状态不部分污染）：
//   * Save：所有失败路径下，目标文件 path 维持调用前的状态（成功 →
//     完整新文件；失败 → 原文件不变 / 不存在则仍不存在）；
//   * Load：所有失败路径下，World 维持调用前的状态。Load 内部跟踪本
//     次 call 创建的 entity，任何后续步骤失败 → 整体回滚（DestroyEntity
//     全部）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>

#include <string_view>

namespace Orange::Engine
{

class World;

}  // namespace Orange::Engine

namespace Orange::Engine::Save
{

class SaveGameRegistry;

class ORANGE_ENGINE_API SaveGameSystem
{
public:
    // registry 必须在 SaveGameSystem 整个生命周期内保持有效——注册表
    // 的所有权 / 生命周期由调用方管理（典型方案：与 SaveGameSystem 同
    // 域的全局对象，启动期注册一次后只读）。
    explicit SaveGameSystem(const SaveGameRegistry& registry) noexcept;

    SaveGameSystem(const SaveGameSystem&)            = delete;
    SaveGameSystem& operator=(const SaveGameSystem&) = delete;

    // 把 `world` 中所有挂 `SaveableComponent` 的 entity 写到 `path`。
    //
    // 失败语义：
    //   * 路径无法创建 / IO 错误      → IoError（原文件不变）
    //   * 注册侧 callback 异常         → 不捕获（与 Core::Serialization
    //                                    其它路径一致，让进程级处理器
    //                                    决定）
    //   * 其余路径全部 → Ok，目标文件被原子替换为新内容
    Result<void, ResultCode> Save(const World& world, std::string_view path) const;

    // 从 `path` 读取存档，把 entity + component 追加到 `world`。
    //
    // 不清空 world——若调用方想"完全替换当前进度"，先构造一个新 World
    // 再合并。
    //
    // 失败语义（World 不被部分修改）：
    //   * 文件不存在 / 读取失败                  → IoError
    //   * header magic / 版本 / 长度不匹配       → IoError
    //   * payload CRC 不匹配                     → InvalidArgument
    //   * JSON 解析失败                          → InvalidArgument
    //   * 顶层 schemaVersion 不兼容              → SchemaMismatch
    //   * 任意 component 的 schemaVersion 不兼容 → SchemaMismatch
    //   * 注册侧 read 返回 false                 → InvalidArgument
    //   * 必填字段缺失 / 类型错                  → InvalidArgument
    //
    // 未识别的 component 名（registry 没注册）→ silent skip，不视为
    // fatal（forward-compat：旧引擎读新存档不崩）。
    Result<void, ResultCode> Load(std::string_view path, World& world) const;

private:
    const SaveGameRegistry& mRegistry;
};

}  // namespace Orange::Engine::Save

#endif  // ORANGE_ENGINE_SAVE_SAVE_GAME_SYSTEM_H
