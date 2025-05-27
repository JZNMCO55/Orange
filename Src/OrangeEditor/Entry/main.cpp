#include <iostream>
#include "Layer1/Core/Application/Application.h"

int main(int argc, char *argv[])
{
    try
    {
        // 创建应用程序实例
        Orange::Core::Application app;

        // 运行应用程序
        app.Run();

        std::cout << "Application finished successfully." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Application error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}