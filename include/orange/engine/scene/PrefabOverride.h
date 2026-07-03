#ifndef ORANGE_ENGINE_SCENE_PREFAB_OVERRIDE_H
#define ORANGE_ENGINE_SCENE_PREFAB_OVERRIDE_H

// ---------------------------------------------------------------------------
// PrefabOverride —— prefab 实例↔模板的字段级 override diff + refresh 传播
// （决策中立）。
//
// 两块能力：
//   * **override diff（只读，CS1）**：给定一个 prefab 实例实体（带
//     PrefabInstanceComponent.templateEntityGuid）+ 它的模板，diff 出该实例相对
//     模板被改了哪些 component / 字段。返回**被 override 的字段路径集**（field 级
//     粒度），供蓝条标记 / revert / refresh 等上层消费。
//   * **refresh-from-template（传播动作，CS2）**：把实例实体的**未被 override
//     字段**从模板重拉（覆盖回模板值），override 字段保留。消费 CS1 的 diff。详见
//     下方 RefreshEntityFromTemplate / RefreshInstanceFromTemplate。
//
// 设计立场（c1-prefab-override-design.md §2 选项 A / §3 CS1）：
//   * **只读、不改任何存储、不改 scene schema**。override 在选项 A 下是派生
//     计算，不预判 C1.0 ADR 最终选存储模型 A 还是 B——两者都消费本 diff
//     （A 用它做蓝条/revert；B 也要它生成 delta）。故本模块是 C1 的决策中立
//     安全先行件。
//   * diff 复用现有序列化：把实例实体与模板实体的 components 各经已注册的
//     ComponentSerializer 写成 JSON，再结构化逐叶子路径比较。**不重写任何
//     component 的序列化逻辑**。
//
// 过滤"身份/链接"component（见 .cpp 的 IsIdentityComponent）：Guid /
// PrefabInstance / Hierarchy 本就该在实例与模板间不同（实例换了新 per-entity
// guid、新 instanceId、互引用指向各自 world 的实体），那些不是业务 override，
// 不报。本期只做"同 entity 字段值 override"，结构性 override（加/删 entity）
// 留后续（design §5 问题 6）。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/scene/Entity.h>

#include <string>
#include <string_view>
#include <vector>

namespace Orange::Engine
{
    class World;
}

namespace Orange::Engine::Asset
{
    class PrefabAsset;
}

namespace Orange::Engine::Scene
{

    struct PrefabInstanceComponent;

    // 一条被 override 的字段。
    //   * componentName —— scene JSON 里 "components/<name>" 这一级的 component 名
    //     （如 "Transform" / "Renderable"）。
    //   * fieldPath —— 该 component 内部相对 component 根的叶子路径（如 "position" /
    //     "position/0" / "materialInstanceId"）。按 JSON 结构枚举叶子，'/' 分隔，
    //     数组元素用数字下标段（与 Core::Serialization 的路径语法一致）。
    //
    // "被 override" = 叶子值在实例与模板间不同，**或**该叶子只在一侧存在（实例
    // 加了模板没有的字段 / 实例缺了模板有的字段，都算 override）。
    struct OverrideField
    {
        std::string componentName;
        std::string fieldPath;
    };

    // 底层入口：diff 两个 world 里的两个实体的 component 字段。
    //
    // instWorld/instEntity = 实例侧；tmplWorld/tmplEntity = 模板侧。返回 instEntity
    // 相对 tmplEntity 被 override 的字段集（过滤身份/链接 component 后）。两实体
    // 任一无效 → 返回空（不崩）。字段集顺序：按 component 名、再按叶子路径稳定排序，
    // 便于测试与上层稳定消费。
    ORANGE_ENGINE_API std::vector<OverrideField> ComputeEntityOverrides(
        const World& instWorld, Entity instEntity,
        const World& tmplWorld, Entity tmplEntity);

    // 便利入口：给定实例实体 + 它的模板 PrefabAsset，自动配对模板实体并 diff。
    //
    // 内部：LoadFromString(tmpl.TemplateBlob()) 到一个 scratch world → 取 instEntity
    // 的 PrefabInstanceComponent.templateEntityGuid → FindEntityByGuid(scratch, guid)
    // 找模板实体 → ComputeEntityOverrides。
    //
    // 失败 graceful（均返回空，不崩）：instEntity 无效 / 无 PrefabInstanceComponent /
    // templateEntityGuid 为空（旧数据） / 模板 blob 载入失败 / 模板里 guid 未命中。
    ORANGE_ENGINE_API std::vector<OverrideField> ComputeInstanceOverrides(
        const World& instWorld, Entity instEntity, const Asset::PrefabAsset& tmpl);

