# Orange 渲染模块开发指南

## 目录
1. [项目概述](#项目概述)
2. [项目架构](#项目架构)
3. [依赖管理](#依赖管理)
4. [构建系统配置](#构建系统配置)
5. [图形接口层详细设计](#图形接口层详细设计)
6. [常见问题排查](#常见问题排查)
7. [开发最佳实践](#开发最佳实践)
8. [高性能渲染技术](#高性能渲染技术)
9. [项目开发计划](#项目开发计划)
10. [附录](#附录)

## 项目概述 

OrangeDev是一个基于C++的现代游戏引擎项目，采用分层架构设计，支持Vulkan图形API。项目包含以下主要组件：

- **OrangeEngine**: 核心引擎静态库
- **OrangeTest**: 单元测试可执行文件
- **OrangeEditor**: 编辑器可执行文件

### 技术栈

- **编程语言**: C++17/20
- **构建系统**: CMake 3.20+
- **图形API**: Vulkan 1.3+
- **着色器编译**: Shaderc
- **窗口管理**: GLFW 3.3+
- **日志系统**: spdlog + fmt
- **数学库**: GLM
- **测试框架**: GoogleTest

## 项目架构

### 目录结构

```
OrangeDev/
├── Src/
│   ├── OrangeEngine/
│   │   ├── Layer1/
│   │   │   └── Core/ #核心层 (数学库、日志系统、内存管理)
│   │   │   └── Platform/ #平台层
│   │   │       └── Graphics/ #图形层
│   │   │           └── GraphicsInterface/    # 图形接口抽象层
│   │   │           └── GraphicsAPI/          # 图形API实现层
│   │   │               └── Vulkan/           # Vulkan实现
│   │   └── Layer2/                 # 应用层（场景、渲染器）
│   ├── OrangeTest/
│   └── OrangeEditor/
├── 3rdparty/                       # 第三方依赖
├── Resources/                      # 资源文件
└── CMakeLists.txt
```

### 分层设计理念

- **Layer1 (核心层)**: 提供基础服务，无外部依赖
- **Layer1 (平台层)**: 封装平台特定功能，提供统一接口
- **Layer2 (应用层)**: 高级功能模块，基于下层接口构建

## 依赖管理

### 主要依赖及版本

```cmake
# 核心图形依赖
find_package(Vulkan 1.3 REQUIRED)
find_package(glfw3 3.3 REQUIRED)

# 第三方库
shaderc_combined.lib    # 着色器编译（静态链接）
spdlog                  # 日志系统
fmt                     # 格式化库
glm                     # 数学库
gtest                   # 测试框架
```

### Shaderc配置详解

Shaderc是Google开发的着色器编译库，用于将GLSL/HLSL编译为SPIR-V字节码。

#### 库文件选择

- `shaderc.lib` (1.5MB): 基础编译功能
- `shaderc_combined.lib` (437MB): 包含所有依赖的完整版本 ✅**推荐**
- `shaderc_shared.dll/.lib`: 动态链接版本

#### CMake配置

```cmake
# 查找Shaderc库文件
find_library(SHADERC_LIB
    NAMES shaderc_combined
    PATHS ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/shaderc/build_Debug/libshaderc/Debug
    NO_DEFAULT_PATH
)

# 创建导入目标
if(SHADERC_LIB)
    add_library(shaderc_combined STATIC IMPORTED)
    set_target_properties(shaderc_combined PROPERTIES
        IMPORTED_LOCATION ${SHADERC_LIB}
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/shaderc/libshaderc/include"
    )
    
    # 私有链接，避免传播依赖
    target_link_libraries(${PROJECT_NAME} PRIVATE shaderc_combined)
endif()
```

## 构建系统配置

### CMake最佳实践

#### 1. Vulkan支持配置

```cmake
# 查找Vulkan SDK
find_package(Vulkan REQUIRED)

if(Vulkan_FOUND)
    # 链接Vulkan库
    target_link_libraries(${PROJECT_NAME} PUBLIC Vulkan::Vulkan)
    
    # 启用Vulkan编译宏
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DORANGE_VULKAN_ENABLED)
    
    message(STATUS "Vulkan found: ${Vulkan_LIBRARY}")
    message(STATUS "Vulkan headers: ${Vulkan_INCLUDE_DIR}")
endif()
```

#### 2. 编译器配置

```cmake
# C++标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /W4                 # 警告级别
        /permissive-        # 标准符合性
        /Zc:__cplusplus     # 正确的__cplusplus宏
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE 
        -Wall -Wextra -Wpedantic
    )
endif()
```

#### 3. 依赖链接策略

```cmake
# 公共依赖（需要传播给使用者）
target_link_libraries(${PROJECT_NAME} PUBLIC 
    Vulkan::Vulkan
    glfw
    glm::glm
)

# 私有依赖（内部实现细节）
target_link_libraries(${PROJECT_NAME} PRIVATE 
    shaderc_combined
    spdlog::spdlog
    fmt::fmt
)
```

## 图形接口层详细设计

### 架构概述

图形接口层采用分层抽象设计，将图形API的复杂性封装在统一的接口后面：

```
应用层 (OrangeEditor/OrangeTest)
    ↓
图形接口抽象层 (GraphicsInterface)
    ↓
图形API实现层 (GraphicsAPI/Vulkan)
    ↓
底层图形API (Vulkan SDK)
```

### 核心组件设计

#### 1. 渲染系统基础架构

```cpp
// 图形接口抽象层
namespace Orange::Graphics {
    
    // 核心图形系统接口
    class IGraphicsSystem {
    public:
        virtual ~IGraphicsSystem() = default;
        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void Present() = 0;
    };
    
    // 渲染设备接口
    class IRenderDevice {
    public:
        virtual ~IRenderDevice() = default;
        virtual IBuffer* CreateBuffer(const BufferCreateInfo& info) = 0;
        virtual ITexture* CreateTexture(const TextureCreateInfo& info) = 0;
        virtual IPipeline* CreatePipeline(const PipelineCreateInfo& info) = 0;
    };
}
```

#### 2. 资源管理系统

```cpp
// 缓冲区接口
class IBuffer {
public:
    virtual ~IBuffer() = default;
    virtual void* Map() = 0;
    virtual void Unmap() = 0;
    virtual void CopyFrom(const void* data, size_t size, size_t offset = 0) = 0;
    virtual size_t GetSize() const = 0;
};

// 纹理接口
class ITexture {
public:
    virtual ~ITexture() = default;
    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual TextureFormat GetFormat() const = 0;
    virtual void GenerateMipmaps() = 0;
};

// 渲染管线接口
class IPipeline {
public:
    virtual ~IPipeline() = default;
    virtual void Bind() = 0;
    virtual void SetUniform(const std::string& name, const void* data) = 0;
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1) = 0;
};
```

#### 3. 着色器编译系统

```cpp
// 着色器编译器接口
class IShaderCompiler {
public:
    virtual ~IShaderCompiler() = default;
    
    struct CompileOptions {
        ShaderStage stage;
        std::string entryPoint = "main";
        std::vector<std::string> includePaths;
        std::vector<std::pair<std::string, std::string>> macros;
        bool optimization = true;
        bool debugInfo = false;
    };
    
    virtual std::vector<uint32_t> CompileFromFile(
        const std::string& filePath,
        const CompileOptions& options
    ) = 0;
    
    virtual std::vector<uint32_t> CompileFromSource(
        const std::string& source,
        const CompileOptions& options
    ) = 0;
};

// Shaderc实现
class ShadercCompiler : public IShaderCompiler {
private:
    shaderc::Compiler m_compiler;
    shaderc::CompileOptions m_baseOptions;
    
public:
    ShadercCompiler();
    ~ShadercCompiler() override = default;
    
    std::vector<uint32_t> CompileFromFile(
        const std::string& filePath,
        const CompileOptions& options
    ) override;
    
    std::vector<uint32_t> CompileFromSource(
        const std::string& source,
        const CompileOptions& options
    ) override;
    
private:
    shaderc_shader_kind GetShadercStage(ShaderStage stage);
    void SetupCompileOptions(shaderc::CompileOptions& opts, const CompileOptions& options);
};
```

#### 4. Vulkan具体实现

```cpp
// Vulkan图形系统实现
class VulkanGraphicsSystem : public IGraphicsSystem {
private:
    VkInstance m_instance;
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkSurfaceKHR m_surface;
    VkSwapchainKHR m_swapchain;
    
    std::unique_ptr<VulkanDevice> m_renderDevice;
    std::unique_ptr<VulkanCommandPool> m_commandPool;
    std::unique_ptr<ShadercCompiler> m_shaderCompiler;
    
public:
    bool Initialize() override;
    void Shutdown() override;
    void BeginFrame() override;
    void EndFrame() override;
    void Present() override;
    
    IRenderDevice* GetRenderDevice() { return m_renderDevice.get(); }
    IShaderCompiler* GetShaderCompiler() { return m_shaderCompiler.get(); }
    
private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapchain();
    void SetupDebugMessenger();
};

// Vulkan缓冲区实现
class VulkanBuffer : public IBuffer {
private:
    VkDevice m_device;
    VkBuffer m_buffer;
    VkDeviceMemory m_memory;
    size_t m_size;
    void* m_mappedData;
    
public:
    VulkanBuffer(VkDevice device, const BufferCreateInfo& createInfo);
    ~VulkanBuffer() override;
    
    void* Map() override;
    void Unmap() override;
    void CopyFrom(const void* data, size_t size, size_t offset = 0) override;
    size_t GetSize() const override { return m_size; }
    
    VkBuffer GetVkBuffer() const { return m_buffer; }
};
```

#### 5. 内存管理系统

```cpp
// 渲染内存管理器
class RenderMemoryManager {
public:
    struct MemoryBlock {
        VkDeviceMemory memory;
        size_t size;
        size_t offset;
        bool isFree;
    };
    
    struct MemoryHeap {
        uint32_t memoryTypeIndex;
        std::vector<MemoryBlock> blocks;
        size_t totalSize;
        size_t usedSize;
    };
    
private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    std::vector<MemoryHeap> m_heaps;
    
public:
    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice);
    void Shutdown();
    
    VkDeviceMemory AllocateMemory(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties);
    void FreeMemory(VkDeviceMemory memory);
    
    // 内存统计
    size_t GetTotalAllocatedMemory() const;
    size_t GetUsedMemory() const;
    void PrintMemoryStats() const;
    
private:
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool AllocateNewHeap(uint32_t memoryTypeIndex, size_t size);
};

// 全局内存管理函数
bool InitializeMemorySystem(VkDevice device, VkPhysicalDevice physicalDevice);
void ShutdownMemorySystem();
RenderMemoryManager* GetMemoryManager();
```

#### 6. 同步对象管理

```cpp
// 事件对象接口
class IEvent {
public:
    virtual ~IEvent() = default;
    virtual void Set() = 0;
    virtual void Reset() = 0;
    virtual bool IsSet() const = 0;
    virtual void Wait(uint32_t timeoutMs = UINT32_MAX) = 0;
};

// Vulkan事件实现
class VulkanEvent : public IEvent {
private:
    VkDevice m_device;
    VkEvent m_event;
    std::string m_debugName;
    
public:
    struct EventCreateInfo {
        std::string debugName;
        bool initialState = false;
    };
    
    VulkanEvent(VkDevice device, const EventCreateInfo& createInfo);
    ~VulkanEvent() override;
    
    void Set() override;
    void Reset() override;
    bool IsSet() const override;
    void Wait(uint32_t timeoutMs = UINT32_MAX) override;
    
    VkEvent GetVkEvent() const { return m_event; }
};

// 事件工厂
class VulkanEventFactory {
private:
    VkDevice m_device;
    
public:
    VulkanEventFactory(VkDevice device);
    ~VulkanEventFactory() = default;
    
    std::unique_ptr<IEvent> CreateEvent(const VulkanEvent::EventCreateInfo& createInfo);
    bool Initialize();
};
```

#### 7. 渲染管线状态管理

```cpp
// 渲染状态枚举
enum class CullMode {
    None,
    Front,
    Back,
    All  // 修正: 之前错误使用FrontAndBack
};

enum class BlendMode {
    None,
    Alpha,
    Additive,
    Multiply
};

enum class DepthTest {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

// 管线状态描述
struct PipelineStateDesc {
    // 光栅化状态
    CullMode cullMode = CullMode::Back;
    bool wireframe = false;
    
    // 深度状态
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    DepthTest depthCompareOp = DepthTest::Less;
    
    // 混合状态
    BlendMode blendMode = BlendMode::None;
    
    // 多重采样
    uint32_t sampleCount = 1;
    bool alphaToCoverage = false;
};

// Vulkan管线实现
class VulkanPipeline : public IPipeline {
private:
    VkDevice m_device;
    VkPipeline m_pipeline;
    VkPipelineLayout m_pipelineLayout;
    VkRenderPass m_renderPass;
    PipelineStateDesc m_stateDesc;
    
public:
    struct PipelineCreateInfo {
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        VkVertexInputBindingDescription vertexBinding;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        PipelineStateDesc stateDesc;
        VkRenderPass renderPass;
    };
    
    VulkanPipeline(VkDevice device, const PipelineCreateInfo& createInfo);
    ~VulkanPipeline() override;
    
    void Bind() override;
    void SetUniform(const std::string& name, const void* data) override;
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1) override;
    
private:
    bool CreatePipelineLayout();
    bool CreateGraphicsPipeline(const PipelineCreateInfo& createInfo);
    VkCullModeFlags GetVulkanCullMode(CullMode mode);
    VkBlendFactor GetVulkanBlendFactor(BlendMode mode);
};
```

### 简化版图形系统

为了快速原型开发，实现了简化版图形系统：

```cpp
// 简化图形系统接口
class SimpleGraphicsSystem {
public:
    bool Initialize();
    void Shutdown();
    void BeginFrame();
    void EndFrame();
    void DrawTriangle();
    void DrawQuad();
    void SetClearColor(float r, float g, float b, float a = 1.0f);
    
private:
    std::unique_ptr<VulkanGraphicsSystem> m_vulkanSystem;
    std::unique_ptr<VulkanPipeline> m_basicPipeline;
    std::unique_ptr<VulkanBuffer> m_vertexBuffer;
};

// 简化Vulkan实现
class SimpleVulkanGraphics {
public:
    struct Vertex {
        glm::vec2 position;
        glm::vec3 color;
    };
    
    bool Initialize();
    void Cleanup();
    void DrawFrame();
    
private:
    // Vulkan核心对象
    VkInstance m_instance;
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkSurfaceKHR m_surface;
    VkSwapchainKHR m_swapchain;
    
    // 渲染资源
    VkRenderPass m_renderPass;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_graphicsPipeline;
    
    // 缓冲区
    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexBufferMemory;
    
    // 命令相关
    VkCommandPool m_commandPool;
    std::vector<VkCommandBuffer> m_commandBuffers;
    
    // 同步对象
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    
private:
    bool CreateInstance();
    bool SetupDebugMessenger();
    bool CreateSurface();
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapChain();
    bool CreateImageViews();
    bool CreateRenderPass();
    bool CreateDescriptorSetLayout();
    bool CreateGraphicsPipeline();
    bool CreateFramebuffers();
    bool CreateCommandPool();
    bool CreateVertexBuffer();
    bool CreateCommandBuffers();
    bool CreateSyncObjects();
    
    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
    std::vector<uint32_t> LoadShader(const std::string& filename);
};
```

### 6. 场景图与空间管理

```cpp
// 场景图系统
class SceneGraph {
public:
    class SceneNode {
    public:
        glm::mat4 GetWorldTransform() const;
        glm::mat4 GetLocalTransform() const { return m_localTransform; }
        void SetLocalTransform(const glm::mat4& transform);
        
        void AddChild(std::shared_ptr<SceneNode> child);
        void RemoveChild(std::shared_ptr<SceneNode> child);
        SceneNode* GetParent() const { return m_parent; }
        
        // 组件系统
        template<typename T>
        T* GetComponent();
        
        template<typename T, typename... Args>
        T* AddComponent(Args&&... args);
        
        template<typename T>
        void RemoveComponent();
        
        // 可见性和渲染
        void SetVisible(bool visible) { m_visible = visible; }
        bool IsVisible() const { return m_visible; }
        
        const BoundingBox& GetBoundingBox() const { return m_boundingBox; }
        void UpdateBoundingBox();
        
    private:
        glm::mat4 m_localTransform{1.0f};
        glm::mat4 m_worldTransform{1.0f};
        SceneNode* m_parent = nullptr;
        std::vector<std::shared_ptr<SceneNode>> m_children;
        std::vector<std::unique_ptr<Component>> m_components;
        
        BoundingBox m_boundingBox;
        bool m_visible = true;
        bool m_transformDirty = true;
    };
    
    void Update(float deltaTime);
    void Render(const Camera& camera);
    std::shared_ptr<SceneNode> GetRootNode() { return m_rootNode; }
    
    // 空间查询
    std::vector<SceneNode*> Query(const BoundingBox& bounds);
    std::vector<SceneNode*> Query(const Frustum& frustum);
    SceneNode* RayQuery(const Ray& ray, float& distance);
    
private:
    std::shared_ptr<SceneNode> m_rootNode;
    std::unique_ptr<SpatialPartitioning> m_spatialTree;  // 八叉树/BSP树
    
    void UpdateTransforms(SceneNode* node);
    void CullAndRender(SceneNode* node, const Camera& camera);
};

// 空间分区系统
class OctreeNode {
public:
    struct OctreeData {
        BoundingBox bounds;
        std::vector<SceneNode*> objects;
        std::array<std::unique_ptr<OctreeNode>, 8> children;
        bool isLeaf = true;
        uint32_t depth = 0;
    };
    
    void Insert(SceneNode* object);
    void Remove(SceneNode* object);
    void Query(const Frustum& frustum, std::vector<SceneNode*>& results);
    void Query(const BoundingBox& bounds, std::vector<SceneNode*>& results);
    
private:
    OctreeData m_data;
    static constexpr uint32_t MAX_OBJECTS_PER_NODE = 10;
    static constexpr uint32_t MAX_DEPTH = 8;
    
    void Subdivide();
    bool ShouldSubdivide() const;
    uint32_t GetChildIndex(const BoundingBox& bounds) const;
};
```

### 7. 骨骼动画系统

```cpp
// 骨骼动画系统
class SkeletalAnimationSystem {
public:
    struct Bone {
        uint32_t id;
        std::string name;
        glm::mat4 offsetMatrix;      // 绑定姿态的逆矩阵
        glm::mat4 localTransform;    // 相对父骨骼的变换
        glm::mat4 globalTransform;   // 世界空间变换
        int32_t parentIndex = -1;
        std::vector<uint32_t> childIndices;
    };
    
    struct AnimationChannel {
        uint32_t boneIndex;
        std::vector<std::pair<float, glm::vec3>> positions;     // 时间-位置
        std::vector<std::pair<float, glm::quat>> rotations;     // 时间-旋转
        std::vector<std::pair<float, glm::vec3>> scales;        // 时间-缩放
    };
    
    struct Animation {
        std::string name;
        float duration;
        float ticksPerSecond;
        std::vector<AnimationChannel> channels;
    };
    
    struct AnimationState {
        std::string currentAnimation;
        float currentTime = 0.0f;
        bool isPlaying = false;
        bool isLooping = true;
        float playbackSpeed = 1.0f;
        
        // 动画混合
        std::string blendTarget;
        float blendWeight = 0.0f;
        float blendDuration = 0.3f;
        float blendTime = 0.0f;
    };
    
    bool LoadSkeleton(const std::string& path);
    bool LoadAnimation(const std::string& path);
    
    void PlayAnimation(const std::string& animName, bool loop = true);
    void BlendToAnimation(const std::string& animName, float blendTime = 0.3f);
    void StopAnimation();
    
    void Update(float deltaTime);
    std::vector<glm::mat4> GetBoneMatrices() const;
    
    // 骨骼查询
    int32_t FindBoneIndex(const std::string& boneName) const;
    glm::mat4 GetBoneTransform(uint32_t boneIndex) const;
    
private:
    std::vector<Bone> m_bones;
    std::unordered_map<std::string, uint32_t> m_boneNameToIndex;
    std::unordered_map<std::string, Animation> m_animations;
    std::unique_ptr<IBuffer> m_boneMatrixBuffer;
    
    AnimationState m_animationState;
    std::vector<glm::mat4> m_finalBoneMatrices;
    
    void CalculateBoneTransforms();
    void UpdateBone(uint32_t boneIndex, const glm::mat4& parentTransform);
    glm::mat4 InterpolateChannel(const AnimationChannel& channel, float time);
    
    glm::vec3 InterpolatePosition(const std::vector<std::pair<float, glm::vec3>>& positions, float time);
    glm::quat InterpolateRotation(const std::vector<std::pair<float, glm::quat>>& rotations, float time);
    glm::vec3 InterpolateScale(const std::vector<std::pair<float, glm::vec3>>& scales, float time);
};

// 动画状态机
class AnimationStateMachine {
public:
    struct AnimationTransition {
        std::string fromState;
        std::string toState;
        std::string triggerName;
        float blendTime = 0.3f;
        std::function<bool()> condition;
    };
    
    struct AnimationStateInfo {
        std::string animationName;
        bool isLooping = true;
        float playbackSpeed = 1.0f;
        std::vector<AnimationTransition> transitions;
    };
    
    void AddState(const std::string& stateName, const AnimationStateInfo& stateInfo);
    void AddTransition(const AnimationTransition& transition);
    void SetTrigger(const std::string& triggerName);
    void SetBool(const std::string& paramName, bool value);
    void SetFloat(const std::string& paramName, float value);
    
    void Update(SkeletalAnimationSystem* animSystem, float deltaTime);
    std::string GetCurrentState() const { return m_currentState; }
    
private:
    std::unordered_map<std::string, AnimationStateInfo> m_states;
    std::unordered_map<std::string, bool> m_boolParams;
    std::unordered_map<std::string, float> m_floatParams;
    std::set<std::string> m_triggers;
    
    std::string m_currentState;
    std::string m_nextState;
    bool m_isTransitioning = false;
    
    void CheckTransitions();
    bool EvaluateCondition(const AnimationTransition& transition);
};
```

### 8. 粒子系统

```cpp
// GPU粒子系统
class ParticleSystem {
public:
    struct ParticleData {
        glm::vec3 position;
        glm::vec3 velocity;
        glm::vec4 color;
        float size;
        float lifetime;
        float age;
        uint32_t textureIndex;
    };
    
    struct ParticleEmitter {
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, 1.0f, 0.0f};
        float emissionRate = 10.0f;          // 每秒发射粒子数
        float emissionAngle = 30.0f;         // 发射角度(度)
        
        // 粒子属性范围
        glm::vec2 velocityRange{1.0f, 5.0f};
        glm::vec2 lifetimeRange{1.0f, 3.0f};
        glm::vec2 sizeRange{0.1f, 0.5f};
        glm::vec4 startColor{1.0f};
        glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
        
        // 物理属性
        glm::vec3 gravity{0.0f, -9.8f, 0.0f};
        float drag = 0.1f;
        
        bool isActive = true;
        uint32_t maxParticles = 1000;
    };
    
    bool Initialize(uint32_t maxParticles);
    void Shutdown();
    
    uint32_t CreateEmitter(const ParticleEmitter& emitterDesc);
    void DestroyEmitter(uint32_t emitterID);
    void UpdateEmitter(uint32_t emitterID, const ParticleEmitter& emitterDesc);
    
    void Update(float deltaTime);
    void Render(const Camera& camera);
    
    // 特效预设
    uint32_t CreateExplosion(const glm::vec3& position, float intensity = 1.0f);
    uint32_t CreateSmoke(const glm::vec3& position, float duration = 5.0f);
    uint32_t CreateFire(const glm::vec3& position);
    
private:
    struct GPUParticleBuffer {
        std::unique_ptr<IBuffer> particleBuffer;
        std::unique_ptr<IBuffer> emitterBuffer;
        std::unique_ptr<IBuffer> aliveListBuffer;
        std::unique_ptr<IBuffer> deadListBuffer;
        std::unique_ptr<IBuffer> indirectArgsBuffer;
    };
    
    GPUParticleBuffer m_buffers;
    std::unique_ptr<IComputeShader> m_emitShader;
    std::unique_ptr<IComputeShader> m_updateShader;
    std::unique_ptr<IComputeShader> m_sortShader;
    std::unique_ptr<IPipeline> m_renderPipeline;
    
    std::unordered_map<uint32_t, ParticleEmitter> m_emitters;
    std::shared_ptr<ITexture> m_particleTexture;
    uint32_t m_maxParticles;
    uint32_t m_nextEmitterID = 1;
    
    void EmitParticles(float deltaTime);
    void UpdateParticles(float deltaTime);
    void SortParticles(const Camera& camera);
};

// 体积粒子系统 (用于雾、烟雾等效果)
class VolumetricParticleSystem {
public:
    struct VolumetricEmitter {
        glm::vec3 position;
        glm::vec3 size{1.0f};            // 体积大小
        float density = 1.0f;            // 密度
        glm::vec4 color{1.0f};
        float scattering = 0.5f;         // 散射系数
        float absorption = 0.1f;         // 吸收系数
        glm::vec3 windDirection{0.0f};
        float windStrength = 0.0f;
    };
    
    void AddVolumetricEmitter(const VolumetricEmitter& emitter);
    void Update(float deltaTime);
    void RenderVolumetric(const Camera& camera);
    
private:
    std::vector<VolumetricEmitter> m_volumetricEmitters;
    std::unique_ptr<ITexture> m_volumeTexture;        // 3D纹理
    std::unique_ptr<IComputeShader> m_volumeShader;
    std::unique_ptr<IPipeline> m_volumeRenderPipeline;
};
```

### 9. 后处理管线

```cpp
// 后处理效果系统
class PostProcessPipeline {
public:
    // 后处理效果基类
    class IPostEffect {
    public:
        virtual ~IPostEffect() = default;
        virtual void Process(ITexture* input, ITexture* output) = 0;
        virtual void SetEnabled(bool enabled) { m_enabled = enabled; }
        virtual bool IsEnabled() const { return m_enabled; }
        
    protected:
        bool m_enabled = true;
    };
    
    bool Initialize();
    void Shutdown();
    
    void AddEffect(std::unique_ptr<IPostEffect> effect);
    void RemoveEffect(size_t index);
    void Process(ITexture* inputTexture, ITexture* outputTexture);
    
    // 内置效果控制
    void EnableBloom(bool enable);
    void EnableToneMapping(bool enable);
    void EnableFXAA(bool enable);
    void EnableSSAO(bool enable);
    void EnableMotionBlur(bool enable);
    
    // 参数设置
    void SetExposure(float exposure);
    void SetBloomThreshold(float threshold);
    void SetBloomIntensity(float intensity);
    void SetGamma(float gamma);
    
private:
    std::vector<std::unique_ptr<IPostEffect>> m_effects;
    std::vector<std::unique_ptr<IFramebuffer>> m_intermediateBuffers;
    std::unique_ptr<ITexture> m_tempTexture1;
    std::unique_ptr<ITexture> m_tempTexture2;
    
    // 内置效果
    std::unique_ptr<BloomEffect> m_bloomEffect;
    std::unique_ptr<ToneMappingEffect> m_toneMappingEffect;
    std::unique_ptr<FXAAEffect> m_fxaaEffect;
    std::unique_ptr<SSAOEffect> m_ssaoEffect;
    std::unique_ptr<MotionBlurEffect> m_motionBlurEffect;
};

// HDR泛光效果
class BloomEffect : public PostProcessPipeline::IPostEffect {
public:
    struct BloomSettings {
        float threshold = 1.0f;        // 亮度阈值
        float intensity = 1.0f;        // 泛光强度
        uint32_t iterations = 5;       // 模糊迭代次数
        float scatter = 0.7f;          // 散布强度
    };
    
    bool Initialize();
    void Process(ITexture* input, ITexture* output) override;
    void SetSettings(const BloomSettings& settings) { m_settings = settings; }
    
private:
    BloomSettings m_settings;
    std::unique_ptr<IPipeline> m_thresholdPipeline;
    std::unique_ptr<IPipeline> m_blurPipeline;
    std::unique_ptr<IPipeline> m_combinePipeline;
    std::vector<std::unique_ptr<ITexture>> m_mipChain;
    
    void ExtractBrightPixels(ITexture* input, ITexture* output);
    void GaussianBlur(ITexture* texture);
    void CombineBloom(ITexture* original, ITexture* bloom, ITexture* output);
};

// 屏幕空间环境遮挡
class SSAOEffect : public PostProcessPipeline::IPostEffect {
public:
    struct SSAOSettings {
        float radius = 0.5f;           // 采样半径
        float bias = 0.025f;           // 偏移值
        float intensity = 1.0f;        // 强度
        uint32_t kernelSize = 64;      // 采样核心大小
        float noiseScale = 4.0f;       // 噪声缩放
    };
    
    bool Initialize();
    void Process(ITexture* input, ITexture* output) override;
    void SetSettings(const SSAOSettings& settings) { m_settings = settings; }
    void SetGBuffer(ITexture* normalTexture, ITexture* depthTexture);
    
private:
    SSAOSettings m_settings;
    std::unique_ptr<IPipeline> m_ssaoPipeline;
    std::unique_ptr<IPipeline> m_blurPipeline;
    std::unique_ptr<ITexture> m_noiseTexture;
    std::unique_ptr<IBuffer> m_kernelBuffer;
    
    ITexture* m_normalTexture = nullptr;
    ITexture* m_depthTexture = nullptr;
    
    void GenerateKernel();
    void GenerateNoiseTexture();
};

// 色调映射
class ToneMappingEffect : public PostProcessPipeline::IPostEffect {
public:
    enum class ToneMappingMode {
        Reinhard,
        Filmic,
        ACES,
        Exposure
    };
    
    struct ToneMappingSettings {
        ToneMappingMode mode = ToneMappingMode::ACES;
        float exposure = 1.0f;
        float gamma = 2.2f;
    };
    
    bool Initialize();
    void Process(ITexture* input, ITexture* output) override;
    void SetSettings(const ToneMappingSettings& settings) { m_settings = settings; }
    
private:
    ToneMappingSettings m_settings;
    std::unique_ptr<IPipeline> m_toneMappingPipeline;
};
```

## 光照和材质系统

### PBR材质系统

```cpp
// 物理基础渲染材质
class PBRMaterial {
public:
    struct MaterialProperties {
        glm::vec3 albedo{1.0f};           // 反照率
        float metallic = 0.0f;            // 金属度
        float roughness = 0.5f;           // 粗糙度
        float ao = 1.0f;                  // 环境遮蔽
        glm::vec3 emissive{0.0f};         // 自发光
        float emissiveStrength = 1.0f;    // 自发光强度
        
        // 纹理贴图
        std::shared_ptr<ITexture> albedoMap;
        std::shared_ptr<ITexture> normalMap;
        std::shared_ptr<ITexture> metallicMap;
        std::shared_ptr<ITexture> roughnessMap;
        std::shared_ptr<ITexture> aoMap;
        std::shared_ptr<ITexture> emissiveMap;
    };
    
    bool Initialize(const MaterialProperties& properties);
    void Bind(uint32_t slot);
    void UpdateProperties(const MaterialProperties& properties);
    const MaterialProperties& GetProperties() const { return m_properties; }
    
private:
    MaterialProperties m_properties;
    std::unique_ptr<IBuffer> m_materialBuffer;
    uint32_t m_materialID;
};

// 光照系统
class LightingSystem {
public:
    enum class LightType {
        Directional,
        Point,
        Spot,
        Area
    };
    
    struct DirectionalLight {
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        bool castShadows = true;
        
        // 阴影相关
        float shadowDistance = 100.0f;
        uint32_t cascadeCount = 4;
        std::array<float, 4> cascadeSplits{7.0f, 25.0f, 50.0f, 100.0f};
    };
    
    struct PointLight {
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float radius = 10.0f;
        bool castShadows = false;
    };
    
    struct SpotLight {
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float innerCone = 30.0f;    // 内锥角(度)
        float outerCone = 45.0f;    // 外锥角(度)
        float range = 10.0f;
        bool castShadows = false;
    };
    
    uint32_t AddDirectionalLight(const DirectionalLight& light);
    uint32_t AddPointLight(const PointLight& light);
    uint32_t AddSpotLight(const SpotLight& light);
    
    void RemoveLight(uint32_t lightID);
    void UpdateLight(uint32_t lightID, const DirectionalLight& light);
    void UpdateLight(uint32_t lightID, const PointLight& light);
    void UpdateLight(uint32_t lightID, const SpotLight& light);
    
    void SetAmbientLight(const glm::vec3& color, float intensity);
    void SetSkyboxIntensity(float intensity);
    
    void Update();
    void BindLightData(uint32_t slot);
    
private:
    std::vector<DirectionalLight> m_directionalLights;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
    
    glm::vec3 m_ambientColor{0.1f};
    float m_ambientIntensity = 1.0f;
    float m_skyboxIntensity = 1.0f;
    
    std::unique_ptr<IBuffer> m_lightBuffer;
    std::unique_ptr<IBuffer> m_lightIndicesBuffer;
    uint32_t m_nextLightID = 1;
    
    static constexpr uint32_t MAX_LIGHTS = 256;
    
    void UpdateLightBuffer();
};

// IBL环境光照
class IBLSystem {
public:
    bool Initialize();
    void LoadHDREnvironment(const std::string& hdrPath);
    void GenerateIrradianceMap();
    void GeneratePrefilteredMap();
    void GenerateBRDFLut();
    
    void BindEnvironmentMaps(uint32_t slot);
    
private:
    std::shared_ptr<ITexture> m_hdrEnvironment;      // HDR环境贴图
    std::shared_ptr<ITexture> m_irradianceMap;       // 漫反射辐照度图
    std::shared_ptr<ITexture> m_prefilteredMap;      // 预过滤环境图
    std::shared_ptr<ITexture> m_brdfLut;             // BRDF查找表
    
    std::unique_ptr<IComputeShader> m_irradianceShader;
    std::unique_ptr<IComputeShader> m_prefilterShader;
    std::unique_ptr<IComputeShader> m_brdfShader;
};
```

### 阴影渲染系统

```cpp
// 阴影渲染系统
class ShadowRenderSystem {
public:
    enum class ShadowTechnique {
        BasicShadowMap,
        VarianceShadowMap,
        CascadedShadowMap,
        ExponentialShadowMap
    };
    
    struct ShadowMapDesc {
        uint32_t resolution = 2048;
        ShadowTechnique technique = ShadowTechnique::CascadedShadowMap;
        uint32_t cascadeCount = 4;
        float cascadeLambda = 0.5f;     // 级联分布参数
        float shadowBias = 0.005f;
        float normalBias = 0.1f;
        bool enablePCF = true;          // 百分比最近过滤
        uint32_t pcfSamples = 4;
    };
    
    bool Initialize(const ShadowMapDesc& desc);
    void Shutdown();
    
    void BeginShadowPass(uint32_t lightID);
    void EndShadowPass();
    void RenderShadowCasters(const std::vector<SceneNode*>& casters);
    
    void BindShadowMaps(uint32_t slot);
    std::vector<glm::mat4> GetShadowMatrices(uint32_t lightID) const;
    
private:
    ShadowMapDesc m_desc;
    
    struct ShadowMapData {
        std::unique_ptr<ITexture> shadowMap;
        std::unique_ptr<IFramebuffer> framebuffer;
        std::vector<glm::mat4> lightMatrices;
        glm::mat4 lightView;
        glm::mat4 lightProjection;
    };
    
    std::unordered_map<uint32_t, ShadowMapData> m_shadowMaps;
    std::unique_ptr<IPipeline> m_shadowPipeline;
    std::unique_ptr<IBuffer> m_shadowMatrixBuffer;
    
    uint32_t m_currentLightID = 0;
    
    void CreateShadowMap(uint32_t lightID, const DirectionalLight& light);
    void CreateCascadedShadowMap(uint32_t lightID, const DirectionalLight& light);
    void UpdateCascadeMatrices(uint32_t lightID, const Camera& camera, const DirectionalLight& light);
    std::vector<float> CalculateCascadeSplits(float nearPlane, float farPlane, uint32_t cascadeCount);
};

// 软阴影实现
class SoftShadowRenderer {
public:
    enum class SoftShadowTechnique {
        PCF,                    // 百分比最近过滤
        PCSS,                   // 百分比最近软阴影
        VSM,                    // 方差阴影映射
        MSM                     // 矩阴影映射
    };
    
    struct SoftShadowSettings {
        SoftShadowTechnique technique = SoftShadowTechnique::PCSS;
        float lightSize = 1.0f;         // 光源大小(用于PCSS)
        uint32_t sampleCount = 16;      // 采样数量
        float minFilterSize = 1.0f;     // 最小过滤尺寸
        float maxFilterSize = 10.0f;    // 最大过滤尺寸
    };
    
    void RenderSoftShadows(const SoftShadowSettings& settings);
    
private:
    std::unique_ptr<IComputeShader> m_pcfShader;
    std::unique_ptr<IComputeShader> m_pcssShader;
    std::unique_ptr<IComputeShader> m_vsmShader;
};
```

## 高性能渲染技术

### GPU-Driven渲染

```cpp
// GPU驱动渲染系统
class GPUDrivenRenderer {
public:
    struct DrawCommand {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t vertexOffset;
        uint32_t firstInstance;
        uint32_t materialID;
        uint32_t transformID;
    };
    
    struct CullData {
        glm::mat4 mvpMatrix;
        glm::vec4 boundingSphere;   // xyz=center, w=radius
        uint32_t instanceID;
        uint32_t materialID;
    };
    
    bool Initialize();
    void Shutdown();
    
    void SetCamera(const Camera& camera);
    void AddMeshBatch(const MeshBatch& batch);
    void Render();
    
    // 性能统计
    uint32_t GetTotalDrawCalls() const { return m_totalDrawCalls; }
    uint32_t GetCulledDrawCalls() const { return m_culledDrawCalls; }
    uint32_t GetRenderedTriangles() const { return m_renderedTriangles; }
    
private:
    // GPU缓冲区
    std::unique_ptr<IBuffer> m_drawCommandBuffer;
    std::unique_ptr<IBuffer> m_cullDataBuffer;
    std::unique_ptr<IBuffer> m_visibilityBuffer;
    std::unique_ptr<IBuffer> m_indirectArgsBuffer;
    
    // 计算着色器
    std::unique_ptr<IComputeShader> m_frustumCullShader;
    std::unique_ptr<IComputeShader> m_occlusionCullShader;
    std::unique_ptr<IComputeShader> m_compactShader;
    
    // 渲染统计
    uint32_t m_totalDrawCalls = 0;
    uint32_t m_culledDrawCalls = 0;
    uint32_t m_renderedTriangles = 0;
    
    void FrustumCull();
    void OcclusionCull();
    void CompactDrawCommands();
};

// 实例化渲染系统
class InstancedRenderer {
public:
    struct InstanceData {
        glm::mat4 worldMatrix;
        glm::mat4 normalMatrix;
        uint32_t materialID;
        glm::vec4 customData;
    };
    
    void AddInstanceBatch(const Mesh& mesh, const std::vector<InstanceData>& instances);
    void RenderInstanced();
    void ClearBatches();
    
private:
    struct InstanceBatch {
        std::shared_ptr<Mesh> mesh;
        std::vector<InstanceData> instances;
        std::unique_ptr<IBuffer> instanceBuffer;
    };
    
    std::vector<InstanceBatch> m_batches;
    std::unique_ptr<IPipeline> m_instancedPipeline;
};

// LOD系统
class LODSystem {
public:
    struct LODLevel {
        std::shared_ptr<Mesh> mesh;
        float distance;             // 切换距离
        float hysteresis = 0.1f;    // 迟滞值，防止抖动
    };
    
    struct LODObject {
        std::vector<LODLevel> levels;
        glm::vec3 position;
        float boundingRadius;
        uint32_t currentLOD = 0;
        bool visible = true;
    };
    
    uint32_t RegisterLODObject(const std::vector<LODLevel>& levels);
    void UpdateLOD(const Camera& camera);
    void RemoveLODObject(uint32_t objectID);
    
    const LODObject& GetLODObject(uint32_t objectID) const;
    
private:
    std::unordered_map<uint32_t, LODObject> m_lodObjects;
    uint32_t m_nextObjectID = 1;
    
    uint32_t CalculateLOD(const LODObject& object, const Camera& camera);
};
```

### 多线程渲染架构

```cpp
// 渲染作业系统
class RenderJobSystem {
public:
    enum class JobType {
        CullingJob,
        ShadowJob,
        GeometryJob,
        LightingJob,
        PostProcessJob
    };
    
    class IRenderJob {
    public:
        virtual ~IRenderJob() = default;
        virtual void Execute() = 0;
        virtual JobType GetType() const = 0;
        virtual std::vector<JobType> GetDependencies() const { return {}; }
    };
    
    bool Initialize(uint32_t threadCount = 0);  // 0 = 自动检测
    void Shutdown();
    
    void SubmitJob(std::unique_ptr<IRenderJob> job);
    void SubmitJobBatch(std::vector<std::unique_ptr<IRenderJob>> jobs);
    void WaitForCompletion();
    void ExecuteFrame();
    
private:
    class ThreadPool;
    std::unique_ptr<ThreadPool> m_threadPool;
    std::vector<std::unique_ptr<IRenderJob>> m_frameJobs;
    std::atomic<bool> m_shutdown{false};
    
    void SortJobsByDependencies();
    bool CanExecuteJob(const IRenderJob* job, const std::set<JobType>& completedJobs);
};

// 多线程命令缓冲区记录
class MultiThreadedCommandRecorder {
public:
    struct RecordingContext {
        VkCommandBuffer commandBuffer;
        VkRenderPass renderPass;
        VkFramebuffer framebuffer;
        uint32_t threadID;
    };
    
    bool Initialize(uint32_t threadCount);
    void BeginRecording();
    void EndRecording();
    
    RecordingContext* GetContext(uint32_t threadID);
    void SubmitSecondaryBuffers(VkCommandBuffer primaryBuffer);
    
private:
    struct ThreadContext {
        VkCommandPool commandPool;
        std::vector<VkCommandBuffer> commandBuffers;
        uint32_t currentBuffer = 0;
    };
    
    std::vector<ThreadContext> m_threadContexts;
    std::vector<VkCommandBuffer> m_activeBuffers;
    uint32_t m_frameIndex = 0;
    
    void CreateThreadContext(uint32_t threadID);
};
```

## 常见问题排查

### Vulkan相关问题

#### 验证层错误
```cpp
// 常见验证层错误及解决方案

1. **VUID-VkGraphicsPipelineCreateInfo-layout-00756**
   - 问题：管线布局与着色器不匹配
   - 解决：检查DescriptorSetLayout是否正确绑定所有着色器资源

2. **VUID-vkCmdDraw-None-02697**
   - 问题：绘制时缺少必要的描述符集
   - 解决：确保在绘制前绑定所有必要的描述符集

3. **VUID-VkImageCreateInfo-extent-00944**
   - 问题：纹理尺寸超出设备限制
   - 解决：查询设备限制并相应调整纹理大小

// 调试工具
class VulkanDebugUtils {
public:
    static void SetObjectName(VkDevice device, VkObjectType objectType, 
                             uint64_t objectHandle, const char* name);
    static void BeginDebugLabel(VkCommandBuffer cmd, const char* labelName, 
                               const float color[4]);
    static void EndDebugLabel(VkCommandBuffer cmd);
    
    // 内存泄漏检测
    static void CheckMemoryLeaks();
    static void PrintMemoryStats();
};
```

#### 性能问题排查
```cpp
// 性能分析工具
class PerformanceProfiler {
public:
    struct FrameStats {
        float frameTime;        // 帧时间(ms)
        float cpuTime;          // CPU时间(ms)
        float gpuTime;          // GPU时间(ms)
        uint32_t drawCalls;     // 绘制调用数
        uint32_t triangles;     // 三角形数量
        uint64_t gpuMemoryUsed; // GPU内存使用
    };
    
    void BeginFrame();
    void EndFrame();
    void BeginGPUEvent(const std::string& name);
    void EndGPUEvent();
    
    const FrameStats& GetFrameStats() const { return m_frameStats; }
    void PrintPerformanceReport();
    
private:
    FrameStats m_frameStats;
    std::vector<VkQueryPool> m_timestampPools;
    std::stack<std::string> m_gpuEventStack;
    std::chrono::high_resolution_clock::time_point m_frameStart;
};

// 常见性能问题
/*
1. **绘制调用过多**
   - 使用实例化渲染减少draw call
   - 合并相同材质的网格
   - 使用GPU-driven渲染

2. **带宽瓶颈**
   - 减少纹理尺寸和格式
   - 使用压缩纹理格式
   - 优化顶点数据布局

3. **填充率瓶颈**
   - 减少overdraw
   - 使用early-Z测试
   - 优化像素着色器复杂度

4. **内存碎片**
   - 使用内存池分配器
   - 预分配大块内存
   - 定期整理内存
*/
```

#### 编译问题解决
```cmake
# 常见CMake问题解决

# 1. Vulkan SDK未找到
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Please install Vulkan SDK and set VULKAN_SDK environment variable.")
endif()

# 2. Shaderc库链接问题
if(WIN32)
    # Windows下可能需要额外的库
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        ${SHADERC_LIB}
        shaderc_util
        SPIRV-Tools
        SPIRV-Tools-opt
    )
endif()

# 3. 头文件路径问题
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
    ${Vulkan_INCLUDE_DIRS}
)

# 4. 运行时依赖
if(WIN32)
    # 复制DLL到输出目录
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${VULKAN_SDK}/Bin/shaderc_shared.dll"
        $<TARGET_FILE_DIR:${PROJECT_NAME}>)
endif()
```

## 开发最佳实践

### 代码组织原则

#### 1. 模块化设计
```cpp
// 良好的模块划分
namespace Orange::Graphics {
    // 核心接口层
    namespace Interface {
        class IGraphicsSystem;
        class IRenderDevice;
        class IBuffer;
        class ITexture;
    }
    
    // 具体实现层
    namespace Vulkan {
        class VulkanGraphicsSystem;
        class VulkanDevice;
        class VulkanBuffer;
    }
    
    // 高级功能层
    namespace Rendering {
        class SceneRenderer;
        class PostProcessor;
        class MaterialSystem;
    }
}

// 依赖注入模式
class RenderSystemFactory {
public:
    static std::unique_ptr<IGraphicsSystem> CreateGraphicsSystem(GraphicsAPI api);
    static std::unique_ptr<IRenderDevice> CreateRenderDevice(GraphicsAPI api);
};
```

#### 2. 资源管理最佳实践
```cpp
// RAII资源管理
class VulkanBuffer {
public:
    VulkanBuffer(VkDevice device, const BufferCreateInfo& info)
        : m_device(device) {
        // 创建资源
        CreateBuffer(info);
    }
    
    ~VulkanBuffer() {
        // 自动清理资源
        if (m_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_device, m_buffer, nullptr);
        }
        if (m_memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_device, m_memory, nullptr);
        }
    }
    
    // 禁止拷贝，允许移动
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;
    
private:
    VkDevice m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
};

// 智能指针使用
class ResourceManager {
public:
    std::shared_ptr<ITexture> LoadTexture(const std::string& path);
    std::shared_ptr<Mesh> LoadMesh(const std::string& path);
    
private:
    std::unordered_map<std::string, std::weak_ptr<ITexture>> m_textureCache;
    std::unordered_map<std::string, std::weak_ptr<Mesh>> m_meshCache;
};
```

#### 3. 错误处理策略
```cpp
// 结果类型模式
template<typename T>
class Result {
public:
    static Result Success(T&& value) {
        return Result(std::move(value), true);
    }
    
    static Result Error(const std::string& message) {
        return Result(message);
    }
    
    bool IsSuccess() const { return m_success; }
    const T& GetValue() const { 
        assert(m_success);
        return m_value; 
    }
    const std::string& GetError() const { 
        assert(!m_success);
        return m_error; 
    }
    
private:
    Result(T&& value, bool success) 
        : m_value(std::move(value)), m_success(success) {}
    Result(const std::string& error) 
        : m_error(error), m_success(false) {}
    
    T m_value;
    std::string m_error;
    bool m_success;
};

// 使用示例
Result<std::unique_ptr<ITexture>> TextureLoader::LoadTexture(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        return Result<std::unique_ptr<ITexture>>::Error("Texture file not found: " + path);
    }
    
    auto texture = std::make_unique<VulkanTexture>();
    if (!texture->LoadFromFile(path)) {
        return Result<std::unique_ptr<ITexture>>::Error("Failed to load texture: " + path);
    }
    
    return Result<std::unique_ptr<ITexture>>::Success(std::move(texture));
}
```

### 性能优化指南

#### 1. 内存管理优化
```cpp
// 内存池分配器
class MemoryPool {
public:
    MemoryPool(size_t blockSize, size_t blockCount);
    
    void* Allocate();
    void Deallocate(void* ptr);
    
    // 统计信息
    size_t GetUsedBlocks() const { return m_usedBlocks; }
    size_t GetTotalBlocks() const { return m_totalBlocks; }
    float GetUsageRatio() const { return float(m_usedBlocks) / m_totalBlocks; }
    
private:
    struct Block {
        Block* next;
        bool inUse;
    };
    
    std::vector<uint8_t> m_memory;
    Block* m_freeList;
    size_t m_blockSize;
    size_t m_totalBlocks;
    size_t m_usedBlocks;
};

// 对象池模式
template<typename T>
class ObjectPool {
public:
    template<typename... Args>
    T* Acquire(Args&&... args) {
        if (m_available.empty()) {
            return new T(std::forward<Args>(args)...);
        }
        
        T* obj = m_available.back();
        m_available.pop_back();
        new(obj) T(std::forward<Args>(args)...);  // placement new
        return obj;
    }
    
    void Release(T* obj) {
        obj->~T();
        m_available.push_back(obj);
    }
    
private:
    std::vector<T*> m_available;
};
```

#### 2. 渲染优化技巧
```cpp
// 批量渲染
class RenderBatcher {
public:
    void AddDrawCommand(const DrawCommand& cmd);
    void Sort();  // 按状态排序减少状态切换
    void Execute();
    void Clear();
    
private:
    std::vector<DrawCommand> m_commands;
    
    static bool CompareDrawCommands(const DrawCommand& a, const DrawCommand& b) {
        // 1. 按材质排序
        if (a.materialID != b.materialID) {
            return a.materialID < b.materialID;
        }
        // 2. 按深度排序（透明物体从后往前，不透明从前往后）
        if (a.isTransparent != b.isTransparent) {
            return !a.isTransparent;  // 不透明物体优先
        }
        if (a.isTransparent) {
            return a.depth > b.depth;  // 透明物体从后往前
        } else {
            return a.depth < b.depth;  // 不透明物体从前往后
        }
    }
};

// 视锥剔除优化
class FrustumCuller {
public:
    std::vector<SceneNode*> CullObjects(
        const std::vector<SceneNode*>& objects,
        const Frustum& frustum) {
        
        std::vector<SceneNode*> visible;
        visible.reserve(objects.size());
        
        for (SceneNode* obj : objects) {
            const BoundingBox& bounds = obj->GetBoundingBox();
            if (frustum.Intersects(bounds)) {
                visible.push_back(obj);
            }
        }
        
        return visible;
    }
    
    // 分层剔除：先用包围球快速剔除，再用包围盒精确剔除
    bool QuickCull(const BoundingSphere& sphere, const Frustum& frustum);
    bool PreciseCull(const BoundingBox& box, const Frustum& frustum);
};
```

## 项目开发计划

### 🏗️ **架构层次重新设计** 

#### **Layer1 (Platform Abstraction Layer) - 已完成** ✅
```cpp
// 平台抽象层 - 保持不变
Src/OrangeEngine/Layer1/Platform/Graphics/
├── GraphicsInterface/           # 图形接口抽象
├── GraphicsAPI/Vulkan/         # Vulkan底层实现
└── VulkanShaderCompiler        # 着色器编译服务 (保留在Layer1)
```

#### **Layer2 (Low-Level Renderer Layer) - 当前开发重点** 🎯
```cpp
// 新建：底层渲染器层
Src/OrangeEngine/Layer2/RenderCore/
├── Camera/                     # 相机系统
│   ├── Camera.h/.cpp          # 视图/投影矩阵，视锥体计算
│   └── CameraController.h/.cpp # FPS/轨道相机控制
├── Material/                   # 材质与着色器管理 (Layer2职责)
│   ├── Material.h/.cpp        # 材质属性和渲染状态
│   ├── Shader.h/.cpp          # 高级着色器管理 (基于Layer1编译器)
│   ├── ShaderLibrary.h/.cpp   # 着色器资源管理
├── Geometry/                   # 几何体与网格
│   ├── Mesh.h/.cpp            # 顶点/索引数据，VAO/VBO管理
│   ├── MeshGenerator.h/.cpp   # 立方体、球体、平面生成器
│   └── VertexBuffer.h/.cpp    # 顶点数据封装
├── Texture/                    # 纹理与表面管理
│   ├── Texture2D.h/.cpp       # 纹理资源生命周期
│   ├── TextureManager.h/.cpp  # 纹理缓存和流式加载
│   └── TextureLoader.h/.cpp   # PNG/JPG等格式加载
├── Rendering/                  # 图元提交系统
│   ├── Renderer.h/.cpp        # 核心渲染器
│   ├── RenderQueue.h/.cpp     # 渲染队列和批处理
│   └── RenderCommand.h/.cpp   # 渲染命令封装
└── Viewport/                   # 视口与虚拟屏幕
    ├── Viewport.h/.cpp        # 视口管理
    └── RenderTarget.h/.cpp    # 渲染目标管理
```

### 第四阶段：性能优化 (2-3周)
- [ ] GPU-driven渲染
- [ ] 实例化渲染
- [ ] 多线程渲染
- [ ] 内存优化
- [ ] 性能分析工具

### 第五阶段：编辑器集成 (3-4周)
- [ ] 场景编辑器
- [ ] 材质编辑器
- [ ] 着色器编辑器
- [ ] 性能监控工具
- [ ] 资源浏览器

### 里程碑目标
1. **MVP版本** (6周后): 基础三角形/立方体渲染
2. **Alpha版本** (12周后): 完整PBR渲染管线
3. **Beta版本** (18周后): 编辑器集成完成
4. **Release版本** (24周后): 性能优化和稳定性测试完成

## 附录

### A. Vulkan扩展支持列表
```cpp
// 必需扩展
static const std::vector<const char*> REQUIRED_DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// 可选扩展（提供额外功能）
static const std::vector<const char*> OPTIONAL_DEVICE_EXTENSIONS = {
    VK_KHR_MAINTENANCE1_EXTENSION_NAME,     // 额外的管线功能
    VK_KHR_MAINTENANCE2_EXTENSION_NAME,     // 输入附件和点大小
    VK_KHR_MAINTENANCE3_EXTENSION_NAME,     // 描述符索引
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,  // bindless渲染
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,  // 缓冲区设备地址
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,   // 光线追踪
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, // 加速结构
    VK_EXT_MESH_SHADER_EXTENSION_NAME,      // 网格着色器
    VK_NV_MESH_SHADER_EXTENSION_NAME,       // NVIDIA网格着色器
};
```

### B. 着色器代码规范
```glsl
// 顶点着色器模板
#version 450

// 输入布局
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

// 输出到片段着色器
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;

// 统一缓冲区
layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec3 cameraPos;
} camera;

layout(binding = 1) uniform ObjectUBO {
    mat4 model;
    mat4 normalMatrix;
} object;

void main() {
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // 变换法线和切线到世界空间
    fragNormal = normalize((object.normalMatrix * vec4(inNormal, 0.0)).xyz);
    fragTangent = normalize((object.model * vec4(inTangent, 0.0)).xyz);
    fragBitangent = cross(fragNormal, fragTangent);
    
    fragTexCoord = inTexCoord;
    
    gl_Position = camera.viewProjection * worldPos;
}
```

### C. 性能基准测试
```cpp
// 性能测试套件
class PerformanceBenchmark {
public:
    struct BenchmarkResult {
        std::string testName;
        float averageFrameTime;
        float minFrameTime;
        float maxFrameTime;
        uint32_t totalFrames;
        uint64_t totalGPUMemory;
    };
    
    void RunBenchmarkSuite();
    void RunDrawCallBenchmark();
    void RunTextureLoadBenchmark();
    void RunShadowMapBenchmark();
    void RunPostProcessBenchmark();
    
    const std::vector<BenchmarkResult>& GetResults() const { return m_results; }
    void ExportResults(const std::string& filename);
    
private:
    std::vector<BenchmarkResult> m_results;
    PerformanceProfiler m_profiler;
};

// 目标性能指标
/*
- 1080p@60FPS: 基础PBR渲染 + 阴影
- 1440p@60FPS: 简化特效设置
- 4K@30FPS: 高质量设置
- VR@90FPS: 专门优化的VR渲染路径

内存使用目标：
- GPU内存: < 4GB (主流显卡)
- 系统内存: < 2GB (引擎部分)
- 启动时间: < 10秒
*/
```

### D. 调试工具清单
```cpp
// 调试可视化
class DebugRenderer {
public:
    void DrawWireframe(const Mesh& mesh, const glm::vec4& color);
    void DrawBoundingBox(const BoundingBox& box, const glm::vec4& color);
    void DrawFrustum(const Frustum& frustum, const glm::vec4& color);
    void DrawLight(const PointLight& light);
    void DrawGrid(float size, uint32_t divisions);
    void DrawAxis(const glm::mat4& transform, float scale = 1.0f);
    
    // 性能可视化
    void DrawPerformanceGraph();
    void DrawMemoryUsage();
    void DrawGPUTimings();
};

// ImGui集成
class EditorGUI {
public:
    void ShowSceneHierarchy();
    void ShowPropertyPanel();
    void ShowPerformancePanel();
    void ShowMaterialEditor();
    void ShowShaderEditor();
    void ShowLogPanel();
};
```

---

**开发建议**：
1. 优先实现MVP版本，确保基础功能稳定
2. 每个模块都要有对应的单元测试
3. 定期进行性能测试和内存泄漏检查
4. 保持代码文档和注释的及时更新
5. 使用版本控制记录重要的设计决策

---


## 下一阶段开发规划

### 🏗️ **架构层次重新设计** 

#### **Layer1 (Platform Abstraction Layer) - 已完成** ✅
```cpp
// 平台抽象层 - 保持不变
Src/OrangeEngine/Layer1/Platform/Graphics/
├── GraphicsInterface/           # 图形接口抽象
├── GraphicsAPI/Vulkan/         # Vulkan底层实现
└── VulkanShaderCompiler        # 着色器编译服务 (保留在Layer1)
```

#### **Layer2 (Low-Level Renderer Layer) - 当前开发重点** 🎯
```cpp
// 新建：底层渲染器层
Src/OrangeEngine/Layer2/RenderCore/
├── Camera/                     # 相机系统
│   ├── Camera.h/.cpp          # 视图/投影矩阵，视锥体计算
│   └── CameraController.h/.cpp # FPS/轨道相机控制
├── Material/                   # 材质与着色器管理 (Layer2职责)
│   ├── Material.h/.cpp        # 材质属性和渲染状态
│   ├── Shader.h/.cpp          # 高级着色器管理 (基于Layer1编译器)
│   ├── ShaderLibrary.h/.cpp   # 着色器资源管理
├── Geometry/                   # 几何体与网格
│   ├── Mesh.h/.cpp            # 顶点/索引数据，VAO/VBO管理
│   ├── MeshGenerator.h/.cpp   # 立方体、球体、平面生成器
│   └── VertexBuffer.h/.cpp    # 顶点数据封装
├── Texture/                    # 纹理与表面管理
│   ├── Texture2D.h/.cpp       # 纹理资源生命周期
│   ├── TextureManager.h/.cpp  # 纹理缓存和流式加载
│   └── TextureLoader.h/.cpp   # PNG/JPG等格式加载
├── Rendering/                  # 图元提交系统
│   ├── Renderer.h/.cpp        # 核心渲染器
│   ├── RenderQueue.h/.cpp     # 渲染队列和批处理
│   └── RenderCommand.h/.cpp   # 渲染命令封装
└── Viewport/                   # 视口与虚拟屏幕
    ├── Viewport.h/.cpp        # 视口管理
    └── RenderTarget.h/.cpp    # 渲染目标管理
```

#### **Mid-Level Functional Systems Layer - 未来开发** 🔮
```cpp
// 中层功能系统 (基于Layer2构建的高级功能)
Src/OrangeEngine/Layer3/Systems/
├── SkeletalAnimation/         # 骨骼动画系统
├── SceneGraph/               # 场景图与裁剪优化  
├── VisualEffects/            # 视觉特效 (HDR, 粒子, 后处理)
├── ProfilingDebugging/       # 性能分析与调试
├── OcclusionPVS/            # 遮挡与潜在可见集
├── LODSystem/               # 细节层次系统
└── AnimationIK/             # 动画IK与插值
```

---

### 🎯 **Layer2 RenderCore开发计划 (8-10周完成)**

#### **第1阶段：RenderCore基础架构** (2-3周)

##### **Week 1-2: 相机与几何体系统**
```cpp
✅ 优先级1: Camera System
├── Camera类 - 视图/投影矩阵计算
├── CameraController类 - FPS风格控制
├── 视锥体计算 (为后续裁剪准备)
└── 相机动画支持

✅ 优先级2: Geometry System  
├── Mesh类 - 顶点/索引数据管理
├── 标准Vertex结构体设计
├── MeshGenerator - 基础几何体生成
└── 包围盒计算
```

##### **Week 3: 材质与着色器管理**
```cpp
🎯 优先级3: Material & Shader Management
├── Shader类 - 基于Layer1编译器的高级封装
├── ShaderLibrary - 着色器资源管理和缓存
├── Material类 - PBR材质属性管理
└── 渲染状态管理
```

#### **第2阶段：纹理与渲染管线** (3-4周)

##### **Week 4-5: 纹理系统**
```cpp
📋 优先级4: Texture Management
├── Texture2D类 - 纹理资源生命周期
├── TextureLoader - PNG/JPG/TGA格式支持
├── TextureManager - 缓存和异步加载
└── Mipmap生成和纹理压缩
```

##### **Week 6-7: 渲染系统完善**
```cpp
📋 优先级5: Rendering Pipeline
├── Renderer类 - 统一渲染接口
├── RenderQueue - 渲染排序和批处理
├── RenderCommand - 命令模式封装
└── Viewport管理 - 多视口支持
```

#### **第3阶段：统一缓冲区与优化** (2-3周)

##### **Week 8-9: UBO系统与优化**
```cpp
📋 优先级6: Uniform Buffer Objects
├── CameraUBO - 相机数据统一管理
├── ObjectUBO - 物体变换数据
├── MaterialUBO - 材质参数缓冲区
└── 动态UBO更新机制

📋 优先级7: 渲染优化
├── 实例化渲染支持
├── 渲染状态缓存
├── 绘制调用合并
└── GPU性能分析集成
```

---

### 🎯 **架构职责明确区分**

#### **Layer2 (RenderCore) 职责范围**:
- ✅ **相机系统**: 视图投影矩阵，视锥体计算
- ✅ **材质着色器**: 着色器管理，材质属性，渲染状态
- ✅ **几何网格**: 顶点数据，基础几何体，网格管理
- ✅ **纹理管理**: 纹理加载，缓存，资源生命周期
- ✅ **图元提交**: 渲染队列，批处理，渲染命令
- ✅ **视口管理**: 渲染目标，多视口支持

#### **Mid-Level Layer 职责范围** (未来开发):
- 🔮 **骨骼动画**: 动画播放，混合，状态机
- 🔮 **场景图**: 空间层次，裁剪优化，遮挡剔除  
- 🔮 **视觉特效**: HDR，粒子系统，后处理效果
- 🔮 **LOD系统**: 细节层次，动态切换
- 🔮 **性能工具**: 分析器，调试可视化

---

### 🎯 **近期实施计划 (基于Layer2架构)**

#### **本周目标 (第1周)**
- [x] ✅ 动态着色器编译系统 (Layer1)
- [x] ✅ 文件系统统一接口 (Layer1)
- [x] ✅ Visual Studio集成 (Layer1)
- [x] 🔄 **创建Layer2目录结构** (RenderCore)
- [x] 🔄 **Camera系统基础实现** (Layer2/Camera)

#### **下周目标 (第2周)**  
- [x] 📋 **完善相机控制器** (Layer2/Camera)
- [ ] 📋 **Mesh和几何体系统** (Layer2/Geometry)
- [ ] 📋 **MeshGenerator实现** (Layer2/Geometry)
- [ ] 📋 **基础Vertex结构体** (Layer2/Geometry)

#### **第3-4周目标**
- [ ] 📋 **Material系统实现** (Layer2/Material)
- [ ] 📋 **Shader高级管理** (Layer2/Material)
- [ ] 📋 **Texture2D系统** (Layer2/Texture)
- [ ] 📋 **TextureLoader实现** (Layer2/Texture)

#### **第5-6周目标**
- [ ] 📋 **Renderer核心系统** (Layer2/Rendering)
- [ ] 📋 **RenderQueue实现** (Layer2/Rendering)
- [ ] 📋 **UBO系统完善** (Layer2)
- [ ] 📋 **Viewport管理** (Layer2/Viewport)

---

### 🚨 **架构决策说明**

#### **为什么使用RenderCore而不是Render？**
- **避免命名冲突**: 与Mid-Level的高级渲染功能区分
- **明确职责范围**: 强调这是底层渲染基础设施
- **架构清晰性**: RenderCore → 基础，VisualEffects → 高级

#### **为什么着色器分层管理？**
- **Layer1**: VulkanShaderCompiler - 纯粹的GLSL→SPIR-V编译服务  
- **Layer2**: Shader + ShaderLibrary - 着色器资源管理和材质绑定
- **分离关注点**: 编译 vs 管理，平台服务 vs 渲染功能

#### **输入系统归属问题**
输入系统应该在**Layer4 (Human Interface Device Layer)**，不在Layer2：
```cpp
Layer4: 人机接口层
├── Physical Device I/O      # 设备输入处理
├── Character Controls       # 角色控制
├── Camera Control          # 相机控制输入 (调用Layer2 Camera)
└── Game-Specific Interface # 游戏特定接口
```

---

### 🎮 **里程碑目标**

#### **MVP版本** (6周后): 基础几何体渲染 ✨
- **相机系统**: 透视/正交投影，FPS控制器
- **几何体渲染**: 立方体、球体、平面等基础形状
- **材质系统**: 基础PBR材质，法线贴图
- **纹理系统**: PNG/JPG加载，Mipmap生成

#### **Alpha版本** (12周后): 完整PBR渲染管线 🚀
- **高级材质**: 金属度/粗糙度工作流
- **光照系统**: 方向光、点光源、聚光灯
- **阴影渲染**: 基础阴影映射
- **渲染优化**: 实例化渲染，批处理

#### **Beta版本** (18周后): Mid-Level系统集成 🎯
- **场景图**: 空间层次管理，裁剪优化
- **动画系统**: 骨骼动画，关键帧插值
- **视觉特效**: 粒子系统，后处理效果
- **编辑器**: 场景编辑，材质编辑

#### **Release版本** (24周后): 生产就绪 🏆
- **性能优化**: GPU-driven渲染，多线程
- **调试工具**: 性能分析器，内存监控
- **文档完善**: API文档，使用指南
- **测试覆盖**: 单元测试，集成测试

