#include <iostream>
#include "Orange.h"

class ExampleLayer : public Orange::Layer
{
public:
    ExampleLayer()
        : Layer("Example")
    {
    }
    ~ExampleLayer() {}

    void OnUpdate() override
    {

    }

    void OnEvent(Orange::Event& event) override
    {
        if (event.GetEventType() == Orange::EEventType::KeyPressed)
        {
            Orange::KeyPressedEvent& e = (Orange::KeyPressedEvent&)event;
            if (e.GetKeyCode() == ORG_KEY_TAB)
            {
                CLIENT_LOG_TRACE("Tab key is pressed(event)");
                CLIENT_LOG_TRACE("Key code: {}", e.GetKeyCode());
            }

        }
    }   
};
class Sandbox : public Orange::Application
{
    public:
        Sandbox()
        {
             PushLayer(new ExampleLayer());
             PushOverlay(new Orange::ImGuiLayer());
        }
        
        ~Sandbox()
        {

        }
        
};

Orange::Application* Orange::CreateApplication()
{
    return new Sandbox();
}