    // ---------------------------------------------------------------------------
    // refresh-from-template —— 选项 A 下的传播动作（design §3 CS2）。
    //
    // 把实例实体的**未被 override 的字段**从（演进后的）模板重拉（覆盖回模板值），
    // **被 override 的字段保留实例值**。身份/链接 component（Guid / PrefabInstance /
    // Hierarchy）**完全不动**（与 CS1 的 IsIdentityComponent 同一过滤面）。
    //
    // 为什么是"三方 merge"而非"实例 vs 单模板的二方 diff"——关键设计点：
    //   refresh 的核心价值是"模板演进后，把实例里**用户没手改过**的字段更新成新模板值"。
    //   但纯"实例 vs 当前模板"的值 diff **分不清**两种"不相等"：
    //     (a) 用户在实例上手改了某字段（真 override，应保留）；
    //     (b) 模板演进改了某字段、实例仍是 bake 的旧值（非 override，应更新）。
    //   二者在值 diff 里都表现为"实例 != 当前模板"。若按二方 diff 把所有"不等"都当
    //   override 保留，refresh 对值就是 no-op（什么都更新不了），CS2 失去意义。
    //   故 CS2 必须三方：
    //     * base （instance bake 时的模板快照）—— 判定"真 override"的基准。
    //     * theirs（演进后的新模板）—— 未 override 字段的新值来源。
    //     * mine （实例当前值）。
    //   规则（每个非身份 component 的每个叶子）：
    //     * mine != base → 用户真改过（真 override）→ 保留 mine。
    //     * mine == base 且 base != theirs → 模板演进 → 取 theirs（更新）。
    //     * mine == base == theirs → 无变化 → 取 theirs（= base，幂等）。
    //   "真 override 集" = CS1 的 diff(mine, base)。
    //
    // 实现（JSON merge，复用 CS1 diff + 序列化 Read/Write）：
    //   ① 序列化 theirs（新模板）entity 的非身份 component → JSON 结构（含正确数组形态）；
    //   ② 序列化 mine（实例）+ base（旧模板）entity → JSON；
    //   ③ CS1 diff(mine, base) → 真 override 叶子集；
    //   ④ 以 theirs JSON 为底，把真 override 叶子用 mine 的值就地覆盖回去 → merged JSON M；
    //   ⑤ 把 M 的各 component 经已注册 ComponentSerializer 的 Read 反序列化**写回实例
    //      实体**（AddComponent 走 emplace_or_replace，整段替换该 component）。
    // 这样未 override 字段=新模板值、override 字段=实例值；身份 component 不在 M 里故不动。
    //
    // base == theirs（未演进/同一模板）退化为"把实例的非 override 字段拉回模板值"——
    // 即"丢弃实例相对模板的非真 override 漂移"，幂等无害。
    //
    // 决策中立：CS2 是运行时动作，不改 scene 存储、无 schema bump（option A 下 refresh
    // 是动作非存储变更），不预判 C1.0 ADR。
    //
    // asset-ref 局限：mesh / material 等 AssetHandle 字段沿用 CS1 的对称退化（不传
    // registry 时两侧写空串）；对"未改 mesh/material"的实例 refresh 无害，但本入口
    // 不重设这类资产引用（与 CS1 的已知局限一致，见 .cpp 注释）。

    // 底层入口：三方 merge。instEntity（mine）相对 baseEntity（bake 时模板）的真
    // override 字段保留实例值，其余字段从 newEntity（演进后模板）重拉。
    //   * instWorld/instEntity —— 实例侧（被写回）。
    //   * baseWorld/baseEntity —— 实例 bake 时的模板快照（override 判定基准）。
    //   * newWorld/newEntity   —— 演进后的新模板（未 override 字段的新值来源）。
    // base 与 new 可指向同一 world/entity（未演进时）。任一 entity 无效 → no-op 返回
    // false。成功（含"无 override 全量回新模板"与"有 override 部分保留"）→ true。
    ORANGE_ENGINE_API bool RefreshEntityFromTemplate(
        World& instWorld, Entity instEntity,
        const World& baseWorld, Entity baseEntity,
        const World& newWorld, Entity newEntity);

    // 便利入口：给定实例实体 + base 模板（bake 时） + new 模板（演进后）两个
    // PrefabAsset，经 templateEntityGuid 自动在两个模板 blob 里配对模板实体（同
    // ComputeInstanceOverrides 的配对逻辑）后做三方 refresh。
    //
    // 单模板便利重载：base 与 new 是同一 PrefabAsset（未演进）→ refresh 退化为"丢弃
    // 实例的非真 override 漂移、拉回模板值"，幂等无害。
    //
    // 失败 graceful（均 no-op 返回 false，不崩）：instEntity 无效 / 无
    // PrefabInstanceComponent / templateEntityGuid 为空（旧数据） / 任一模板 blob 载入
    // 失败 / 任一模板里 guid 未命中。成功 → true。
    ORANGE_ENGINE_API bool RefreshInstanceFromTemplate(
        World& instWorld, Entity instEntity,
        const Asset::PrefabAsset& baseTmpl, const Asset::PrefabAsset& newTmpl);

