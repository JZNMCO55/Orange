#ifndef PCH_H
#define PCH_H

#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <functional>
#include <memory>
#include <sstream>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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