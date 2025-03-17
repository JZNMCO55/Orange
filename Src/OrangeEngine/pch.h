#ifndef PCH_H
#define PCH_H

#include <iostream>
#include <fstream>
#include <memory>
#include <sstream>
#include <filesystem>

#include <vector>
#include <array>
#include <unordered_map>
#include <functional>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include "entt/entt.hpp"

#include "Log.h"
#include "Debug/Instrumentor.h"

#ifdef ORANGE_ENABLE_ASSERTS
#define ORANGE_ASSERT(x, ...) { if(!(x)) { CLIENT_LOG_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#define ORANGE_CORE_ASSERT(x, ...) { if(!(x)) { ORANGE_LOG_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
#define ORANGE_ASSERT(x, ...)
#define ORANGE_CORE_ASSERT(x, ...)
#endif //ORANGE_ENABLE_ASSERTS

#endif //PCH_H