    ORANGE_ENGINE_API bool RefreshInstanceFromTemplate(
        World& instWorld, Entity instEntity, const Asset::PrefabAsset& tmpl);

    // ---------------------------------------------------------------------------
    // 持久化 overriddenPaths —— 显式 override 集的记录 / 查询 / 移除（C1 / ADR-019 问题 4）。
    //
    // PrefabInstanceComponent.overriddenPaths 是**持久化的显式 override 字段集**：用户在
    // 实例上手改某字段时记下该 path（编辑器命令栈钩子调 RecordOverridePath），作 refresh
    // 的显式 override 集 + 蓝条 / revert 的数据源。每项是扁平串 "componentName/fieldPath"
    // （componentName + '/' + OverrideField.fieldPath；fieldPath 为空时即 componentName）。
    //
    // 与运行时 diff（ComputeInstanceOverrides）的关系：diff 是"每次从模板推断 override"；
    // 本组是"显式记录的 override 集"。有了持久化集后，RefreshInstanceWithRecordedOverrides
    // 不再需要 bake 时 base 快照来区分"用户手改" vs "模板演进"——集合本身就是真 override。
    // ---------------------------------------------------------------------------

    // 把 "componentName/fieldPath" 拼成 overriddenPaths 里的扁平串（fieldPath 为空 →
    // 仅 componentName）。RecordOverridePath / IsPathOverridden / ClearOverridePath 与
    // 序列化共用同一格式。
    ORANGE_ENGINE_API std::string MakeOverridePath(std::string_view componentName,
                                                   std::string_view fieldPath);

    // 记录一条 override path 到 link.overriddenPaths（dedup：已存在则不重复加）。
    // 格式经 MakeOverridePath 规范化。返回 true 表示新增了一条；false 表示已存在（no-op）。
    ORANGE_ENGINE_API bool RecordOverridePath(PrefabInstanceComponent& link,
                                              std::string_view         componentName,
                                              std::string_view         fieldPath);

    // 查询某 path 是否已在 link.overriddenPaths 里（蓝条标记用）。
    ORANGE_ENGINE_API bool IsPathOverridden(const PrefabInstanceComponent& link,
                                            std::string_view               componentName,
                                            std::string_view               fieldPath);

    // 从 link.overriddenPaths 移除一条 override path（revert 单字段用）。返回 true 表示
    // 确实移除了一条；false 表示原本就不在（no-op）。
    ORANGE_ENGINE_API bool ClearOverridePath(PrefabInstanceComponent& link,
                                             std::string_view         componentName,
                                             std::string_view         fieldPath);

    // ---------------------------------------------------------------------------
    // RefreshInstanceWithRecordedOverrides —— 用持久化 overriddenPaths 当显式 override 集
    // 做 refresh（C1 / ADR-019 问题 4）。
    //
    // 比 CS2 的三方 merge 更干净：有了持久化的显式 override 集，就**不需要** bake 时的
    // base 模板快照来区分"用户手改" vs "模板演进"——overriddenPaths 直接给出真 override
    // 叶子集。规则（每个非身份 component 的每个叶子）：
    //   * 该叶子在 overriddenPaths 里 → 保留实例值（mine）。
    //   * 否则 → 取模板值（theirs）。
    // 即：序列化模板（theirs）为底 + overriddenPaths 指定的叶子用实例（mine）覆盖 → 写回。
    // 复用 CS2 的 merge 机器（BuildMergedComponentJson / WriteBackMergedComponents），只把
    // override 叶子集的来源从"CS1 diff(mine, base)"换成"实例 PrefabInstanceComponent.
    // overriddenPaths"。
    //
    // 配对走 templateEntityGuid + FindEntityByGuid（同 CS2 / ComputeInstanceOverrides）。
    //
    // 空 overriddenPaths → 全取模板值（实例完全跟随模板，无 override 保留）。
    //
    // 失败 graceful（均 no-op 返回 false，不崩）：instEntity 无效 / 无
    // PrefabInstanceComponent / templateEntityGuid 为空（旧数据） / 模板 blob 载入失败 /
    // 模板里 guid 未命中。成功 → true。
    ORANGE_ENGINE_API bool RefreshInstanceWithRecordedOverrides(
        World& instWorld, Entity instEntity, const Asset::PrefabAsset& tmpl);

