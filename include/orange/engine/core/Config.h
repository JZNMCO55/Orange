#ifndef ORANGE_ENGINE_CORE_CONFIG_H
#define ORANGE_ENGINE_CORE_CONFIG_H

// ---------------------------------------------------------------------------
// Core::Config —— app 启动时从 JSON 加载、之后只读不可变的 key-value
// snapshot。Key 是斜杠分隔的路径，与 JsonReader 的语法一致（如
// "window/size/x"、"renderer/vsync"）。Key 缺失时的读取直接回落到调用方
// 提供的默认值；Config 永远不抛异常、getter 也不返回错误——以"部分缺失
// / 完全缺失 config 文件"启动是受支持的兜底路径。
//
// `ConfigLoader` 是唯一的构造入口。Config 一旦加载完成就不可修改——若
// 模块需要运行时覆盖，应通过自身的 settings / debug UI 实现，不应
// "回写" Config。
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
    // 返回一个空 Config——任何 getter 都会回落到调用方提供的默认值。
    // 适用于不发布 config 文件、所有取值都来自编译期默认的场景。
    static Config Empty();

    static Result<Config, ParseError> LoadFromFile(std::string_view path);
    static Result<Config, ParseError> LoadFromString(std::string_view text);
};

}  // namespace Orange::Engine

#endif  // ORANGE_ENGINE_CORE_CONFIG_H
