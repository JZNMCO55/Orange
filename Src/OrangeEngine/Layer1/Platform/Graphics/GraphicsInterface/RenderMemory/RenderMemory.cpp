/**
 * @file RenderMemory.cpp
 * @brief 渲染内存管理实现
 */

#include "RenderMemory.h"
#include "../RenderInterface/IRenderDevice.h"
#include <iostream>

namespace Orange
{
    namespace Graphics
    {
        // 全局内存管理器实例
        static IMemoryManager *g_memoryManager = nullptr;

        IMemoryManager *CreateMemoryManager(IRenderDevice *device)
        {
            if (!device)
            {
                std::cerr << "CreateMemoryManager: 设备为空" << std::endl;
                return nullptr;
            }

            // TODO: 根据设备类型创建相应的内存管理器
            // 目前返回空指针，需要后续实现
            std::cout << "CreateMemoryManager: 暂未实现，返回空指针" << std::endl;
            return nullptr;
        }

        void DestroyMemoryManager(IMemoryManager *manager)
        {
            if (manager)
            {
                delete manager;
                std::cout << "DestroyMemoryManager: 内存管理器已销毁" << std::endl;
            }
        }

        bool InitializeMemorySystem(IRenderDevice *device)
        {
            if (!device)
            {
                std::cerr << "InitializeMemorySystem: 设备为空" << std::endl;
                return false;
            }

            if (g_memoryManager)
            {
                std::cerr << "InitializeMemorySystem: 内存系统已初始化" << std::endl;
                return true;
            }

            g_memoryManager = CreateMemoryManager(device);
            if (!g_memoryManager)
            {
                std::cerr << "InitializeMemorySystem: 创建内存管理器失败" << std::endl;
                return false;
            }

            std::cout << "InitializeMemorySystem: 内存系统初始化成功" << std::endl;
            return true;
        }

        void ShutdownMemorySystem(IRenderDevice *device)
        {
            if (g_memoryManager)
            {
                DestroyMemoryManager(g_memoryManager);
                g_memoryManager = nullptr;
                std::cout << "ShutdownMemorySystem: 内存系统已关闭" << std::endl;
            }
        }

        IMemoryManager *GetMemoryManager()
        {
            return g_memoryManager;
        }

    } // namespace Graphics
} // namespace Orange