    // ---------------------------------------------------------------------------
    // RevertEntityOverridePath —— 把实例某个 field path 回退到模板值（C1.3）。
    //
    // = CS2 三方 merge 机器的"单 leaf 版"：对该 path 用模板**当前值**覆盖实例字段，
    // 其余字段（含同 component 内其它 override）一律不动。语义上是"撤销这一条 override"。
    //
    // 实现复用 CS2 的 merge 机器（BuildMergedComponentJson / WriteBackMergedComponents），
    // 但底与 CS2 相反——以**实例**当前 JSON 为底（保留实例所有字段），仅把 path 这**一个
    // 叶子**用**模板**值覆盖，然后只把该 path 所属 component 写回实例（不碰其余 component）。
    // 这样精确达成"只回退这一条字段、同 component 其它 override 原样保留"，无副作用。
    // 写回后调 ClearOverridePath 清掉该 path 的 override 记录；其余 override 记录不动。
    //
    // 入参风格对偶 RefreshEntityFromTemplate（要 templateWorld/templateEntity +
    // instanceWorld/instanceEntity + path）：
    //   * instWorld/instEntity —— 实例侧（被写回 + 被 ClearOverridePath）。
    //   * tmplWorld/tmplEntity —— 模板侧（path 回退值的来源；通常由调用方经
    //     LoadFromString(tmpl.TemplateBlob()) + FindEntityByGuid(templateEntityGuid) 取得）。
    //   * componentName/fieldPath —— 要回退的那一条 override 字段（OverrideField 两段）。
    //
    // 失败 graceful（均 no-op 返回 false，不崩）：任一 entity 无效 / 实例无
    // PrefabInstanceComponent / 序列化内部错误。成功（含"该 path 原本就没被 override"
    // 也照常用模板值覆盖并尝试 Clear）→ true。
    ORANGE_ENGINE_API bool RevertEntityOverridePath(
        World& instWorld, Entity instEntity,
        const World& tmplWorld, Entity tmplEntity,
        std::string_view componentName, std::string_view fieldPath);

    // 便利入口：给定实例实体 + 它的模板 PrefabAsset，经 templateEntityGuid 自动配对
    // 模板实体后回退单条 override path（同 ComputeInstanceOverrides / CS2 的配对逻辑）。
    //
    // 失败 graceful（均 no-op 返回 false，不崩）：instEntity 无效 / 无
    // PrefabInstanceComponent / templateEntityGuid 为空（旧数据） / 模板 blob 载入失败 /
    // 模板里 guid 未命中。成功 → true。
    ORANGE_ENGINE_API bool RevertInstanceOverridePath(
        World& instWorld, Entity instEntity, const Asset::PrefabAsset& tmpl,
        std::string_view componentName, std::string_view fieldPath);

    // ---------------------------------------------------------------------------
    // ApplyInstanceToTemplate —— 以实例当前态重建模板 blob（C1.3）。
    //
    // "apply" = 把用户在实例上的修改回灌成新模板：序列化实例子树为新的模板 blob 字符串
    // （供调用方写回 .prefab 文件）。关键不变量：**实例实体的 guid 必须映回它锚定的
    // templateEntityGuid 再写出**——否则新模板里的实体 guid 变成实例 guid，其它实例的
    // A2.2 锚定（PrefabInstanceComponent.templateEntityGuid → 模板内 guid）全部断链。
    //
    // 做法（不污染原实例 world）：
    //   ① SaveSubtreeToString(instWorld, {instEntity}) → 实例子树 blob；
    //   ② LoadFromString 到 scratch world（得到含 GuidComponent=实例 guid +
    //      PrefabInstanceComponent.templateEntityGuid 的完整克隆）；
    //   ③ 遍历 scratch 每个实体：把 GuidComponent.guid 改成它的 templateEntityGuid
    //      （锚有效时）+ 移除 PrefabInstanceComponent（模板里不该有链接组件）；
    //   ④ SaveSubtreeToString(scratch, {scratchRoot}) → 新模板 blob 返回。
    //      由于 Hierarchy / 引用走 GuidStringOf 读当前 GuidComponent.guid，③ 改 guid
    //      后 ④ 写出的引用自动指向模板侧稳定 guid，无需手工重写引用。
    //
    // 同时清空本实例的 overriddenPaths（apply 后实例 == 模板，无 override 残留）。
    //
    // 失败 graceful（返回空 optional / 空字符串语义见 Result）：instEntity 无效 /
    // 子树序列化失败 / 重载入 scratch 失败 / scratch 内找不到根。成功 → 返回新模板 blob。
    ORANGE_ENGINE_API Result<std::string, ResultCode> ApplyInstanceToTemplate(
        World& instWorld, Entity instEntity);

} // namespace Orange::Engine::Scene

#endif // ORANGE_ENGINE_SCENE_PREFAB_OVERRIDE_H
