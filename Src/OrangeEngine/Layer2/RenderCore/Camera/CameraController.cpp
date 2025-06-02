#include "CameraController.h"
#include <algorithm>

namespace Orange::RenderCore
{

    CameraController::CameraController(Camera *camera)
        : m_camera(camera)
    {
        if (m_camera)
        {
            m_targetPosition = m_camera->GetPosition();
        }
    }

    void CameraController::SetOrbitConstraints(float minDistance, float maxDistance, float minPitch, float maxPitch)
    {
        m_minOrbitDistance = minDistance;
        m_maxOrbitDistance = maxDistance;
        m_minOrbitPitch = minPitch;
        m_maxOrbitPitch = maxPitch;
        ApplyOrbitConstraints();
    }

    void CameraController::Update(float deltaTime)
    {
        if (!m_camera)
            return;

        switch (m_controlType)
        {
        case CameraControlType::FPS:
            UpdateFPSCamera(deltaTime);
            break;
        case CameraControlType::Orbit:
            UpdateOrbitCamera(deltaTime);
            break;
        case CameraControlType::Fly:
            UpdateFlyCamera(deltaTime);
            break;
        }

        m_camera->Update();
    }

    void CameraController::OnMouseMove(float deltaX, float deltaY)
    {
        if (m_firstMouse)
        {
            m_firstMouse = false;
            return;
        }

        float xOffset = deltaX * m_mouseSensitivity;
        float yOffset = deltaY * m_mouseSensitivity;

        switch (m_controlType)
        {
        case CameraControlType::FPS:
        case CameraControlType::Fly:
            m_yaw += xOffset;
            m_pitch -= yOffset; // 翻转Y轴

            // 约束俯仰角
            m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
            UpdateCameraVectors();
            break;

        case CameraControlType::Orbit:
            m_orbitYaw += xOffset;
            m_orbitPitch -= yOffset;
            ApplyOrbitConstraints();
            break;
        }
    }

    void CameraController::OnMouseScroll(float scrollY)
    {
        if (m_controlType == CameraControlType::Orbit)
        {
            m_orbitDistance -= scrollY;
            ApplyOrbitConstraints();
        }
        else
        {
            // 对于FPS和Fly模式，滚轮可以调整移动速度
            m_movementSpeed += scrollY * 0.5f;
            m_movementSpeed = std::max(0.1f, m_movementSpeed);
        }
    }

    void CameraController::OnKeyPress(Orange::Core::KeyCode key)
    {
        using namespace Orange::Core;

        switch (key)
        {
        case KeyCode::W:
            m_keyStates.moveForward = true;
            break;
        case KeyCode::S:
            m_keyStates.moveBackward = true;
            break;
        case KeyCode::A:
            m_keyStates.moveLeft = true;
            break;
        case KeyCode::D:
            m_keyStates.moveRight = true;
            break;
        case KeyCode::Space:
            m_keyStates.moveUp = true;
            break;
        case KeyCode::LeftShift:
            m_keyStates.moveDown = true;
            break;
        }
    }

    void CameraController::OnKeyRelease(Orange::Core::KeyCode key)
    {
        using namespace Orange::Core;

        switch (key)
        {
        case KeyCode::W:
            m_keyStates.moveForward = false;
            break;
        case KeyCode::S:
            m_keyStates.moveBackward = false;
            break;
        case KeyCode::A:
            m_keyStates.moveLeft = false;
            break;
        case KeyCode::D:
            m_keyStates.moveRight = false;
            break;
        case KeyCode::Space:
            m_keyStates.moveUp = false;
            break;
        case KeyCode::LeftShift:
            m_keyStates.moveDown = false;
            break;
        }
    }

    void CameraController::MoveForward(float amount)
    {
        Math::Vec3 movement = m_camera->GetForward() * amount;
        Math::Vec3 newPos = m_camera->GetPosition() + movement;
        m_camera->SetPosition(newPos);
    }

    void CameraController::MoveRight(float amount)
    {
        Math::Vec3 movement = m_camera->GetRight() * amount;
        Math::Vec3 newPos = m_camera->GetPosition() + movement;
        m_camera->SetPosition(newPos);
    }

    void CameraController::MoveUp(float amount)
    {
        Math::Vec3 movement = m_camera->GetUp() * amount;
        Math::Vec3 newPos = m_camera->GetPosition() + movement;
        m_camera->SetPosition(newPos);
    }

