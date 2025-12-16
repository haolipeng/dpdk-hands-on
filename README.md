# DPDK Hands-on 项目

本项目是一个面向 DPDK 初学者的完整教程和实战项目，包含 6 个循序渐进的示例程序，展示了 DPDK 的核心功能和用法。每个示例都配有详细的中文教程文档。所有程序都使用 CMake 构建系统。

## 项目结构

```
dpdk-hands-on/
├── build/                        # CMake 构建目录
├── bin/                          # 可执行文件输出目录
│   ├── helloworld               # Lesson 1: Hello World
│   ├── hash_usage               # Lesson 2: 哈希表使用
│   ├── capture_packet           # Lesson 3: 数据包捕获
│   ├── parse_packet             # Lesson 4: 数据包解析
│   ├── mempool_usage            # Lesson 5: 内存池使用
│   └── flow_manager             # Lesson 6: 流管理器
├── 1-helloworld/                 # Lesson 1 源代码
├── 2-hash_usage/                 # Lesson 2 源代码
├── 3-capture_packet/             # Lesson 3 源代码
├── 4-parse_packet/               # Lesson 4 源代码
├── 5-mempool_usage/              # Lesson 5 源代码
├── 6-flow_manager/               # Lesson 6 源代码
├── lesson1-helloworld.md         # Lesson 1 教程文档
├── lesson2-hash.md               # Lesson 2 教程文档
├── lesson3-capture-packet.md     # Lesson 3 教程文档
├── lesson4-parse-packet.md       # Lesson 4 教程文档
├── lesson5-mempool.md            # Lesson 5 教程文档
├── lesson6-flowmanager.md        # Lesson 6 教程文档
├── lesson7-multiprocess.md       # Lesson 7 教程文档（理论）
├── picture/                      # 教程配图目录
├── CMakeLists.txt                # 根 CMake 配置
└── README.md                     # 本文档
```

## 教程和示例程序

### Lesson 1: Hello World (`1-helloworld/`)
📖 **教程**: [lesson1-helloworld.md](lesson1-helloworld.md)

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
sudo ./bin/helloworld -l 0-1
```

---

### Lesson 2: Hash Table Usage (`2-hash_usage/`)
📖 **教程**: [lesson2-hash.md](lesson2-hash.md)

**功能**: DPDK 哈希表使用示例

**主要特性**:
- 演示 DPDK 哈希表的创建和使用
- 展示 5-tuple 流键的处理
- 包含数据包结构体打包优化

**学习要点**:
- DPDK 哈希表 API 使用
- 网络流识别和处理
- 内存对齐和结构体优化
- 哈希函数选择（CRC vs JHash）

**运行示例**:
```bash
sudo ./bin/hash_usage -l 0
```

---

### Lesson 3: Capture Packet (`3-capture_packet/`)
📖 **教程**: [lesson3-capture-packet.md](lesson3-capture-packet.md)

**功能**: 网络数据包捕获程序

**主要特性**:
- 实时网络数据包捕获
- 支持多端口同时监听
- 数据包统计和分析
- 支持混杂模式
- 基础 IPv4 数据包解析

**学习要点**:
- 网络接口初始化和配置
- 数据包接收和处理
- 内存池（mbuf pool）管理
- 网络协议基础解析
- 信号处理和优雅退出

**运行示例**:
```bash
# 基本运行
sudo ./bin/capture_packet -l 0

