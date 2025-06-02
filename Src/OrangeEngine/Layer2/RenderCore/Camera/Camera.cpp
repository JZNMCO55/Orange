#include "Camera.h"
#include <algorithm>

namespace Orange::RenderCore
{

    Camera::Camera()
    {
        Update();
    }

    Camera::Camera(ProjectionType projectionType)
        : m_projectionType(projectionType)
    {
        Update();
    }

    void Camera::SetPosition(const Math::Vec3 &position)
    {
        m_position = position;
        m_viewMatrixDirty = true;
        m_frustumDirty = true;
    }

    void Camera::SetRotation(const Math::Vec3 &eulerAngles)
    {
        // 将欧拉角转换为方向向量
        Math::Vec3 radians = Math::Vec3(
            Math::DegToRad(eulerAngles.X()),
            Math::DegToRad(eulerAngles.Y()),
            Math::DegToRad(eulerAngles.Z()));

        // 计算forward向量
        Math::Vec3 newForward(
            cos(radians.Y()) * cos(radians.X()),
            sin(radians.X()),
            sin(radians.Y()) * cos(radians.X()));
        m_forward = Math::Normalize(newForward);

        UpdateVectors();
        m_viewMatrixDirty = true;
        m_frustumDirty = true;
    }

    void Camera::SetTarget(const Math::Vec3 &target)
    {
        m_forward = Math::Normalize(target - m_position);
        UpdateVectors();
        m_viewMatrixDirty = true;
        m_frustumDirty = true;
    }

    void Camera::SetPerspective(float fov, float aspectRatio, float nearPlane, float farPlane)
    {
        m_projectionType = ProjectionType::Perspective;
        m_fov = fov;
        m_aspectRatio = aspectRatio;
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        m_projectionMatrixDirty = true;
        m_frustumDirty = true;
    }

