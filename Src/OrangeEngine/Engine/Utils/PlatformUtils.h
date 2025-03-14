#ifndef PLATFORM_UTILS_H
#define PLATFORM_UTILS_H

#include <string>

namespace Orange
{
	class FileDialog
	{
	public:
        // The
		static std::string OpenFile(const char* filter);
        static std::string SaveFile(const char* filter);
    };
}

#endif // PLATFORM_UTILS_H