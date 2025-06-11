#ifndef ORANGE_PROFILER_H
#define ORANGE_PROFILER_H

#define ORG_ENABLE_PROFILING !ORG_DIST

#if ORG_ENABLE_PROFILING 
#include <tracy/Tracy.hpp>
#endif

#if ORG_ENABLE_PROFILING
#define ORG_PROFILE_MARK_FRAME			FrameMark;
// NOTE(Peter): Use ORG_PROFILE_FUNC ONLY at the top of a function
//				Use ORG_PROFILE_SCOPE / ORG_PROFILE_SCOPE_DYNAMIC for an inner scope
#define ORG_PROFILE_FUNC(...)			ZoneScoped##__VA_OPT__(N(__VA_ARGS__))
#define ORG_PROFILE_SCOPE(...)			ORG_PROFILE_FUNC(__VA_ARGS__)
#define ORG_PROFILE_SCOPE_DYNAMIC(NAME)  ZoneScoped; ZoneName(NAME, strlen(NAME))
#define ORG_PROFILE_THREAD(...)          tracy::SetThreadName(__VA_ARGS__)
#else
#define ORG_PROFILE_MARK_FRAME
#define ORG_PROFILE_FUNC(...)
#define ORG_PROFILE_SCOPE(...)
#define ORG_PROFILE_SCOPE_DYNAMIC(NAME)
#define ORG_PROFILE_THREAD(...)
#endif

#endif // ORANGE_PROFILER_H