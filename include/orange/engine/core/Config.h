#ifndef ORANGE_ENGINE_CORE_CONFIG_H
#define ORANGE_ENGINE_CORE_CONFIG_H

// ---------------------------------------------------------------------------
// Phase 1 / Task 05 — Core::Config
//
// Read-only, immutable key-value snapshot loaded from a JSON file at app
// boot. Keys are slash-separated paths matching JsonReader's syntax (e.g.
// "window/size/x", "renderer/vsync"). Missing-key reads return the
// caller-provided default; the Config never throws and never returns an
// error from a getter — booting on a partial / absent config file is a
// supported fallback path.
//
// `ConfigLoader` is the only construction surface. The Config itself
// can't be mutated after load — modules that need runtime overrides do so
// via their own settings / debug UI, not by reaching back into Config.
// ---------------------------------------------------------------------------

#include <orange/engine/OrangeEngineExport.h>
#include <orange/engine/core/Result.h>
#include <orange/engine/core/Serialization.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Orange::Engine
{

class ORANGE_ENGINE_API Config
{
public:
    Config();
    Config(Config&&) noexcept;
    Config& operator=(Config&&) noexcept;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    ~Config();

    bool         GetBool(std::string_view path, bool defaultValue) const;
    std::int64_t GetInt(std::string_view path, std::int64_t defaultValue) const;
    double       GetFloat(std::string_view path, double defaultValue) const;
    std::string  GetString(std::string_view path, std::string defaultValue) const;

    glm::vec2 GetVec2(std::string_view path, glm::vec2 defaultValue) const;
    glm::vec3 GetVec3(std::string_view path, glm::vec3 defaultValue) const;
    glm::vec4 GetVec4(std::string_view path, glm::vec4 defaultValue) const;

    bool Has(std::string_view path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> mpImpl;

    friend class ConfigLoader;
    explicit Config(std::unique_ptr<Impl> impl) noexcept;
};

class ORANGE_ENGINE_API ConfigLoader
{
public:
    // Returns an empty Config — every getter falls back to the
    // caller-provided default. Useful when no config file ships and
    // every value comes from compile-time defaults.
    static Config Empty();

    static Result<Config, ParseError> LoadFromFile(std::string_view path);
    static Result<Config, ParseError> LoadFromString(std::string_view text);
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_CORE_CONFIG_H
