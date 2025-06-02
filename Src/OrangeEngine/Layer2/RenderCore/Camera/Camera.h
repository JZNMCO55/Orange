#ifndef ORANGE_CAMERA_H
#define ORANGE_CAMERA_H

#include "../../../Layer1/Core/Math/Math.h"

namespace Orange::RenderCore
{

    enum class ProjectionType
    {
        Perspective,
        Orthographic
    };

    /**
     * @brief 相机类 - 负责视图和投影变换
     *
     * 功能包括：
     * - 透视/正交投影矩阵计算
     * - 视图矩阵计算
     * - 视锥体计算（用于裁剪）
     * - 相机参数管理
     */
    class Camera
    {
    public:
        // 构造函数
        Camera();
        explicit Camera(ProjectionType projectionType);

        // 基础变换
        void SetPosition(const Math::Vec3 &position);
        void SetRotation(const Math::Vec3 &eulerAngles); // 欧拉角(度)
        void SetTarget(const Math::Vec3 &target);        // 看向目标点

        // 投影参数设置
        void SetPerspective(float fov, float aspectRatio, float nearPlane, float farPlane);
        void SetOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane);
        void SetViewport(int width, int height);

        // 获取变换矩阵
        const Math::Mat4 &GetViewMatrix() const;
        const Math::Mat4 &GetProjectionMatrix() const;
        Math::Mat4 GetViewProjectionMatrix() const;

        // 相机属性获取
        const Math::Vec3 &GetPosition() const { return m_position; }
        const Math::Vec3 &GetForward() const { return m_forward; }
        const Math::Vec3 &GetRight() const { return m_right; }
        const Math::Vec3 &GetUp() const { return m_up; }

        float GetFOV() const { return m_fov; }
        float GetAspectRatio() const { return m_aspectRatio; }
        float GetNearPlane() const { return m_nearPlane; }
        float GetFarPlane() const { return m_farPlane; }

        ProjectionType GetProjectionType() const { return m_projectionType; }

        // 视锥体相关
        struct Frustum
        {
            Math::Vec4 planes[6]; // 左右上下前后6个面 (法向量+距离)
        };

        const Frustum &GetFrustum() const;
        bool IsPointInFrustum(const Math::Vec3 &point) const;
        bool IsSphereInFrustum(const Math::Vec3 &center, float radius) const;
        bool IsBoxInFrustum(const Math::Vec3 &min, const Math::Vec3 &max) const;

        // 射线投射
        struct Ray
        {
            Math::Vec3 origin;
            Math::Vec3 direction;
        };

        Ray ScreenPointToRay(const Math::Vec2 &screenPos) const; // 屏幕坐标到世界射线
        Math::Vec3 WorldToScreenPoint(const Math::Vec3 &worldPos) const;

        // 更新函数
        void Update(); // 重新计算所有矩阵和视锥体

    private:
        // 相机变换
        Math::Vec3 m_position{0.0f, 0.0f, 0.0f};
        Math::Vec3 m_forward{0.0f, 0.0f, -1.0f}; // Z轴负方向
        Math::Vec3 m_right{1.0f, 0.0f, 0.0f};    // X轴正方向
        Math::Vec3 m_up{0.0f, 1.0f, 0.0f};       // Y轴正方向

        // 投影参数
        ProjectionType m_projectionType = ProjectionType::Perspective;

        // 透视投影参数
        float m_fov = 45.0f; // 视野角度(度)
        float m_aspectRatio = 16.0f / 9.0f;

        // 正交投影参数
        float m_orthoLeft = -10.0f;
        float m_orthoRight = 10.0f;
        float m_orthoBottom = -10.0f;
        float m_orthoTop = 10.0f;

        // 通用参数
        float m_nearPlane = 0.1f;
        float m_farPlane = 1000.0f;

        // 视口
        int m_viewportWidth = 800;
        int m_viewportHeight = 600;

        // 计算得出的矩阵
        mutable Math::Mat4 m_viewMatrix{1.0f};
        mutable Math::Mat4 m_projectionMatrix{1.0f};
        mutable Frustum m_frustum;

        // 脏标记
        mutable bool m_viewMatrixDirty = true;
        mutable bool m_projectionMatrixDirty = true;
        mutable bool m_frustumDirty = true;

        // 内部计算函数
        void UpdateViewMatrix() const;
        void UpdateProjectionMatrix() const;
        void UpdateFrustum() const;
        void UpdateVectors(); // 根据旋转更新forward/right/up向量

        // 视锥体计算辅助函数
        void ExtractFrustumPlanes() const;
        float DistanceToPlane(const Math::Vec4 &plane, const Math::Vec3 &point) const;
    };

} // namespace Orange::RenderCore

#endif // ORANGE_CAMERA_H