    void CameraController::Rotate(float yaw, float pitch)
    {
        if (m_controlType == CameraControlType::Orbit)
        {
            m_orbitYaw += yaw;
            m_orbitPitch += pitch;
            ApplyOrbitConstraints();
        }
        else
        {
            m_yaw += yaw;
            m_pitch += pitch;
            m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);
            UpdateCameraVectors();
        }
    }

    void CameraController::ResetToDefaultPosition()
    {
        if (!m_camera)
            return;

        m_targetPosition = m_defaultPosition;
        m_camera->SetPosition(m_defaultPosition);
        m_camera->SetTarget(m_defaultTarget);

        m_yaw = -90.0f;
        m_pitch = 0.0f;
        m_orbitYaw = 0.0f;
        m_orbitPitch = 0.0f;
        m_orbitDistance = 10.0f;

        UpdateCameraVectors();
    }

    void CameraController::SetPosition(const Math::Vec3 &position)
    {
        m_camera->SetPosition(position);
        m_targetPosition = position;
    }

    void CameraController::LookAt(const Math::Vec3 &target)
    {
        if (!m_camera)
            return;

        if (m_controlType == CameraControlType::Orbit)
        {
            SetOrbitTarget(target);
        }
        else
        {
            m_camera->SetTarget(target);

            // 计算yaw和pitch角度
            Math::Vec3 direction = target - m_camera->GetPosition();
            direction = Math::Normalize(direction);
            m_yaw = Math::RadToDeg(atan2(direction.Z(), direction.X()));
            m_pitch = Math::RadToDeg(asin(direction.Y()));
        }
    }

    void CameraController::UpdateFPSCamera(float deltaTime)
    {
        ProcessKeyboardInput(deltaTime);

        // 平滑移动到目标位置
        Math::Vec3 currentPos = m_camera->GetPosition();
        Math::Vec3 smoothedPos = Math::Lerp(currentPos, m_targetPosition, m_smoothness * deltaTime);
        m_camera->SetPosition(smoothedPos);

        // 更新相机向量
        UpdateCameraVectors();
    }

    void CameraController::UpdateOrbitCamera(float deltaTime)
    {
        // 计算轨道相机位置
        float yawRad = Math::DegToRad(m_orbitYaw);
        float pitchRad = Math::DegToRad(m_orbitPitch);

        Math::Vec3 offset(
            cos(pitchRad) * cos(yawRad) * m_orbitDistance,
            sin(pitchRad) * m_orbitDistance,
            cos(pitchRad) * sin(yawRad) * m_orbitDistance);

        Math::Vec3 newPosition = m_orbitTarget + offset;
        m_camera->SetPosition(newPosition);
        m_camera->SetTarget(m_orbitTarget);
    }

    void CameraController::UpdateFlyCamera(float deltaTime)
    {
        ProcessKeyboardInput(deltaTime);

        // 自由飞行模式下直接设置位置，不使用平滑移动
        m_camera->SetPosition(m_targetPosition);
    }

    void CameraController::ProcessKeyboardInput(float deltaTime)
    {
        float velocity = m_movementSpeed * deltaTime;

        if (m_keyStates.moveForward)
        {
            MoveForward(velocity);
        }
        if (m_keyStates.moveBackward)
        {
            MoveForward(-velocity);
        }
        if (m_keyStates.moveLeft)
        {
            MoveRight(-velocity);
        }
        if (m_keyStates.moveRight)
        {
            MoveRight(velocity);
        }
        if (m_keyStates.moveUp)
        {
            MoveUp(velocity);
        }
        if (m_keyStates.moveDown)
        {
            MoveUp(-velocity);
        }
    }

    void CameraController::UpdateCameraVectors()
    {
        if (!m_camera)
            return;

        // 重新计算前向向量
        Math::Vec3 front(
            cos(Math::DegToRad(m_yaw)) * cos(Math::DegToRad(m_pitch)),
            sin(Math::DegToRad(m_pitch)),
            sin(Math::DegToRad(m_yaw)) * cos(Math::DegToRad(m_pitch)));

        Math::Vec3 target = m_camera->GetPosition() + Math::Normalize(front);
        m_camera->SetTarget(target);
    }

    float CameraController::ClampAngle(float angle, float min, float max)
    {
        return std::clamp(angle, min, max);
    }

    void CameraController::ApplyOrbitConstraints()
    {
        m_orbitDistance = std::clamp(m_orbitDistance, m_minOrbitDistance, m_maxOrbitDistance);
        m_orbitPitch = ClampAngle(m_orbitPitch, m_minOrbitPitch, m_maxOrbitPitch);

        // 将偏航角保持在0-360度范围内
        while (m_orbitYaw > 360.0f)
            m_orbitYaw -= 360.0f;
        while (m_orbitYaw < 0.0f)
            m_orbitYaw += 360.0f;
    }

} // namespace Orange::RenderCore