#!/usr/bin/env python3
"""
Orange Engine 第三方库自动化安装脚本
自动拉取、编译和安装项目所需的第三方库
"""

import os
import sys
import json
import subprocess
import shutil
import argparse
from pathlib import Path


class DependencyManager:
    def __init__(self, project_root: Path, configuration: str = "Debug"):
        self.project_root = project_root
        self.configuration = configuration
        self.thirdparty_dir = project_root / "3rdparty"
        self.build_dir = project_root / "build" / configuration
        self.install_prefix = self.build_dir
        self.failed_dependencies = []  # 记录失败的依赖项
        
        # 确保目录存在
        self.thirdparty_dir.mkdir(exist_ok=True)
        self.build_dir.mkdir(parents=True, exist_ok=True)
        
    def load_dependencies(self) -> list:
        """加载第三方库配置"""
        config_file = self.project_root / "3rdparty.json"
        if not config_file.exists():
            raise FileNotFoundError(f"配置文件不存在: {config_file}")
            
        with open(config_file, 'r', encoding='utf-8') as f:
            config = json.load(f)
            
        return config.get('dependencies', [])
    
    def check_vulkan_sdk(self) -> bool:
        """检查Vulkan SDK是否已安装"""
        vulkan_sdk_path = os.environ.get('VULKAN_SDK')
        if vulkan_sdk_path and Path(vulkan_sdk_path).exists():
            print(f"✓ Vulkan SDK 已找到: {vulkan_sdk_path}")
            return True
        
        # 检查常见的Vulkan安装路径
        common_paths = [
            Path("C:/VulkanSDK"),
            Path(os.path.expanduser("~/VulkanSDK")),
        ]
        
        for path in common_paths:
            if path.exists():
                # 查找版本目录
                version_dirs = [d for d in path.iterdir() if d.is_dir()]
                if version_dirs:
                    latest_version = sorted(version_dirs)[-1]
                    print(f"✓ Vulkan SDK 已找到: {latest_version}")
                    return True
        
        print("✗ Vulkan SDK 未找到")
        return False
    
    def run_command(self, cmd: list, cwd: Path = None, check: bool = True) -> subprocess.CompletedProcess:
        """执行命令"""
        cwd = cwd or self.project_root
        print(f"执行命令: {' '.join(cmd)}")
        print(f"工作目录: {cwd}")
        
        try:
            result = subprocess.run(cmd, cwd=cwd, check=check, 
                                  capture_output=False, text=True)
            return result
        except subprocess.CalledProcessError as e:
            print(f"命令执行失败: {e}")
            if check:
                raise
            return e
    
    def clone_or_update_repo(self, name: str, url: str) -> Path:
        """克隆或更新Git仓库"""
        repo_path = self.thirdparty_dir / name
        
        if repo_path.exists():
            print(f"更新现有仓库: {name}")
            self.run_command(["git", "pull"], cwd=repo_path)
        else:
            print(f"克隆新仓库: {name}")
            self.run_command(["git", "clone", url, str(repo_path)])
        
        return repo_path
    
    def build_cmake_library(self, name: str, source_path: Path, 
                           shared_libs: bool = False, extra_args: list = None) -> tuple[bool, str]:
        """使用CMake编译库"""
        build_path = source_path / "build" / self.configuration
        build_path.mkdir(parents=True, exist_ok=True)
        
        # 清理CMake缓存
        cache_file = build_path / "CMakeCache.txt"
        if cache_file.exists():
            print(f"清理CMake缓存: {cache_file}")
            cache_file.unlink()
        
        # CMake配置参数
        cmake_args = [
            "cmake", "-G", "Visual Studio 17 2022", "-A", "x64",
            f"-DBUILD_SHARED_LIBS={'ON' if shared_libs else 'OFF'}",
            f"-DCMAKE_CONFIGURATION_TYPES={self.configuration}",
            f"-DCMAKE_BUILD_TYPE={self.configuration}",
            f"-DCMAKE_INSTALL_PREFIX={self.install_prefix}",
            f"-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY={self.install_prefix}/lib",
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={self.install_prefix}/lib",
            f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={self.install_prefix}/bin",
        ]
        
        # 为特定库添加自定义选项
        if name == "glm":
            cmake_args.extend([
                "-DGLM_BUILD_TESTS=OFF",
                "-DGLM_BUILD_INSTALL=ON",
                "-DGLM_ENABLE_CXX_17=ON"  # 启用C++17支持
            ])
        elif name == "spdlog":
            cmake_args.extend([
                "-DSPDLOG_BUILD_TESTS=OFF",
                "-DSPDLOG_BUILD_EXAMPLE=OFF",
                "-DSPDLOG_INSTALL=ON"
            ])
        elif name == "fmt":
            cmake_args.extend([
                "-DFMT_TEST=OFF",
                "-DFMT_DOC=OFF",
                "-DFMT_INSTALL=ON"
            ])
        elif name == "glfw":
            cmake_args.extend([
                "-DGLFW_BUILD_TESTS=OFF",
                "-DGLFW_BUILD_EXAMPLES=OFF",
                "-DGLFW_BUILD_DOCS=OFF",
                "-DGLFW_INSTALL=ON"
            ])
        elif name == "tracy":
            cmake_args.extend([
                "-DTRACY_ENABLE_CLIENT=OFF",
                "-DTRACY_ENABLE_TRACY_CLIENT=OFF",
                "-DTRACY_ENABLE_TRACY_CLIENT_PYTHON=OFF"
            ])
        elif name == "VulkanMemoryAllocator":
            cmake_args.extend([
                "-DVKMA_BUILD_TESTS=OFF",
                "-DVKMA_BUILD_EXAMPLES=OFF",
                "-DVKMA_BUILD_DOCS=OFF",
                "-DVKMA_INSTALL=ON"
            ])
        
        # 添加额外参数
        if extra_args:
            cmake_args.extend(extra_args)
            
        # 添加源码路径
        cmake_args.append(str(source_path))
        
        # 配置项目
        print(f"配置CMake项目: {name}")
        result = self.run_command(cmake_args, cwd=build_path, check=False)
        if result.returncode != 0:
            error_msg = f"CMake配置失败，返回码: {result.returncode}"
            print(f"✗ {error_msg}")
            return False, error_msg
        
        # 编译和安装
        print(f"编译和安装: {name}")
        build_cmd = [
            "cmake", "--build", ".", 
            "--config", self.configuration,
            "--target", "install"
        ]
        
        result = self.run_command(build_cmd, cwd=build_path, check=False)
        if result.returncode != 0:
            error_msg = f"编译安装失败，返回码: {result.returncode}"
            print(f"✗ {error_msg}")
            return False, error_msg
            
        print(f"✓ {name} 编译安装成功")
        return True, ""
    
    def install_header_only_library(self, name: str, source_path: Path, custom_dirs: list = None) -> tuple[bool, str]:
        """安装仅头文件库"""
        include_dir = self.install_prefix / "include" / name
        
        # 构建可能的头文件目录列表
        possible_include_dirs = []
        
        # 如果指定了自定义目录，优先使用
        if custom_dirs:
            for custom_dir in custom_dirs:
                possible_include_dirs.append(source_path / custom_dir)
        
        # 添加默认搜索路径
        possible_include_dirs.extend([
            source_path / "include",
            source_path / "src", 
            source_path,
        ])
        
        source_include_dir = None
        for dir_path in possible_include_dirs:
            if dir_path.exists():
                # 检查是否有头文件
                has_headers = any(dir_path.rglob("*.h")) or any(dir_path.rglob("*.hpp")) or any(dir_path.rglob("*.hxx"))
                if has_headers:
                    source_include_dir = dir_path
                    break
        
        if not source_include_dir:
            error_msg = f"未找到 {name} 的头文件目录。搜索路径: {[str(p) for p in possible_include_dirs]}"
            print(f"✗ {error_msg}")
            return False, error_msg
        
        try:
            # 复制头文件
            if include_dir.exists():
                shutil.rmtree(include_dir)
            
            print(f"复制头文件: {source_include_dir} -> {include_dir}")
            shutil.copytree(source_include_dir, include_dir)
            
            print(f"✓ {name} 头文件安装成功")
            return True, ""
        except Exception as e:
            error_msg = f"复制头文件失败: {e}"
            print(f"✗ {error_msg}")
            return False, error_msg
    
    def process_dependency(self, dep: dict) -> tuple[bool, str]:
        """处理单个依赖项"""
        name = dep.get('name')
        dep_type = dep.get('type')
        url = dep.get('url')
        include_dirs = dep.get('include_dirs', None)
        
        print(f"\n处理依赖项: {name} (类型: {dep_type})")
        
        # 跳过VulkanSDK如果已安装
        if name == "VulkanSDK":
            if self.check_vulkan_sdk():
                print(f"跳过 {name}，已安装")
                return True, ""
            else:
                error_msg = "需要手动安装Vulkan SDK"
                print(f"✗ {error_msg}: {url}")
                return False, error_msg
        
        # 克隆仓库
        try:
            source_path = self.clone_or_update_repo(name, url)
        except Exception as e:
            error_msg = f"克隆仓库失败: {e}"
            print(f"✗ {error_msg}")
            return False, error_msg
        
        # 根据类型处理
        try:
            if dep_type == "HeaderOnly":
                return self.install_header_only_library(name, source_path, include_dirs)
            elif dep_type == "library":
                return self.build_cmake_library(name, source_path)
            else:
                error_msg = f"未知的依赖类型: {dep_type}"
                print(f"✗ {error_msg}")
                return False, error_msg
        except Exception as e:
            error_msg = f"处理依赖项时发生异常: {e}"
            print(f"✗ {error_msg}")
            return False, error_msg
    
    def install_all(self) -> bool:
        """安装所有依赖项"""
        dependencies = self.load_dependencies()
        
        print(f"找到 {len(dependencies)} 个依赖项")
        print(f"配置: {self.configuration}")
        print(f"安装路径: {self.install_prefix}")
        
        success_count = 0
        total_count = len(dependencies)
        
        for dep in dependencies:
            success, error_msg = self.process_dependency(dep)
            if success:
                success_count += 1
            else:
                self.failed_dependencies.append({
                    'name': dep.get('name'),
                    'type': dep.get('type'),
                    'error': error_msg
                })
        
        # 输出最终结果
        print(f"\n{'='*60}")
        print(f"安装完成: {success_count}/{total_count} 成功")
        
        if success_count == total_count:
            print("✓ 所有依赖项安装成功！")
            return True
        else:
            print(f"✗ {len(self.failed_dependencies)} 个依赖项安装失败:")
            print()
            for i, failed in enumerate(self.failed_dependencies, 1):
                print(f"{i}. {failed['name']} ({failed['type']})")
                print(f"   失败原因: {failed['error']}")
                print()
            
            print("建议:")
            print("1. 检查网络连接和Git仓库访问权限")
            print("2. 确认CMake和Visual Studio正确安装")
            print("3. 查看上述错误信息进行针对性修复")
            print("4. 可以尝试单独安装失败的依赖项")
            
            return False


def main():
    parser = argparse.ArgumentParser(description="Orange Engine 第三方库安装脚本")
    parser.add_argument(
        "--config", 
        choices=["Debug", "Release"], 
        default="Debug",
        help="构建配置 (默认: Debug)"
    )
    parser.add_argument(
        "--clean", 
        action="store_true",
        help="清理现有的第三方库目录"
    )
    
    args = parser.parse_args()
    
    # 获取项目根目录
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    
    print("Orange Engine 第三方库安装脚本")
    print("=" * 50)
    print(f"项目根目录: {project_root}")
    print(f"构建配置: {args.config}")
    
    # 清理选项
    if args.clean:
        thirdparty_dir = project_root / "3rdparty"
        if thirdparty_dir.exists():
            print(f"清理第三方库目录: {thirdparty_dir}")
            shutil.rmtree(thirdparty_dir)
    
    # 创建依赖管理器并安装
    manager = DependencyManager(project_root, args.config)
    
    try:
        success = manager.install_all()
        sys.exit(0 if success else 1)
    except Exception as e:
        print(f"安装过程中出现错误: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main() 