import os
import subprocess
import sys
from pathlib import Path
from io import BytesIO
from urllib.request import urlopen
from zipfile import ZipFile

import Utils

# 获取当前脚本所在目录的上一级目录
base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
# 定义新的 Vulkan 存放目录：3rdParty/Vulkan
vulkan_dir = os.path.join(base_dir, '3rdParty', 'Vulkan')

# 如果目录不存在，则创建目录
if not os.path.exists(vulkan_dir):
    os.makedirs(vulkan_dir)
    print(f"Created directory: {vulkan_dir}")
else:
    print(f"Using existing directory: {vulkan_dir}")

# 配置相关路径
VULKAN_SDK = os.environ.get('VULKAN_SDK')
VULKAN_SDK_INSTALLER_URL = 'https://sdk.lunarg.com/sdk/download/1.4.304.0/windows/vulkan_sdk.exe'
Orange_VULKAN_VERSION = '1.4.304.0'
# 将 Vulkan SDK 安装包放在 3rdParty/Vulkan 目录下
VULKAN_SDK_EXE_PATH = os.path.join(vulkan_dir, "VulkanSDK.exe")

# Debug 库路径设置
OutputDirectory = vulkan_dir
TempZipFile = os.path.join(OutputDirectory, "VulkanSDK.zip")

def InstallVulkanSDK():
    print('Downloading {} to {}'.format(VULKAN_SDK_INSTALLER_URL, VULKAN_SDK_EXE_PATH))
    Utils.DownloadFile(VULKAN_SDK_INSTALLER_URL, VULKAN_SDK_EXE_PATH)
    print("Done!")
    print("Running Vulkan SDK installer...")
    os.startfile(os.path.abspath(VULKAN_SDK_EXE_PATH))
    print("Re-run this script after installation")

def InstallVulkanPrompt():
    print("Would you like to install the Vulkan SDK?")
    install = Utils.YesOrNo()
    if install:
        InstallVulkanSDK()
        quit()

def CheckVulkanSDK():
    if VULKAN_SDK is None:
        print("You don't have the Vulkan SDK installed!")
        InstallVulkanPrompt()
        return False
    elif Orange_VULKAN_VERSION not in VULKAN_SDK:
        print(f"Located Vulkan SDK at {VULKAN_SDK}")
        print(f"You don't have the correct Vulkan SDK version! (Orange requires {Orange_VULKAN_VERSION})")
        InstallVulkanPrompt()
        return False
    
    print(f"Correct Vulkan SDK located at {VULKAN_SDK}")
    return True

VulkanSDKDebugLibsURL = 'https://files.lunarg.com/SDK-1.4.304.0/VulkanSDK-1.4.304.0-DebugLibs.zip'

def CheckVulkanSDKDebugLibs():
    shadercdLib = Path(os.path.join(OutputDirectory, "Lib", "shaderc_sharedd.lib"))
    if not shadercdLib.exists():
        print(f"No Vulkan SDK debug libs found. (Checked {shadercdLib})")
        print("Downloading", VulkanSDKDebugLibsURL)
        with urlopen(VulkanSDKDebugLibsURL) as zipresp:
            with ZipFile(BytesIO(zipresp.read())) as zfile:
                zfile.extractall(OutputDirectory)
    print(f"Vulkan SDK debug libs located at {OutputDirectory}")
    return True
