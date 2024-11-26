#include <iostream>
#include "Orange.h"
class Sandbox : public Orange::Application
{
    public:
        Sandbox()
        {

        }
        
        ~Sandbox()
        {

        }
        
};

Orange::Application* Orange::CreateApplication()
{
    return new Sandbox();
}