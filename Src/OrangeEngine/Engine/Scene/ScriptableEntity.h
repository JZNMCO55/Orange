#ifndef SCRIPTABLE_ENTITY_H
#define SCRIPTABLE_ENTITY_H

#include "OrangeExport.h"
#include "Entity.h"
namespace Orange
{
    class ORANGE_API ScriptableEntity
    {
    public:
        template<typename T>
        T& GetComponent()
        {
            return mEntity.GetComponent<T>();
        }

        virtual ~ScriptableEntity() {}

    protected:
        virtual void OnCreate() {}
        virtual void OnDestroy() {}
        virtual void OnUpdate(float deltaTime) {}

    protected:
        Entity mEntity;
        friend class Scene;
    };
}
#endif

