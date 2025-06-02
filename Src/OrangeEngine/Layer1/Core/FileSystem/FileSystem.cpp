#include "FileSystem.h"
#include <stdexcept>
#include <iostream>

namespace Orange::Core
{

    std::string FileSystem::ReadTextFile(const std::string &filename)
    {
        std::ifstream file(filename);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        std::string content;
        std::string line;
        while (std::getline(file, line))
        {
            content += line + "\n";
        }

        file.close();
        return content;
    }

    std::vector<uint8_t> FileSystem::ReadBinaryFile(const std::string &filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> buffer(fileSize);

        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
        file.close();

        return buffer;
    }

    std::vector<uint32_t> FileSystem::ReadBinaryFileAsUint32(const std::string &filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

        file.seekg(0);
        file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
        file.close();

        return buffer;
    }

    bool FileSystem::FileExists(const std::string &filename)
    {
        std::ifstream file(filename);
        return file.good();
    }

    std::string FileSystem::GetFileExtension(const std::string &filename)
    {
        size_t lastDot = filename.find_last_of('.');
        if (lastDot == std::string::npos)
        {
            return "";
        }
        return filename.substr(lastDot + 1);
    }

}