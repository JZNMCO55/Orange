#ifndef PCH_H
#define PCH_H

#include <iostream>
#include <vector>
#include <functional>
#include <memory>
#include <sstream>

#include "Log.h"

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "ImGuiBackend/ImGuiOpengl/imgui_impl_opengl3.h"
#include "ImGuiBackend/ImGuiGlfw/imgui_impl_glfw.h"
#include "glm/glm.hpp"

#ifdef ORANGE_ENABLE_ASSERTS
#define ORANGE_ASSERT(x, ...) { if(!(x)) { CLIENT_LOG_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#define ORANGE_CORE_ASSERT(x, ...) { if(!(x)) { ORANGE_LOG_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
#define ORANGE_ASSERT(x, ...)
#define ORANGE_CORE_ASSERT(x, ...)
#endif //ORANGE_ENABLE_ASSERTS

#endif //PCH_H