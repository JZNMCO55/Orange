# 🎯 游戏引擎开发 Roadmap（个人项目）

## 🧱 阶段 1：引擎基础设施搭建（Week 1 ~ 4）
目标：构建运行主循环的最小引擎骨架 + 渲染管线初始化

- [ ] ⬜ 跨平台窗口与上下文创建（Platform Detection, Graphics Wrapper）
- [ ] ⬜ 高精度定时器与主循环（Hi-Res Timer）
- [ ] ⬜ Vulkan 初始化（Graphics API Support）
- [ ] ⬜ 日志系统与调试输出（Debug Printing and Logging）
- [ ] ⬜ 图形设备接口与清屏测试（Graphics Device Interface）
- [ ] ⬜ 输入系统初步支持（Keyboard/Mouse 捕获）
- [ ] ⬜ 引擎入口函数封装（模块启动与关闭）

🎯 验收目标：  
运行时打开窗口并渲染背景色，可打印日志

---

## 📦 阶段 2：资源加载与基础渲染（Week 5 ~ 8）
目标：实现资源加载、摄像机与模型显示流程

- [ ] ⬜ ResourceManager 资源管理器（支持纹理/模型）
- [ ] ⬜ 实现 Mesh & Texture Resource 支持
- [ ] ⬜ 材质与着色器系统（支持 .frag/.vert 加载）
- [ ] ⬜ 摄像机系统（视图/投影矩阵、FPS控制）
- [ ] ⬜ 基础光照支持（Phong 或 Lambert）
- [ ] ⬜ 支持一个场景中绘制多个模型（Scene Render Queue）

🎯 验收目标：  
加载模型文件并正确渲染，摄像机可绕场景移动

---

## 🧠 阶段 3：对象系统与组件模型（Week 9 ~ 12）
目标：实现基本 GameObject / Component 架构

- [ ] ⬜ 实现 Entity + Component 框架
- [ ] ⬜ 支持 Transform、MeshRenderer、CameraComponent
- [ ] ⬜ 实现对象唯一 ID（Object Handle System）
- [ ] ⬜ 场景图结构构建（Scene Graph）
- [ ] ⬜ 事件系统初步实现（Event / Messaging System）

🎯 验收目标：  
可动态创建多个对象，添加组件并通过事件控制行为

---

## 🧪 阶段 4：编辑器与调试工具（Week 13 ~ 17）
目标：构建一个基础 ImGui 编辑器，支持场景浏览与属性查看

- [ ] ⬜ 集成 ImGui（ImGuiLayer）
- [ ] ⬜ 实现 Scene Hierarchy 面板
- [ ] ⬜ 实现 Inspector 面板（可显示组件）
- [ ] ⬜ 实现控制台面板（显示日志输出）
- [ ] ⬜ 支持鼠标点击选中对象
- [ ] ⬜ 将渲染结果嵌入 ImGui Viewport

🎯 验收目标：  
编辑器可显示场景对象结构，选中对象并查看组件属性

---

## 🎮 阶段 5：逻辑控制与基础交互（Week 18 ~ 22）
目标：加入角色控制、相机跟随、脚本绑定系统

- [ ] ⬜ 玩家控制器（键盘 WASD 控制角色移动）
- [ ] ⬜ 游戏相机（跟随模式）
- [ ] ⬜ 引入 Lua 脚本绑定（支持绑定组件事件）
- [ ] ⬜ 简单 FSM 状态机系统（Idle/Walk 状态切换）
- [ ] ⬜ 物理碰撞基础集成（Bounding Box + AABB 碰撞）

🎯 验收目标：  
玩家可控制角色移动，相机跟随，场景中发生碰撞检测

---

## 🧩 阶段 6：渲染与资源系统扩展（Week 23 ~ 30）
目标：提升渲染表现力，优化资源加载流程

- [ ] ⬜ 实现 LOD 系统（根据距离切换模型）
- [ ] ⬜ 实现粒子系统（ImGui 中实时调整参数）
- [ ] ⬜ 材质系统扩展（支持多 Pass / Shader Uniform 面板）
- [ ] ⬜ 异步资源加载机制（Texture/Model 加载不阻塞主线程）
- [ ] ⬜ 骨骼动画系统（导入带骨架模型并播放）

🎯 验收目标：  
渲染支持动态粒子/动画，资源支持热加载

---

## 🧱 阶段 7：功能完善与游戏 Demo 制作（Week 31 ~ ...）
目标：使用已有功能构建简单 Demo，测试引擎稳定性

- [ ] ⬜ 设计 Demo 地图（场景数据加载）
- [ ] ⬜ 实现基础 UI（ImGui 面板 / 游戏内状态）
- [ ] ⬜ 添加一个简单敌人 AI（巡逻 + 玩家感知）
- [ ] ⬜ 支持场景切换与重新加载（World Loading）
- [ ] ⬜ 打包一份 Demo 可执行程序

🎯 验收目标：  
运行一个可玩的 Demo，包含角色、AI、交互、UI 等基本要素

---

## ✅ 附录：任务状态说明

- [x] 已完成
- [ ] 待实现
- [~] 进行中
- [!] 阻塞/依赖未满足

