#include <iostream>
#include "Orange.h"
#include "pch.h"
#include "ImGui/ImGuiLayer.h"

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