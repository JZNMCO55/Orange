#ifndef ORANGE_CAMERA_CONTROLLER_H
#define ORANGE_CAMERA_CONTROLLER_H

#include "Camera.h"
#include "../../../Layer1/Core/Event/Events.h"

namespace Orange::RenderCore
{

    enum class CameraControlType
    {
        FPS,   // 第一人称视角
        Orbit, // 轨道相机（围绕目标旋转）
        Fly    // 自由飞行
    };

    /**
     * @brief 相机控制器 - 处理用户输入和相机运动
     *
     * 功能包括：
     * - FPS风格相机控制（WASD移动，鼠标查看）
     * - 轨道相机控制（围绕目标旋转和缩放）
     * - 自由飞行控制
     * - 平滑运动和插值
     */
    class CameraController
    {
    public:
        explicit CameraController(Camera *camera);
        ~CameraController() = default;

        // 控制模式设置
        void SetControlType(CameraControlType type) { m_controlType = type; }
        CameraControlType GetControlType() const { return m_controlType; }

        // 运动参数设置
        void SetMovementSpeed(float speed) { m_movementSpeed = speed; }
        void SetMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }
        void SetSmoothness(float smoothness) { m_smoothness = smoothness; }

        float GetMovementSpeed() const { return m_movementSpeed; }
        float GetMouseSensitivity() const { return m_mouseSensitivity; }
        float GetSmoothness() const { return m_smoothness; }

        // 轨道相机参数
        void SetOrbitTarget(const Math::Vec3 &target) { m_orbitTarget = target; }
        void SetOrbitDistance(float distance) { m_orbitDistance = distance; }
        void SetOrbitConstraints(float minDistance, float maxDistance, float minPitch, float maxPitch);

        const Math::Vec3 &GetOrbitTarget() const { return m_orbitTarget; }
        float GetOrbitDistance() const { return m_orbitDistance; }

        // 更新函数
        void Update(float deltaTime);

        // 事件处理
        void OnMouseMove(float deltaX, float deltaY);
        void OnMouseScroll(float scrollY);
        void OnKeyPress(Orange::Core::KeyCode key);
        void OnKeyRelease(Orange::Core::KeyCode key);

        // 便捷控制函数
        void MoveForward(float amount);
        void MoveRight(float amount);
        void MoveUp(float amount);
        void Rotate(float yaw, float pitch);

        // 重置和设置位置
        void ResetToDefaultPosition();
        void SetPosition(const Math::Vec3 &position);
        void LookAt(const Math::Vec3 &target);

    private:
        Camera *m_camera;
        CameraControlType m_controlType = CameraControlType::FPS;

        // 运动参数
        float m_movementSpeed = 5.0f;    // 单位/秒
        float m_mouseSensitivity = 0.1f; // 度/像素
        float m_smoothness = 10.0f;      // 平滑系数

        // FPS控制状态
        struct
        {
            bool moveForward = false;
            bool moveBackward = false;
            bool moveLeft = false;
            bool moveRight = false;
            bool moveUp = false;
            bool moveDown = false;
        } m_keyStates;

        // 鼠标状态
        float m_yaw = -90.0f; // 偏航角（水平旋转）
        float m_pitch = 0.0f; // 俯仰角（垂直旋转）
        bool m_firstMouse = true;

        // 轨道相机参数
        Math::Vec3 m_orbitTarget{0.0f};
        float m_orbitDistance = 10.0f;
        float m_orbitYaw = 0.0f;
        float m_orbitPitch = 0.0f;

        // 轨道相机约束
        float m_minOrbitDistance = 1.0f;
        float m_maxOrbitDistance = 100.0f;
        float m_minOrbitPitch = -80.0f;
        float m_maxOrbitPitch = 80.0f;

        // 平滑运动状态
        Math::Vec3 m_targetPosition{0.0f};
        Math::Vec3 m_currentVelocity{0.0f};

        // 默认位置
        Math::Vec3 m_defaultPosition{0.0f, 0.0f, 3.0f};
        Math::Vec3 m_defaultTarget{0.0f, 0.0f, 0.0f};

        // 内部更新函数
        void UpdateFPSCamera(float deltaTime);
        void UpdateOrbitCamera(float deltaTime);
        void UpdateFlyCamera(float deltaTime);

        // 输入处理
        void ProcessKeyboardInput(float deltaTime);
        void UpdateCameraVectors();

        // 约束函数
        float ClampAngle(float angle, float min, float max);
        void ApplyOrbitConstraints();
    };

} // namespace Orange::RenderCore

#endif // ORANGE_CAMERA_CONTROLLER_H