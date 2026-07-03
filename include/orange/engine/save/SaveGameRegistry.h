#ifndef ORANGE_ENGINE_SAVE_SAVE_GAME_REGISTRY_H
#define ORANGE_ENGINE_SAVE_SAVE_GAME_REGISTRY_H

// ---------------------------------------------------------------------------
// SaveGameRegistry —— "玩家进度"存档子系统的 component 注册表。
//
// 与 Scene 序列化（src/scene/ComponentSerializers.h，引擎内部固定调度
// 表）刻意互不重叠：
//   * Scene 持久化的是 **content**（关卡 / 美术 / 设计师产出），跟随引
//     擎 + 游戏版本一起发布，schema 由引擎写死，第三方 component 不进
//     scene。
//   * SaveGame 持久化的是 **player state**（玩家在游戏过程中产生），必
//     须穿越引擎升级；schema 由游戏侧自己声明。引擎只提供注册容器与
//     type-erased 的调度入口，**不内置**任何 component 注册。
//
// 设计要点：
//   * "完全手写反射" —— 游戏侧为每个可入存档的 component 类型 TComp 手
//     写两个 callback：
//
//         void write(JsonWriter&, path, const TComp&)
//         bool read (const JsonReader&, path, TComp&)
//
//     再调 `Register<TComp>(name, version, write, read)`。不
//     引入任何反射 / codegen（详见 CLAUDE.md "Serialization and reflection"）。
//
//   * 每条注册带独立 SchemaVersion —— 同一存档文件里不同 component 的
//     schema 各自演化。Save 写入时把 `name + version + payload` 三件套
//     一起落地；Load 时主流程用注册侧 version 与文件里 version 配对，
//     触发 migrator 或 fail-fast（major 不匹配）。
//
//   * 公共面只见 `JsonReader` / `JsonWriter` / `SchemaVersion`（来自
//     `orange/engine/core/Serialization.h` / `SchemaVersion.h`）—— 不暴
//     露 nlohmann::json，与项目级"序列化必须走 Core::Serialization"约
//     束一致。
//
//   * 注册表本身只做"有序存储 + 按名查询"，**不**知道哪些 entity 算"可
//     入存档"。该策略由 Save / Load 主流程决定（典型方案：
//     游戏侧给 entity 打一个 tag component，主流程 view 一遍）。注册表
//     与策略解耦，让后续各自演化时不用回头改这层。
//
//   * `SaveGameComponentEntry` 中 has/write/read 用 `std::function` 而
//     非裸函数指针：要把 `Register<TComp>` 拿到的 typed callbacks 包成
//     操作 `(World&, Entity)` 的 type-erased 形态，必须捕获原 callbacks，
//     裸函数指针无法承载。Save / Load 是低频路径（每存档一次），
//     std::function 的间接调用开销不进 hot path；换来公共面简洁。
//
// 典型使用：
//
//     struct PlayerInventory  // 游戏侧组件
//     {
//         std::int64_t goldCoins{0};
//         std::int64_t spiritShards{0};
//     };
//
//     SaveGameRegistry registry;
//     registry.Register<PlayerInventory>(
//         "PlayerInventory",
//         SchemaVersion{"game/PlayerInventory", 1, 0},
//         [](JsonWriter& w, std::string_view path, const PlayerInventory& c)
//         {
//             const std::string base(path);
//             w.WriteInt(base + "/goldCoins",    c.goldCoins);
//             w.WriteInt(base + "/spiritShards", c.spiritShards);
//         },
//         [](const JsonReader& r, std::string_view path, PlayerInventory& c) -> bool
//         {
//             const std::string base(path);
//             std::int64_t v = 0;
//             if (!r.ReadInt(base + "/goldCoins",    v)) { return false; }
//             c.goldCoins = v;
//             if (!r.ReadInt(base + "/spiritShards", v)) { return false; }
//             c.spiritShards = v;
//             return true;
//         });
//
//     // —— 此后 SaveGameSystem::Save / Load 走 registry.Entries()
//     //    去派发到具体 component。
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/core/SchemaVersion.h>
#include <orange/engine/scene/Entity.h>
#include <orange/engine/scene/World.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Orange::Engine
{

    class JsonReader;
    class JsonWriter;

} // namespace Orange::Engine

namespace Orange::Engine::Save
{

    // 单条注册项的 type-erased 形态 —— 注册时由模板把 typed callbacks 包
    // 装成操作 `(World&, Entity, ...)` 的统一形态；Save / Load
    // 主流程拿到 entry 后只需循环调用 has → write / read，无需再 dispatch
    // 到具体 TComp。
    //
    // 字段命名与 src/scene/ComponentSerializers.h 中的 ComponentSerializerEntry
    // 对齐（Has / Write / Read 三件套），让两层将来重构调度循环时形态一致。
    struct ORANGE_ENGINE_API SaveGameComponentEntry
    {
        // 落地到 JSON 的 component key（主流程典型把它接到
        // "entities/<n>/components/<name>" 后传给 Write/Read）。
        std::string name;

