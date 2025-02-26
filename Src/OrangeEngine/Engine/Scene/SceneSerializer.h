#ifndef SCENE_SERIALIZER_H
#define SCENE_SERIALIZER_H

#include "OrangeExport.h"

namespace Orange
{
    class Scene;
    class ORANGE_API SceneSerializer
    {
	public:
		SceneSerializer(const std::weak_ptr<Scene>& scene);

		void Serialize(const std::string& filepath);
		void SerializeRuntime(const std::string& filepath);

		bool Deserialize(const std::string& filepath);
		bool DeserializeRuntime(const std::string& filepath);
	private:
		std::weak_ptr<Scene> mpScene;
	};
}

#endif // SCENE_SERIALIZER_H