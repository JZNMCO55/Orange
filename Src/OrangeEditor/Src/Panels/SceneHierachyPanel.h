#ifndef SCENE_HIERARCHY_PANEL_H
#define SCENE_HIERARCHY_PANEL_H

namespace Orange
{
    class Scene;
    class Entity;
    class SceneHierachyPanel
    {
    public:
        SceneHierachyPanel()= default;
        SceneHierachyPanel(const Ref<Scene>& scene);
        ~SceneHierachyPanel();

        void SetContext(const Ref<Scene>& scene);

        void OnImGuiRender();
    private:
        void DrawEntityNode(Entity entity);
    private:
        Ref<Scene> mpContext;
        Entity mSelectionContext;
    };
}

#endif // !
