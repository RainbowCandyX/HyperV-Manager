# HyperV-Manager

基于 Qt 6 和 [ElaWidgetTools](https://github.com/RainbowCandyX/ElaWidgetTools) 的 Hyper-V 虚拟机管理工具，提供现代化 Fluent Design 风格界面，替代 Windows 自带的 Hyper-V 管理器。

## 功能概览

### 仪表盘

- 实时统计虚拟机运行/关闭/暂停数量
- 资源概览与快速操作入口

### 虚拟机管理

- **虚拟机列表** — 表格展示所有虚拟机，支持启动、关闭、强制关机、暂停、恢复、重启、保存、删除、连接 (VMConnect)
- 按钮根据虚拟机状态自动显示/隐藏（运行中隐藏启动按钮，已关闭隐藏停止按钮等）
- 实时显示 CPU 使用率、内存占用、运行时间等信息

### 创建虚拟机

- 支持第 1 代 (BIOS + MBR) 和第 2 代 (UEFI + GPT) 虚拟机
- 虚拟机版本选择（降序排列，格式如 12.0、11.2）
- CPU、内存、虚拟硬盘、网络适配器配置
- 机密计算支持（GuestStateIsolationType：TrustedLaunch、VBS、SNP、TDX）
- GPU-P 分区显卡配置
- 虚拟机名称校验（空格、特殊字符、长度限制）

### 虚拟机设置

- **基本设置** — CPU 核心数、启动内存、动态内存、备注、自动启动/停止策略、检查点类型
- **处理器高级设置** — CPU 预留/上限、NUMA 配置
- **安全设置** — TPM、安全启动、状态加密
- **网络适配器**
  - QoS 带宽上限/下限
  - 硬件加速（VMQ、IPsec 任务卸载、SR-IOV）
  - 安全（MAC 地址欺骗、DHCP 防护、路由器防护）
  - 网络适配器增删与虚拟交换机切换

### GPU-P 设置

- 为已有虚拟机添加/移除 GPU 分区
- 选择宿主机 GPU 设备与资源分配比例

### 硬件管理

- **网络管理** — 虚拟交换机的查看与管理
- **PCIe 直通 (DDA)** — 将 PCIe 设备（显卡、网卡、USB 控制器等）直通分配给虚拟机，支持卸载、分配、移除、挂载
- **USB 设备** — 查看当前主机连接的 USB 设备信息

### 应用设置

- 亮色/暗色主题切换（持久保存）
- 导航栏显示模式
- 自动刷新开关与刷新间隔

## 技术栈

| 组件 | 说明 |
|------|------|
| Qt 6 Widgets | GUI 框架 |
| [ElaWidgetTools](https://github.com/RainbowCandyX/ElaWidgetTools) | Fluent Design 风格组件库（静态链接） |
| PowerShell | 通过 `QProcess` 调用 Hyper-V Cmdlet |
| CMake | 构建系统 |
| C++17 | 编程语言标准 |

## 环境要求

- Windows 10/11，已启用 **Hyper-V** 功能
- Qt 6.x（需包含 Widgets 模块）
- CMake 3.16+
- MSVC 编译器（推荐 Visual Studio 2019+）
- **管理员权限**（程序通过 UAC 清单要求提升权限以操作 Hyper-V）
- 若使用 GPU-P：宿主机显卡与驱动需支持 GPU 分区（`Get-VMHostPartitionableGpu` 可返回设备）
- 若使用 DDA：BIOS 中需启用 IOMMU（Intel VT-d / AMD-Vi）

## 构建

```bash
# 克隆仓库（含子模块）
git clone --recursive <repo-url>
cd HyperV-Manager

# 配置并构建
cmake -B build -DCMAKE_PREFIX_PATH="<Qt安装路径>"
cmake --build build --config Release
```

也可直接使用 Qt Creator 或 CLion 打开 `CMakeLists.txt` 进行构建。

## 项目结构

```
HyperV-Manager/
├── main.cpp                    # 程序入口，主题恢复
├── MainWindow.h/cpp            # 主窗口，导航页面注册
├── HomePage.h/cpp              # 仪表盘首页
├── VirtualMachinePage.h/cpp    # 虚拟机列表页
├── CreateVMPage.h/cpp          # 创建虚拟机页
├── GpuSettingsPage.h/cpp       # GPU-P 设置页
├── VMSettingsDialog.h/cpp      # 虚拟机详细设置对话框
├── NetworkPage.h/cpp           # 网络管理页
├── DDAPage.h/cpp               # PCIe 直通 (DDA) 页
├── USBPage.h/cpp               # USB 设备查看页
├── UsbTunnelManager.h/cpp      # USB 设备枚举
├── SettingPage.h/cpp           # 应用设置页
├── HyperVManager.h/cpp         # Hyper-V 操作封装层（PowerShell）
├── BasePage.h/cpp              # 页面基类
├── CMakeLists.txt              # 构建配置
└── 3rdparty/
    └── ElaWidgetTools/         # Fluent Design UI 组件库
```