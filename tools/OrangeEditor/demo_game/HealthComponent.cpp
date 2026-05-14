#include "HealthComponent.h"
#include "../schema/ComponentSchemaRegistry.h"

#include <orange/engine/scene/World.h>

#include <cstdint>
#include <string>

namespace DemoGame
{

namespace
{

const Orange::Engine::SchemaVersion kHealthSchemaVersion{"demo_game/HealthComponent", 1, 0};

// componentPath + "/" + key 拼接，例如 "components/Health" + "hp" → "components/Health/hp"
std::string Join(std::string_view base, std::string_view key)
{
    std::string s;
    s.reserve(base.size() + 1 + key.size());
    s.append(base);
    s += '/';
    s.append(key);
    return s;
}

bool HealthHas(const Orange::Engine::World& world, Orange::Engine::Entity entity)
{
    return world.HasComponent<HealthComponent>(entity);
}

void HealthWrite(Orange::Engine::JsonWriter&              writer,
                 std::string_view                         componentPath,
                 Orange::Engine::Entity                   entity,
                 const Orange::Engine::Scene::SaveContext& ctx)
{
    const auto* c = ctx.world.GetComponent<HealthComponent>(entity);
    if (c == nullptr) { return; }

    writer.WriteSchemaVersion(Join(componentPath, "schema"), kHealthSchemaVersion);
    writer.WriteInt(Join(componentPath, "hp"),    static_cast<std::int64_t>(c->hp));
    writer.WriteInt(Join(componentPath, "maxHp"), static_cast<std::int64_t>(c->maxHp));
}

bool HealthRead(const Orange::Engine::JsonReader&          reader,
                std::string_view                           componentPath,
                Orange::Engine::Entity                     entity,
                const Orange::Engine::Scene::LoadContext&  ctx)
{
    auto svResult = reader.ReadSchemaVersion(Join(componentPath, "schema"));
    if (svResult.IsErr()) { return false; }
    if (!kHealthSchemaVersion.CanRead(svResult.Value())) { return false; }

    HealthComponent c{};
    std::int64_t hp    = static_cast<std::int64_t>(c.hp);
    std::int64_t maxHp = static_cast<std::int64_t>(c.maxHp);
    reader.ReadInt(Join(componentPath, "hp"),    hp);
    reader.ReadInt(Join(componentPath, "maxHp"), maxHp);
    c.hp    = static_cast<int>(hp);
    c.maxHp = static_cast<int>(maxHp);

    ctx.world.AddComponent<HealthComponent>(entity, c);
    return true;
}

}  // namespace

void RegisterHealthComponentSchema()
{
    using namespace Orange::Editor::Schema;

    ComponentSchemaBuilder<HealthComponent>("HealthComponent", "Health")
        .Field<&HealthComponent::hp>("hp", "HP")
        .Field<&HealthComponent::maxHp>("maxHp", "Max HP")
        .Addable()
        .Removable()
        .Register();
}

const Orange::Engine::Scene::ComponentSerializerEntry& GetHealthSerializerEntry()
{
    using Orange::Engine::Scene::ComponentKind;
    using Orange::Engine::Scene::ComponentSerializerEntry;
    static const ComponentSerializerEntry kEntry{
        .name  = "Health",
        .kind  = ComponentKind::PureData,
        .Has   = HealthHas,
        .Write = HealthWrite,
        .Read  = HealthRead,
    };
    return kEntry;
}

}  // namespace DemoGame