# 查看详细日志
sudo ./bin/capture_packet -l 0 --log-level=8
```

---

### Lesson 4: Parse Packet (`4-parse_packet/`)
📖 **教程**: [lesson4-parse-packet.md](lesson4-parse-packet.md)

**功能**: 深度数据包解析程序

**主要特性**:
- 完整的以太网、IPv4、TCP/UDP 协议解析
- 详细的协议字段提取和显示
- 大小端转换处理
- 协议类型识别

**学习要点**:
- 网络协议栈深入理解
- 数据包头部结构解析
- 字节序转换（网络序 vs 主机序）
- 多层协议解析
- DPDK mbuf 数据访问方法

**运行示例**:
```bash
sudo ./bin/parse_packet -l 0
```

---

### Lesson 5: Mempool Usage (`5-mempool_usage/`)
📖 **教程**: [lesson5-mempool.md](lesson5-mempool.md)

**功能**: DPDK 内存池使用示例

**主要特性**:
- 演示 DPDK 内存池的创建和使用
- 自定义对象内存池
- 内存对象分配和释放
- 内存池统计信息查询
- NUMA 感知的内存分配

**学习要点**:
- DPDK 内存池（mempool）原理
- 对象缓存机制
- 内存池配置参数
- 内存池性能优化
- 批量分配和释放
- 内存泄漏检测

**运行示例**:
```bash
sudo ./bin/mempool_usage -l 0
```

---

### Lesson 6: Flow Manager (`6-flow_manager/`)
📖 **教程**: [lesson6-flowmanager.md](lesson6-flowmanager.md)

**功能**: TCP 流管理器

**主要特性**:
- 基于哈希表的流管理
- TCP 会话跟踪
- 五元组（5-tuple）流识别
- 流统计（数据包数、字节数）
- 双向流聚合

**学习要点**:
- 网络流的概念和管理
- DPDK Hash 表高级应用
- 流键规范化技术
- 会话状态跟踪
- 流表遍历和统计
- 实际网络应用场景

**运行示例**:
```bash
sudo ./bin/flow_manager -l 0
```

---

### Lesson 7: Multi-Process Architecture
📖 **教程**: [lesson7-multiprocess.md](lesson7-multiprocess.md)

**说明**: 多进程架构理论教程（暂无代码示例）

**学习要点**:
- DPDK 多进程模型
- 主进程和从进程通信
- 共享内存管理
- 进程间数据交换

---

### Lesson 14: ACL (Access Control List) 三部曲

#### Lesson 14-1: ACL 入门与核心概念
📖 **教程**: [lesson14-1-acl-basics.md](lesson14-1-acl-basics.md)

**功能**: ACL 基础概念和数据结构

**主要特性**:
- ACL 原理和优势介绍
- 五元组匹配机制
- 三种匹配类型（MASK/RANGE/BITMASK）
- 优先级机制讲解
- 核心数据结构详解

**学习要点**:
- 理解 ACL 的应用场景
- 掌握五元组匹配原理
- 熟悉 ACL 数据结构设计
- 了解优先级和通配符使用

---

#### Lesson 14-2: ACL 实战应用
📖 **教程**: [lesson14-2-acl-practice.md](lesson14-2-acl-practice.md)

**功能**: 完整的 ACL 防火墙演示程序

**主要特性**:
- ACL 上下文创建和配置
- 规则添加和构建
- 数据包批量分类
- 完整的防火墙示例（5条规则）
- 字段定义和规则构造

**学习要点**:
- 掌握 ACL API 详细使用
- 能够编写 ACL 防火墙程序
- 理解规则配置和数据包分类
- 掌握编译、运行、调试流程

**运行示例**:
```bash
sudo ./bin/acl_demo -l 0 --no-pci
```

---

#### Lesson 14-3: ACL 性能优化与进阶
📖 **教程**: [lesson14-3-acl-advanced.md](lesson14-3-acl-advanced.md)

**功能**: ACL 性能优化和高级应用

**主要特性**:
- 批量分类性能优化（15x 提升）
- 规则排序优化技巧
- NUMA 感知配置
- 多分类器应用（入站/出站）
- 通配符和范围高级用法
- 动态规则更新

**学习要点**:
- 掌握 ACL 性能优化技巧
- 学习多分类器应用场景
- 解决常见问题
- 为生产环境部署做准备

**应用场景**:
- 高性能防火墙（10-100x 性能提升）
- QoS 流量分类
- DDoS 防护
- 流量统计和审计

---

## 快速开始

### 环境要求

- **操作系统**: Linux (推荐 Ubuntu 20.04+)
- **DPDK**: 版本 24.11.2 或更高
- **编译工具**:
  - GCC 7.0+
  - CMake 3.10+
  - pkg-config
- **硬件**:
  - 至少 2GB RAM
  - 支持大页内存的 CPU
  - （可选）网卡用于实际测试

### 安装 DPDK

```bash
# Ubuntu/Debian
sudo apt-get install dpdk dpdk-dev

# 或从源码编译（参考 DPDK 官方文档）
wget https://fast.dpdk.org/rel/dpdk-24.11.2.tar.xz
tar xf dpdk-24.11.2.tar.xz
cd dpdk-24.11.2
meson build
cd build
ninja
sudo ninja install
```

### 配置大页内存

```bash
# 临时配置（重启后失效）
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# 永久配置（编辑 /etc/sysctl.conf）
sudo bash -c "echo 'vm.nr_hugepages=1024' >> /etc/sysctl.conf"
sudo sysctl -p

# 挂载大页内存
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge

# 验证配置
grep Huge /proc/meminfo
```

### 构建项目

```bash
# 1. 克隆项目（如果还没有）
git clone <repository-url>
cd dpdk-hands-on

# 2. 创建构建目录
mkdir -p build
cd build

# 3. 配置 CMake
cmake ..

# 4. 编译所有示例
make