        // 该 component 自己的 schema 版本。每次字段增删 / 语义变化时游戏
        // 侧 bump 后再发版；旧存档加载时由 migrator hook 接管。
        SchemaVersion version;

        // entity 是否拥有该 component（Save 阶段判断"要不要写这一条"）。
        std::function<bool(const World&, Entity)> Has;

        // 在 `componentPath` 这个对象路径下写出 component 全部字段。
        std::function<void(JsonWriter&      writer,
                           std::string_view componentPath,
                           const World&     world,
                           Entity           entity)>
            Write;

        // 从 `componentPath` 读取并 attach 到 entity；返回 false 表示数据
        // 存在但格式坏（缺必填字段 / 类型错），调用方应将本次 Load 视为
        // fatal 整体回滚。"该 entity 没有这个 component"由调用方先用
        // `JsonReader::Has(componentPath)` 过滤——这里只处理"已经决定要读"
        // 的路径。
        std::function<bool(const JsonReader& reader,
                           std::string_view  componentPath,
                           World&            world,
                           Entity            entity)>
            Read;

        // 单条 schema 迁移：(from → to) 的 JSON 变换。SaveGameSystem
        // ::Load 在 per-component schema 不匹配时按"找 from == 当前版本的
        // migrator → 应用 → 把 dst dump+reparse 当下次输入"循环走链，直到
        // 到达 entry.version 或链断（→ SchemaMismatch）。
        //
        // fn 签名（4 参）：
        //   * src       —— 当前版本的 JsonReader
        //   * srcPath   —— src 中本 component 数据子树根路径
        //   * dst       —— 写入新版本数据的 JsonWriter（每次迁移一个 fresh
        //                  实例，由引擎内部分配）
        //   * dstPath   —— dst 中应写入的根路径（引擎传 "data"）
        //   返回 false 表示迁移过程中发现数据坏（缺必填字段等），整次 Load
        //   被视为 fatal 整体回滚。
        struct ORANGE_ENGINE_API Migrator
        {
            SchemaVersion from;
            SchemaVersion to;
            std::function<bool(const JsonReader& src,
                               std::string_view  srcPath,
                               JsonWriter&       dst,
                               std::string_view  dstPath)>
                fn;
        };

        // 该 component 的 migrator 链 —— 顺序无所谓（按 from 字段查表）。
        // 每个 migrator 跨一档 major bump；多个 migrator 串成 v1→v2→v3 链。
        std::vector<Migrator> migrators;
    };

    // SaveGameRegistry —— 容器 + 注册门面。Save / Load 主流程后续接
    // 入；当前仅交付注册 + 查询。
    //
    // 线程模型：注册阶段（启动期）单线程；运行期注册表只读，可任意线程
    // 并发查询。
    class ORANGE_ENGINE_API SaveGameRegistry
    {
    public:
        SaveGameRegistry()  = default;
        ~SaveGameRegistry() = default;

        SaveGameRegistry(const SaveGameRegistry&)            = delete;
        SaveGameRegistry& operator=(const SaveGameRegistry&) = delete;

        SaveGameRegistry(SaveGameRegistry&&) noexcept            = default;
        SaveGameRegistry& operator=(SaveGameRegistry&&) noexcept = default;

        // 游戏侧 typed callbacks 的签名。模板包装层会把它们 type-erase 成
        // SaveGameComponentEntry 中操作 `(World&, Entity, ...)` 的 std::function。
        template <typename TComp>
        using WriteFn = std::function<void(JsonWriter&      writer,
                                           std::string_view componentPath,
                                           const TComp&     component)>;

        template <typename TComp>
        using ReadFn = std::function<bool(const JsonReader& reader,
                                          std::string_view  componentPath,
                                          TComp&            outComponent)>;

        // 注册一个游戏侧 component 类型。失败语义：
        //   * `name` 为空                     → InvalidArgument
        //   * `version.IsValid()` 为 false    → InvalidArgument
        //   * `write` / `read` 为空           → InvalidArgument
        //   * 同名 component 已注册             → AlreadyExists
        //
        // 成功时把 typed callbacks 包装成 type-erased 的 entry 追加到内部
        // 表；Entries() 的相对顺序与注册顺序一致——Save 主流程直接按这个
        // 顺序写出，让 JSON 字段排列稳定（diff 友好）。
        template <typename TComp>
        Result<void, ResultCode> Register(std::string_view name,
                                          SchemaVersion    version,
                                          WriteFn<TComp>   write,
                                          ReadFn<TComp>    read);

