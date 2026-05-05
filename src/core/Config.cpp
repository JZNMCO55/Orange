// ---------------------------------------------------------------------------
// Phase 1 / Task 05 — Core::Config implementation
//
// Config wraps a JsonReader internally — the reader already handles the
// slashed-path traversal and missing-key fallback semantics, so Config
// only needs to express the read-only public surface. ConfigLoader is the
// construction funnel.
// ---------------------------------------------------------------------------

#include "orange/engine/core/Config.h"

#include <utility>

namespace Orange::Engine
{

struct Config::Impl
{
    JsonReader reader;
    bool       loaded{false};
};

Config::Config() : mpImpl(std::make_unique<Impl>()) {}

Config::Config(std::unique_ptr<Impl> impl) noexcept : mpImpl(std::move(impl)) {}

Config::Config(Config&&) noexcept = default;
Config& Config::operator=(Config&&) noexcept = default;
Config::~Config() = default;

bool Config::GetBool(std::string_view path, bool defaultValue) const
{
    return mpImpl->reader.GetBool(path, defaultValue);
}

std::int64_t Config::GetInt(std::string_view path, std::int64_t defaultValue) const
{
    return mpImpl->reader.GetInt(path, defaultValue);
}

double Config::GetFloat(std::string_view path, double defaultValue) const
{
    return mpImpl->reader.GetFloat(path, defaultValue);
}

std::string Config::GetString(std::string_view path, std::string defaultValue) const
{
    return mpImpl->reader.GetString(path, std::move(defaultValue));
}

glm::vec2 Config::GetVec2(std::string_view path, glm::vec2 defaultValue) const
{
    float values[2]{defaultValue.x, defaultValue.y};
    if (mpImpl->reader.ReadFloatArray(path, values, 2))
    {
        return {values[0], values[1]};
    }
    return defaultValue;
}

glm::vec3 Config::GetVec3(std::string_view path, glm::vec3 defaultValue) const
{
    float values[3]{defaultValue.x, defaultValue.y, defaultValue.z};
    if (mpImpl->reader.ReadFloatArray(path, values, 3))
    {
        return {values[0], values[1], values[2]};
    }
    return defaultValue;
}

glm::vec4 Config::GetVec4(std::string_view path, glm::vec4 defaultValue) const
{
    float values[4]{defaultValue.x, defaultValue.y, defaultValue.z, defaultValue.w};
    if (mpImpl->reader.ReadFloatArray(path, values, 4))
    {
        return {values[0], values[1], values[2], values[3]};
    }
    return defaultValue;
}

bool Config::Has(std::string_view path) const
{
    return mpImpl->reader.Has(path);
}

// ---------------------------------------------------------------------------
// ConfigLoader
// ---------------------------------------------------------------------------

Config ConfigLoader::Empty()
{
    return Config{};
}

Result<Config, ParseError> ConfigLoader::LoadFromFile(std::string_view path)
{
    auto readerResult = JsonReader::FromFile(path);
    if (readerResult.IsErr())
    {
        return readerResult.Error();
    }
    auto impl    = std::make_unique<Config::Impl>();
    impl->reader = std::move(readerResult.Value());
    impl->loaded = true;
    return Config{std::move(impl)};
}

Result<Config, ParseError> ConfigLoader::LoadFromString(std::string_view text)
{
    auto readerResult = JsonReader::FromString(text);
    if (readerResult.IsErr())
    {
        return readerResult.Error();
    }
    auto impl    = std::make_unique<Config::Impl>();
    impl->reader = std::move(readerResult.Value());
    impl->loaded = true;
    return Config{std::move(impl)};
}

}  // namespace Orange::Engine