# 5. 可执行文件在 ../bin/ 目录中
ls -lh ../bin/
```

### 构建选项

```bash
# Debug 模式（带调试符号）
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release 模式（优化性能，默认）
cmake -DCMAKE_BUILD_TYPE=Release ..

# 编译单个示例
make helloworld
make hash_usage
make capture_packet
```

### 运行第一个示例

```bash
# 进入 bin 目录
cd ../bin

# 运行 Hello World（最简单的示例）
sudo ./helloworld -l 0

# 应该看到类似输出：
# EAL: Detected 4 lcore(s)
# hello from core 0
```

## 学习路径

### 推荐学习顺序

按照以下顺序学习，可以循序渐进地掌握 DPDK：

1. **Lesson 1: Hello World** - 了解 DPDK 基础和 EAL 初始化
2. **Lesson 2: Hash Table** - 学习 DPDK 核心数据结构
3. **Lesson 3: Capture Packet** - 掌握网卡初始化和数据包接收
4. **Lesson 4: Parse Packet** - 深入理解网络协议解析
5. **Lesson 5: Mempool** - 掌握内存管理和性能优化
6. **Lesson 6: Flow Manager** - 构建实际的网络应用
7. **Lesson 7: Multi-Process** - 理解高级架构模式

### 学习建议

- **阅读教程**: 每个 Lesson 都有详细的 markdown 教程，先阅读理解再运行代码
- **动手实践**: 运行每个示例，观察输出，尝试修改参数
- **对比代码**: 对比不同 Lesson 之间的代码差异，理解演进过程
- **查阅文档**: 遇到不理解的 API，查阅 DPDK 官方文档
- **逐步调试**: 使用 `--log-level=8` 参数查看详细日志

## 常用 EAL 参数

所有程序都支持标准的 DPDK EAL 参数：

| 参数 | 说明 | 示例 |
|------|------|------|
| `-l <core_list>` | 指定使用的 CPU 核心 | `-l 0` 或 `-l 0-3` |
| `-n <channels>` | 内存通道数量 | `-n 4` |
| `-m <memory_MB>` | 内存大小（MB） | `-m 512` |
| `--log-level=<level>` | 日志级别（0-8） | `--log-level=8` |
| `-w <pci_addr>` | 白名单网卡 PCI 地址 | `-w 0000:01:00.0` |
| `--huge-dir <path>` | 大页内存目录 | `--huge-dir /mnt/huge` |
| `--no-pci` | 不使用 PCI 设备 | `--no-pci` |

### 运行示例命令

```bash
# 1. Hello World（最简单，不需要网卡）
sudo ./bin/helloworld -l 0

# 2. Hash Table（演示哈希表，不需要网卡）
sudo ./bin/hash_usage -l 0

# 3. Capture Packet（需要网卡）
sudo ./bin/capture_packet -l 0

# 4. Parse Packet（需要网卡，详细解析）
sudo ./bin/parse_packet -l 0 --log-level=8

# 5. Mempool（演示内存池，不需要网卡）
sudo ./bin/mempool_usage -l 0

# 6. Flow Manager（需要网卡，流跟踪）
sudo ./bin/flow_manager -l 0
```

## 项目特色

### 📚 详细的中文教程

- 每个示例都配有完整的中文教程文档
- 从基础到进阶，循序渐进
- 包含原理讲解、代码分析、运行示例
- 适合 DPDK 零基础学习者

### 🛠️ 现代化构建系统

- 使用 CMake 替代传统 Makefile
- 自动检测 DPDK 依赖（pkg-config）
- 统一的编译选项管理
- 支持 Debug/Release 模式切换
- 模块化的项目结构，易于扩展

### 💡 循序渐进的示例

| Lesson | 复杂度 | 需要网卡 | 主要内容 |
|--------|--------|----------|----------|
| 1 | ⭐ | ❌ | EAL 初始化、多核基础 |
| 2 | ⭐⭐ | ❌ | 哈希表、数据结构 |
| 3 | ⭐⭐⭐ | ✅ | 网卡初始化、数据包接收 |
| 4 | ⭐⭐⭐ | ✅ | 协议解析、字节序转换 |
| 5 | ⭐⭐ | ❌ | 内存池、性能优化 |
| 6 | ⭐⭐⭐⭐ | ✅ | 流管理、会话跟踪 |
| 7 | ⭐⭐⭐⭐ | - | 多进程架构（理论） |

### 🎯 覆盖核心功能

- ✅ **EAL 初始化**: DPDK 环境抽象层
- ✅ **内存管理**: 大页内存、内存池（mempool）
- ✅ **网络接口**: 网卡初始化、端口配置
- ✅ **数据包处理**: mbuf、接收、解析
- ✅ **数据结构**: 哈希表（hash）、流管理
- ✅ **协议解析**: Ethernet、IPv4、TCP/UDP
- ✅ **多核编程**: lcore 管理
- ✅ **信号处理**: 优雅退出

## 故障排除

### 常见问题

#### 1. 编译错误：找不到 DPDK

**错误信息**:
```
CMake Error: Could not find DPDK
```

**解决方案**:
```bash
# 检查 DPDK 是否安装
pkg-config --exists libdpdk && echo "DPDK installed" || echo "DPDK not found"

