# Orange Engine 构建脚本说明

本目录包含Orange引擎的构建和依赖管理脚本。

## 脚本列表

### 1. setup_dependencies.py
**第三方库自动化安装脚本**

自动拉取、编译和安装项目所需的第三方库。

#### 功能特性
- 自动读取 `3rdparty.json` 配置文件
- 支持Git仓库克隆和更新
- 自动检测Vulkan SDK安装状态
- 支持CMake库和仅头文件库的安装
- 支持自定义头文件目录配置
- 输出路径与主项目构建目录保持一致
- 支持Debug/Release配置
- 详细的错误报告和失败原因分析

#### 使用方法

```bash
# 安装Debug版本依赖库 (默认)
python setup_dependencies.py

# 安装Release版本依赖库
python setup_dependencies.py --config Release

# 清理并重新安装
python setup_dependencies.py --config Debug --clean

# 显示帮助信息
python setup_dependencies.py --help
```

#### 依赖处理规则

1. **VulkanSDK**: 检查环境变量和常见安装路径，如已存在则跳过
2. **HeaderOnly类型**: 
   - 支持自定义头文件目录 (`include_dirs` 配置)
   - 自动搜索 `include/`, `src/`, 根目录等常见位置
   - 复制头文件到 `{build_dir}/include/{library_name}/`
3. **library类型**: 使用CMake编译并安装到 `{build_dir}/`

#### 输出目录结构
```
build/{Configuration}/
├── bin/                # 运行时库和可执行文件
├── lib/                # 静态库和导入库
└── include/            # 头文件
    ├── spdlog/
    ├── tracy/
    ├── glm/
    └── fmt/
```

### 2. generate.bat
**主项目构建脚本**

编译Orange引擎主项目。

#### 使用方法
```bat
# 编译Release版本 (默认)
generate.bat

# 编译Debug版本
generate.bat Debug

# 编译为共享库
generate.bat Release shared
```

## 完整构建流程

### 首次构建
1. 确保已安装必要工具：
   - Python 3.6+
   - Git
   - CMake 3.15+
   - Visual Studio 2022
   
2. 安装第三方库：
   ```bash
   python script/setup_dependencies.py --config Debug
   ```

3. 编译主项目：
   ```bat
   generate.bat Debug
   ```

### 日常开发
```bash
# 更新依赖库 (如果有变化)
python script/setup_dependencies.py --config Debug

# 编译项目
generate.bat Debug
```

### 发布构建
```bash
# 安装Release版本依赖
python script/setup_dependencies.py --config Release

# 编译Release版本
generate.bat Release
```

## 故障排除

### 常见问题

1. **Python未找到**
   - 确保Python已安装并添加到PATH环境变量
   - 支持Python 3.6及以上版本

2. **Git克隆失败**
   - 检查网络连接
   - 确保有访问Git仓库的权限
   - 可能需要配置SSH密钥

3. **CMake配置失败**
   - 确保CMake版本为3.15或更高
   - 检查Visual Studio 2022是否正确安装
   - 查看具体错误信息

4. **Vulkan SDK问题**
   - 从 https://vulkan.lunarg.com/sdk/home 下载安装
   - 确保设置了VULKAN_SDK环境变量

5. **编译错误**
   - 检查依赖库是否正确安装
   - 确认构建配置匹配 (Debug/Release)
   - 清理构建目录重试

### 清理和重建
```bash
# 清理第三方库并重新安装
python script/setup_dependencies.py --config Debug --clean

# 清理主项目构建 (手动删除build目录)
rmdir /s build

# 重新构建
generate.bat Debug
```

## 配置文件说明

### 3rdparty.json
```json
{
    "dependencies": [
        {
            "name": "库名称",
            "type": "library|HeaderOnly|SDK",
            "url": "Git仓库URL或下载链接",
            "include_dirs": ["可选的头文件目录列表"]
        }
    ]
}
```

- **name**: 库的名称，用作目录名
- **type**: 
  - `library`: 需要CMake编译的库（如spdlog、fmt、glfw、glm）
    - 支持标准CMake安装流程
    - 自动为常见库添加优化的编译选项（禁用测试、示例等）
  - `HeaderOnly`: 仅头文件库（如tracy）
    - 直接复制头文件到安装目录
    - 支持自定义头文件搜索路径
  - `SDK`: 需要手动安装的SDK（如VulkanSDK）
- **url**: Git仓库地址或下载链接
- **include_dirs**: (可选) 对于HeaderOnly类型，指定头文件搜索目录

## 扩展和自定义

### 添加新的第三方库
1. 在 `3rdparty.json` 中添加依赖项配置
2. 对于HeaderOnly类型，可以指定 `include_dirs` 来定制头文件搜索路径
3. 运行 `python script/setup_dependencies.py` 自动处理

### 修改安装路径
修改 `setup_dependencies.py` 中的 `install_prefix` 设置

### 添加特殊构建参数
在 `build_cmake_library` 方法中添加特定库的CMake参数 