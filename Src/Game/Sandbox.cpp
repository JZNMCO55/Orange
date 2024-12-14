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
        //std::cout << "ExampleLayer::Update" << std::endl;
    }

    void OnEvent(Orange::Event& event) override
    {
        //std::cout << "ExampleLayer::Event" << std::endl;
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