# 安装 DPDK 开发包
sudo apt-get install dpdk dpdk-dev

# 或设置 PKG_CONFIG_PATH
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

#### 2. 运行错误：权限不足

**错误信息**:
```
EAL: Cannot create lock on '/var/run/dpdk/rte/config'
```

**解决方案**:
```bash
# 必须使用 root 权限运行
sudo ./bin/helloworld -l 0

# 或者配置权限
sudo chmod 777 /var/run/dpdk
```

#### 3. 运行错误：大页内存不足

**错误信息**:
```
EAL: Cannot get hugepage information
EAL: Not enough memory available on socket
```

**解决方案**:
```bash
# 检查大页内存配置
grep Huge /proc/meminfo

# 配置大页内存
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# 挂载大页文件系统
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge
```

#### 4. 运行错误：找不到网卡

**错误信息**:
```
EAL: No available Ethernet device
```

**解决方案**:

对于不需要网卡的示例（Lesson 1, 2, 5）:
```bash
# 使用 --no-pci 参数
sudo ./bin/helloworld -l 0 --no-pci
```

对于需要网卡的示例（Lesson 3, 4, 6）:
```bash
# 1. 查看网卡信息
dpdk-devbind.py --status

# 2. 绑定网卡到 DPDK（示例）
sudo modprobe uio_pci_generic
sudo dpdk-devbind.py --bind=uio_pci_generic 0000:01:00.0

# 3. 运行程序
sudo ./bin/capture_packet -l 0
```

#### 5. 程序崩溃或段错误

**可能原因**:
- 内存不足
- 大页内存配置错误
- 网卡驱动不兼容

**解决方案**:
```bash
# 查看详细日志
sudo ./bin/capture_packet -l 0 --log-level=8

# 使用 Debug 模式编译
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# 使用 gdb 调试
sudo gdb ./bin/capture_packet
(gdb) run -l 0
```

### 网卡绑定详细说明

#### 查看可用网卡

```bash
# 查看所有网卡状态
dpdk-devbind.py --status

# 输出示例：
# Network devices using kernel driver
# ===================================
# 0000:01:00.0 'Ethernet Controller' if=eth0 drv=e1000 unused=uio_pci_generic
```

#### 绑定网卡到 DPDK

```bash
# 1. 加载 UIO 驱动
sudo modprobe uio_pci_generic

# 2. 关闭网卡（如果正在使用）
sudo ifconfig eth0 down

# 3. 绑定网卡
sudo dpdk-devbind.py --bind=uio_pci_generic 0000:01:00.0

# 4. 验证绑定
dpdk-devbind.py --status
```

#### 解绑网卡（恢复系统使用）

```bash
# 解绑网卡
sudo dpdk-devbind.py --bind=e1000 0000:01:00.0

# 启动网卡
sudo ifconfig eth0 up
```

## 参考资源

### 官方文档
- [DPDK 官方网站](https://www.dpdk.org/)
- [DPDK 文档](https://doc.dpdk.org/)
- [DPDK API 参考](https://doc.dpdk.org/api/)
- [DPDK 编程指南](https://doc.dpdk.org/guides/prog_guide/)

### 学习资源
- [DPDK Sample Applications](https://doc.dpdk.org/guides/sample_app_ug/)
- [DPDK Getting Started Guide](https://doc.dpdk.org/guides/linux_gsg/)
- [DPDK Blog](https://www.dpdk.org/blog/)

### 社区
- [DPDK Mailing List](https://mailing.dpdk.org/listinfo)
- [DPDK Slack](https://dpdkio.slack.com/)
- [DPDK GitHub](https://github.com/DPDK/dpdk)

## 贡献指南

欢迎贡献代码、报告问题或提出改进建议！

### 如何贡献

1. Fork 本项目
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

### 贡献方向

- 添加新的示例程序
- 改进现有教程文档
- 修复 bug
- 优化代码性能
- 翻译文档（英文等）
- 添加测试用例

## 许可证

本项目采用 [MIT License](LICENSE) 许可证。

## 致谢

感谢 DPDK 社区提供的强大框架和详细文档。

---

**祝学习愉快！如有问题，欢迎提 Issue！** 🚀