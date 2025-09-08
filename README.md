# DPDK Hands-on 项目

本项目包含三个 DPDK 示例程序，展示了 DPDK 的核心功能和用法。所有程序都已从 Makefile 构建系统转换为 CMake 构建系统。

## 项目结构

```
dpdk-hands-on/
├── build/                    # CMake 构建目录
├── bin/                      # 可执行文件输出目录
│   ├── helloworld           # Hello World 示例
│   ├── hash_usage           # 哈希表使用示例
│   └── capture_packet       # 数据包捕获程序
├── 1-helloworld/              # Hello World 源代码
├── 2-hash_usage/              # 哈希表使用源代码
├── 3-capture_packet/        # 数据包捕获源代码
├── CMakeLists.txt           # 根 CMake 配置
└── .gitignore              # Git 忽略文件
```

## 示例程序介绍

### 1. Hello World (`helloworld/`)

**功能**: DPDK 基础入门示例

**主要特性**:
- 演示 DPDK EAL (Environment Abstraction Layer) 初始化
- 展示多核处理的基本概念
- 简单的 lcore 函数执行

**学习要点**:
- DPDK 环境初始化流程
- 多核编程基础
- EAL 参数处理

**运行示例**:
```bash
sudo ./bin/helloworld -l 0-1 -n 4
```

### 2. Hash Usage (`hash_usage/`)

**功能**: DPDK 哈希表使用示例

**主要特性**:
- 演示 DPDK 哈希表的创建和使用
- 展示 5-tuple 流键的处理
- 包含数据包结构体打包优化

**学习要点**:
- DPDK 哈希表 API 使用
- 网络流识别和处理
- 内存对齐和结构体优化
- 数据包解析基础

**运行示例**:
```bash
sudo ./bin/hash_usage -l 0-1 -n 4
```

### 3. Capture Packet (`3-capture_packet/`)

**功能**: 网络数据包捕获程序

**主要特性**:
- 实时网络数据包捕获
- 支持多端口同时监听
- 数据包统计和分析
- 支持混杂模式
- IPv4 数据包解析

**学习要点**:
- 网络接口初始化和配置
- 数据包接收和处理
- 内存池管理
- 网络协议解析
- 信号处理和优雅退出

**运行示例**:
```bash
# 基本运行
sudo ./bin/capture_packet -l 0-1 -n 4

# 指定网卡运行
sudo ./bin/capture_packet -l 0-1 -n 4 -- -p 0x1
```

## 构建说明

### 环境要求

- DPDK 开发库 (推荐版本 24.11.2)
- pkg-config
- CMake 3.10+
- GCC 编译器
- Linux 系统

### 构建步骤

```bash
# 1. 创建构建目录
mkdir build
cd build

# 2. 配置 CMake
cmake ..

# 3. 编译
make

# 4. 可执行文件将生成在 ../bin/ 目录中
```

### 构建选项

- **Debug 模式**: `cmake -DCMAKE_BUILD_TYPE=Debug ..`
- **Release 模式**: `cmake -DCMAKE_BUILD_TYPE=Release ..` (默认)

## 运行说明

### 通用 EAL 参数

所有程序都支持标准的 DPDK EAL 参数：

- `-l <core_list>`: 指定使用的 CPU 核心列表
- `-n <num_memory_channels>`: 内存通道数量
- `-m <memory_size>`: 内存大小 (MB)
- `--huge-dir <path>`: 大页内存目录
- `-w <pci_address>`: 绑定网卡 PCI 地址

### 示例运行命令

```bash
# Hello World - 使用核心 0-1，4个内存通道
sudo ./bin/helloworld -l 0-1 -n 4

# Hash Usage - 使用核心 0-3，8个内存通道
sudo ./bin/hash_usage -l 0-3 -n 8

# Capture Packet - 使用核心 0-1，绑定网卡
sudo ./bin/capture_packet -l 0-1 -n 4 -w 0000:01:00.0
```

## 技术特性

### CMake 构建系统

- 自动检测 DPDK 依赖
- 支持 pkg-config 配置
- 统一的编译选项管理
- 模块化的项目结构

### DPDK 功能覆盖

- **内存管理**: 大页内存、内存池
- **网络接口**: 网卡初始化、配置
- **数据包处理**: 接收、解析、转发
- **多核编程**: lcore 管理、负载均衡
- **哈希表**: 高性能查找和存储

## 学习路径建议

1. **入门**: 从 `helloworld` 开始，了解 DPDK 基础概念
2. **进阶**: 学习 `hash_usage`，掌握数据结构和优化技巧
3. **实战**: 使用 `capture_packet`，体验真实的网络编程

## 故障排除

### 常见问题

1. **权限问题**: 确保以 root 权限运行
2. **大页内存**: 检查 `/proc/meminfo` 中的 HugePages 配置
3. **网卡绑定**: 使用 `dpdk-devbind.py` 工具绑定网卡
4. **依赖缺失**: 确保 DPDK 开发包正确安装