        // 给已注册的 component 追加一条 schema migrator。
        // SaveGameSystem::Load 在 per-component schema 不匹配时按
        // (from→to) chain 走，直到到达 entry.version 或链断（→ SchemaMismatch）。
        //
        // 失败语义：
        //   * `componentName` 未注册                        → NotFound
        //   * `from` / `to` 的 IsValid() 为 false           → InvalidArgument
        //   * `from == to`                                  → InvalidArgument
        //   * `fn` 为空                                     → InvalidArgument
        //   * 同 component 同 from 已注册过 migrator        → AlreadyExists
        //
        // 注意：from / to 不强制同 namespace —— 允许跨 namespace 迁移
        // （component rename 场景），只要最后一档能命中 entry.version 即可。
        Result<void, ResultCode> RegisterMigrator(
            std::string_view componentName,
            SchemaVersion    from,
            SchemaVersion    to,
            std::function<bool(const JsonReader& src,
                               std::string_view  srcPath,
                               JsonWriter&       dst,
                               std::string_view  dstPath)>
                fn);

        // 按 name 查 entry。命中返回非 null 指针，未命中返回 nullptr。
        // Load 路径用它处理 "JSON 里出现了某个 component key 但
        // 注册表里没有" → 走 forward-compat：跳过 + warn。
        const SaveGameComponentEntry* Find(std::string_view name) const noexcept;

        // 枚举所有已注册的 entry，顺序 = 注册顺序。Save 路径直
        // 接按这个顺序对每个 entity 跑一遍 entries.Has → entries.Write，让
        // 落地的 JSON 字段排列稳定。
        const std::vector<SaveGameComponentEntry>& Entries() const noexcept { return mEntries; }

        std::size_t Size() const noexcept { return mEntries.size(); }
        bool        Empty() const noexcept { return mEntries.empty(); }

    private:
        // 注册时通过 `Register<TComp>` 模板内部调入；entry 已经 type-erase
        // 完成。把 string 验证 / 重名检查 / 表追加这些非模板逻辑挪到 .cpp
        // 避免 unordered_map / string 复制的成本被模板实例化放大。
        Result<void, ResultCode> RegisterErased(SaveGameComponentEntry entry);

        std::vector<SaveGameComponentEntry>          mEntries;
        std::unordered_map<std::string, std::size_t> mNameToIndex; // name → index in mEntries
    };

    // ---------------------------------------------------------------------------
    // 模板实现 —— 把 typed callbacks 包装成对 (World&, Entity) 操作的
    // std::function。需要 World / Entity 完整定义（已 #include World.h），
    // 因此放在头里。
    // ---------------------------------------------------------------------------

    template <typename TComp>
    Result<void, ResultCode> SaveGameRegistry::Register(std::string_view name,
                                                        SchemaVersion    version,
                                                        WriteFn<TComp>   write,
                                                        ReadFn<TComp>    read)
    {
        if (name.empty())
        {
            return ResultCode::InvalidArgument;
        }
        if (!version.IsValid())
        {
            return ResultCode::InvalidArgument;
        }
        if (!write || !read)
        {
            return ResultCode::InvalidArgument;
        }

        SaveGameComponentEntry entry{};
        entry.name    = std::string(name);
        entry.version = std::move(version);

        entry.Has = [](const World& world, Entity entity) -> bool
        {
            return world.HasComponent<TComp>(entity);
        };

        // typed callbacks 通过值捕获——`write` 为右值，`std::move` 一次到
        // 闭包，闭包再被 std::function 持有。被注册组件的 typed callbacks
        // 不会再被外部修改，captured copy 是 stable 的。
        entry.Write = [userWrite = std::move(write)](JsonWriter&      writer,
                                                     std::string_view componentPath,
                                                     const World&     world,
                                                     Entity           entity)
        {
            const TComp* comp = world.GetComponent<TComp>(entity);
            if (comp == nullptr)
            {
                // Has 已在 Save 主流程内被检查；走到这里说明并发修改 / 调
                // 用方未先过滤——按"无操作"处理，不写空对象避免污染 JSON。
                return;
            }
            userWrite(writer, componentPath, *comp);
        };

        entry.Read = [userRead = std::move(read)](const JsonReader& reader,
                                                  std::string_view  componentPath,
                                                  World&            world,
                                                  Entity            entity) -> bool
        {
            TComp comp{};
            if (!userRead(reader, componentPath, comp))
            {
                return false;
            }
            world.AddComponent<TComp>(entity, std::move(comp));
            return true;
        };

        return RegisterErased(std::move(entry));
    }

} // namespace Orange::Engine::Save

#endif // ORANGE_ENGINE_SAVE_SAVE_GAME_REGISTRY_H
