#ifndef ORANGE_RENDERCORE_MESHGENERATOR_H
#define ORANGE_RENDERCORE_MESHGENERATOR_H

#include "Mesh.h"
#include <memory>

namespace Orange
{
    namespace RenderCore
    {
        /**
         * @brief 网格生成器类
         * 用于生成各种基础几何体网格
         */
        class MeshGenerator
        {
        public:
            /**
             * @brief 生成指定类型的基础几何体
             * @param type 几何体类型
             * @param params 创建参数
             * @return 生成的网格对象
             */
            static std::unique_ptr<Mesh> CreatePrimitive(PrimitiveMeshType type,
                                                         const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成立方体网格
             * @param size 立方体大小（默认为单位立方体）
             * @param params 创建参数
             * @return 立方体网格
             */
            static std::unique_ptr<Mesh> CreateCube(const Math::Vec3 &size = Math::Vec3(1.0f),
                                                    const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成球体网格
             * @param radius 球体半径
             * @param segments 水平分段数
             * @param rings 垂直环数
             * @param params 创建参数
             * @return 球体网格
             */
            static std::unique_ptr<Mesh> CreateSphere(float radius = 1.0f,
                                                      uint32_t segments = 32,
                                                      uint32_t rings = 16,
                                                      const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成平面网格
             * @param size 平面大小
             * @param subdivisions 细分级别
             * @param params 创建参数
             * @return 平面网格
             */
            static std::unique_ptr<Mesh> CreatePlane(const Math::Vec2 &size = Math::Vec2(2.0f),
                                                     uint32_t subdivisions = 1,
                                                     const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成圆柱体网格
             * @param radius 底面半径
             * @param height 高度
             * @param segments 圆周分段数
             * @param params 创建参数
             * @return 圆柱体网格
             */
            static std::unique_ptr<Mesh> CreateCylinder(float radius = 1.0f,
                                                        float height = 2.0f,
                                                        uint32_t segments = 32,
                                                        const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成锥体网格
             * @param radius 底面半径
             * @param height 高度
             * @param segments 圆周分段数
             * @param params 创建参数
             * @return 锥体网格
             */
            static std::unique_ptr<Mesh> CreateCone(float radius = 1.0f,
                                                    float height = 2.0f,
                                                    uint32_t segments = 32,
                                                    const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成环形体网格
             * @param majorRadius 主半径
             * @param minorRadius 次半径
             * @param majorSegments 主分段数
             * @param minorSegments 次分段数
             * @param params 创建参数
             * @return 环形体网格
             */
            static std::unique_ptr<Mesh> CreateTorus(float majorRadius = 1.0f,
                                                     float minorRadius = 0.3f,
                                                     uint32_t majorSegments = 32,
                                                     uint32_t minorSegments = 16,
                                                     const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成网格平面（适用于地形或网格显示）
             * @param width 宽度
             * @param height 高度
             * @param widthSegments 宽度分段数
             * @param heightSegments 高度分段数
             * @param params 创建参数
             * @return 网格平面
             */
            static std::unique_ptr<Mesh> CreateGrid(float width = 10.0f,
                                                    float height = 10.0f,
                                                    uint32_t widthSegments = 10,
                                                    uint32_t heightSegments = 10,
                                                    const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成茶壶网格（经典测试模型）
             * @param params 创建参数
             * @return 茶壶网格
             */
            static std::unique_ptr<Mesh> CreateTeapot(const MeshCreateParams &params = MeshCreateParams{});

            /**
             * @brief 生成简单三角形网格（用于测试）
             * @param size 三角形大小
             * @return 三角形网格
             */
            static std::unique_ptr<Mesh> CreateTriangle(float size = 1.0f);

            /**
             * @brief 生成四边形网格（用于测试和UI）
             * @param size 四边形大小
             * @return 四边形网格
             */
            static std::unique_ptr<Mesh> CreateQuad(const Math::Vec2 &size = Math::Vec2(1.0f));

            /**
             * @brief 生成线框立方体（用于调试可视化）
             * @param size 立方体大小
             * @return 线框立方体网格
             */
            static std::unique_ptr<Mesh> CreateWireCube(const Math::Vec3 &size = Math::Vec3(1.0f));

            /**
             * @brief 生成坐标轴网格（用于调试可视化）
             * @param length 轴线长度
             * @return 坐标轴网格
             */
            static std::unique_ptr<Mesh> CreateAxis(float length = 1.0f);

        private:
            /**
             * @brief 计算球面坐标对应的位置
             * @param theta 水平角度
             * @param phi 垂直角度
             * @param radius 半径
             * @return 球面位置
             */
            static Math::Vec3 SphericalToCartesian(float theta, float phi, float radius);

            /**
             * @brief 为网格生成切线向量
             * @param mesh 目标网格
             * @param params 创建参数
             */
            static void GenerateTangents(Mesh *mesh, const MeshCreateParams &params);

            /**
             * @brief 为网格生成法线向量
             * @param mesh 目标网格
             * @param params 创建参数
             */
            static void GenerateNormals(Mesh *mesh, const MeshCreateParams &params);

            /**
             * @brief 为网格生成纹理坐标
             * @param mesh 目标网格
             * @param params 创建参数
             */
            static void GenerateTexCoords(Mesh *mesh, const MeshCreateParams &params);

            /**
             * @brief 计算平面纹理坐标
             * @param position 世界位置
             * @param normal 法线向量
             * @return 纹理坐标
             */
            static Math::Vec2 CalculatePlanarTexCoords(const Math::Vec3 &position, const Math::Vec3 &normal);

            /**
             * @brief 计算球面纹理坐标
             * @param position 世界位置
             * @return 纹理坐标
             */
            static Math::Vec2 CalculateSphericalTexCoords(const Math::Vec3 &position);

            /**
             * @brief 计算圆柱形纹理坐标
             * @param position 世界位置
             * @param height 圆柱体高度
             * @return 纹理坐标
             */
            static Math::Vec2 CalculateCylindricalTexCoords(const Math::Vec3 &position, float height);
        };
    }
}

#endif // ORANGE_RENDERCORE_MESHGENERATOR_H