    void Camera::SetOrthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane)
    {
        m_projectionType = ProjectionType::Orthographic;
        m_orthoLeft = left;
        m_orthoRight = right;
        m_orthoBottom = bottom;
        m_orthoTop = top;
        m_nearPlane = nearPlane;
        m_farPlane = farPlane;
        m_projectionMatrixDirty = true;
        m_frustumDirty = true;
    }

    void Camera::SetViewport(int width, int height)
    {
        m_viewportWidth = width;
        m_viewportHeight = height;

        if (m_projectionType == ProjectionType::Perspective)
        {
            m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
            m_projectionMatrixDirty = true;
            m_frustumDirty = true;
        }
    }

    const Math::Mat4 &Camera::GetViewMatrix() const
    {
        if (m_viewMatrixDirty)
        {
            UpdateViewMatrix();
        }
        return m_viewMatrix;
    }

    const Math::Mat4 &Camera::GetProjectionMatrix() const
    {
        if (m_projectionMatrixDirty)
        {
            UpdateProjectionMatrix();
        }
        return m_projectionMatrix;
    }

    Math::Mat4 Camera::GetViewProjectionMatrix() const
    {
        return GetProjectionMatrix() * GetViewMatrix();
    }

    const Camera::Frustum &Camera::GetFrustum() const
    {
        if (m_frustumDirty || m_viewMatrixDirty || m_projectionMatrixDirty)
        {
            UpdateFrustum();
        }
        return m_frustum;
    }

    bool Camera::IsPointInFrustum(const Math::Vec3 &point) const
    {
        const Frustum &frustum = GetFrustum();

        // 检查点是否在所有6个平面的正面
        for (int i = 0; i < 6; ++i)
        {
            if (DistanceToPlane(frustum.planes[i], point) < 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    bool Camera::IsSphereInFrustum(const Math::Vec3 &center, float radius) const
    {
        const Frustum &frustum = GetFrustum();

        // 检查球心到每个平面的距离
        for (int i = 0; i < 6; ++i)
        {
            if (DistanceToPlane(frustum.planes[i], center) < -radius)
            {
                return false; // 球完全在平面外侧
            }
        }
        return true;
    }

    bool Camera::IsBoxInFrustum(const Math::Vec3 &min, const Math::Vec3 &max) const
    {
        const Frustum &frustum = GetFrustum();

        // 获取包围盒的8个顶点
        Math::Vec3 corners[8] = {
            {min.X(), min.Y(), min.Z()}, {max.X(), min.Y(), min.Z()}, {min.X(), max.Y(), min.Z()}, {max.X(), max.Y(), min.Z()}, {min.X(), min.Y(), max.Z()}, {max.X(), min.Y(), max.Z()}, {min.X(), max.Y(), max.Z()}, {max.X(), max.Y(), max.Z()}};

        // 对每个平面检查
        for (int i = 0; i < 6; ++i)
        {
            bool allOutside = true;

            // 检查8个顶点是否都在平面外侧
            for (int j = 0; j < 8; ++j)
            {
                if (DistanceToPlane(frustum.planes[i], corners[j]) >= 0.0f)
                {
                    allOutside = false;
                    break;
                }
            }

            if (allOutside)
            {
                return false; // 整个包围盒在这个平面外侧
            }
        }
        return true;
    }

    Camera::Ray Camera::ScreenPointToRay(const Math::Vec2 &screenPos) const
    {
        // 将屏幕坐标转换为NDC坐标 (-1 到 1)
        float x = (2.0f * screenPos.X()) / m_viewportWidth - 1.0f;
        float y = 1.0f - (2.0f * screenPos.Y()) / m_viewportHeight; // Y轴翻转

        // 在近平面和远平面上的点 (NDC坐标)
        Math::Vec4 nearPoint(x, y, -1.0f, 1.0f);
        Math::Vec4 farPoint(x, y, 1.0f, 1.0f);

        // 转换到世界坐标
        Math::Mat4 invVP = Math::Inverse(GetViewProjectionMatrix());
        nearPoint = invVP * nearPoint;
        farPoint = invVP * farPoint;

        // 透视除法
        Math::Vec4 nearDivided = Math::Vec4(nearPoint.X() / nearPoint.W(), nearPoint.Y() / nearPoint.W(), nearPoint.Z() / nearPoint.W(), 1.0f);
        Math::Vec4 farDivided = Math::Vec4(farPoint.X() / farPoint.W(), farPoint.Y() / farPoint.W(), farPoint.Z() / farPoint.W(), 1.0f);

        Ray ray;
        ray.origin = nearDivided.XYZ();
        ray.direction = Math::Normalize(farDivided.XYZ() - nearDivided.XYZ());

        return ray;
    }

    Math::Vec3 Camera::WorldToScreenPoint(const Math::Vec3 &worldPos) const
    {
        Math::Vec4 clipSpacePos = GetViewProjectionMatrix() * Math::Vec4(worldPos, 1.0f);

        // 透视除法
        Math::Vec3 ndcPos = Math::Vec3(clipSpacePos.X() / clipSpacePos.W(), clipSpacePos.Y() / clipSpacePos.W(), clipSpacePos.Z() / clipSpacePos.W());

        // 转换到屏幕坐标
        Math::Vec3 screenPos(
            (ndcPos.X() + 1.0f) * 0.5f * m_viewportWidth,
            (1.0f - ndcPos.Y()) * 0.5f * m_viewportHeight, // Y轴翻转
            ndcPos.Z()                                     // 深度值
        );

        return screenPos;
    }

    void Camera::Update()
    {
        if (m_viewMatrixDirty)
        {
            UpdateViewMatrix();
        }
        if (m_projectionMatrixDirty)
        {
            UpdateProjectionMatrix();
        }
        if (m_frustumDirty)
        {
            UpdateFrustum();
        }
    }

    void Camera::UpdateViewMatrix() const
    {
        m_viewMatrix = Math::LookAt(m_position, m_position + m_forward, m_up);
        m_viewMatrixDirty = false;
    }

    void Camera::UpdateProjectionMatrix() const
    {
        if (m_projectionType == ProjectionType::Perspective)
        {
            m_projectionMatrix = Math::Perspective(Math::DegToRad(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
        }
        else
        {
            m_projectionMatrix = Math::Ortho(m_orthoLeft, m_orthoRight, m_orthoBottom, m_orthoTop, m_nearPlane, m_farPlane);
        }
        m_projectionMatrixDirty = false;
    }

    void Camera::UpdateFrustum() const
    {
        // 确保视图和投影矩阵是最新的
        if (m_viewMatrixDirty)
            UpdateViewMatrix();
        if (m_projectionMatrixDirty)
            UpdateProjectionMatrix();

        ExtractFrustumPlanes();
        m_frustumDirty = false;
    }

    void Camera::UpdateVectors()
    {
        // 重新计算right和up向量
        Math::Vec3 worldUp(0.0f, 1.0f, 0.0f);
        m_right = Math::Normalize(Math::Cross(m_forward, worldUp));
        m_up = Math::Normalize(Math::Cross(m_right, m_forward));
    }

    void Camera::ExtractFrustumPlanes() const
    {
        Math::Mat4 vp = GetProjectionMatrix() * GetViewMatrix();

        // 提取6个平面 (Gribb & Hartmann method)
        // 左平面
        m_frustum.planes[0] = Math::Vec4(
            vp.Get(0, 3) + vp.Get(0, 0),
            vp.Get(1, 3) + vp.Get(1, 0),
            vp.Get(2, 3) + vp.Get(2, 0),
            vp.Get(3, 3) + vp.Get(3, 0));

        // 右平面
        m_frustum.planes[1] = Math::Vec4(
            vp.Get(0, 3) - vp.Get(0, 0),
            vp.Get(1, 3) - vp.Get(1, 0),
            vp.Get(2, 3) - vp.Get(2, 0),
            vp.Get(3, 3) - vp.Get(3, 0));

        // 下平面
        m_frustum.planes[2] = Math::Vec4(
            vp.Get(0, 3) + vp.Get(0, 1),
            vp.Get(1, 3) + vp.Get(1, 1),
            vp.Get(2, 3) + vp.Get(2, 1),
            vp.Get(3, 3) + vp.Get(3, 1));

        // 上平面
        m_frustum.planes[3] = Math::Vec4(
            vp.Get(0, 3) - vp.Get(0, 1),
            vp.Get(1, 3) - vp.Get(1, 1),
            vp.Get(2, 3) - vp.Get(2, 1),
            vp.Get(3, 3) - vp.Get(3, 1));

        // 近平面
        m_frustum.planes[4] = Math::Vec4(
            vp.Get(0, 3) + vp.Get(0, 2),
            vp.Get(1, 3) + vp.Get(1, 2),
            vp.Get(2, 3) + vp.Get(2, 2),
            vp.Get(3, 3) + vp.Get(3, 2));

        // 远平面
        m_frustum.planes[5] = Math::Vec4(
            vp.Get(0, 3) - vp.Get(0, 2),
            vp.Get(1, 3) - vp.Get(1, 2),
            vp.Get(2, 3) - vp.Get(2, 2),
            vp.Get(3, 3) - vp.Get(3, 2));

        // 标准化平面方程
        for (int i = 0; i < 6; ++i)
        {
            float length = Math::Length(m_frustum.planes[i].XYZ());
            m_frustum.planes[i] = m_frustum.planes[i] * (1.0f / length);
        }
    }

    float Camera::DistanceToPlane(const Math::Vec4 &plane, const Math::Vec3 &point) const
    {
        return Math::Dot(plane.XYZ(), point) + plane.W();
    }

} // namespace Orange::RenderCore