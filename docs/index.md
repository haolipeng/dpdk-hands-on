---
layout: home

hero:
  name: "DPDK Hands-on"
  text: "DPDK 入门实战教程"
  tagline: 从零开始学习 DPDK 高性能网络编程
  actions:
    - theme: brand
      text: 开始学习
      link: /lessons/lesson1-helloworld
    - theme: alt
      text: 在 GitHub 上查看
      link: https://github.com/haolipeng/dpdk-hands-on

features:
  - icon: 📚
    title: 详细的中文教程
    details: 每个示例都配有完整的中文教程，从基础到进阶，循序渐进，适合 DPDK 零基础学习者
  - icon: 💻
    title: 完整的代码示例
    details: 13+ 个可运行的示例程序，覆盖 EAL、内存池、Ring 队列、多进程等核心功能
  - icon: 🛠️
    title: 现代化构建系统
    details: 使用 CMake 构建，自动检测 DPDK 依赖，支持 Debug/Release 模式切换
  - icon: 🚀
    title: 循序渐进
    details: 从 Hello World 到多进程架构，从基础概念到实际应用，一步步掌握 DPDK
---

## 课程概览

| 课程 | 主题 | 难度 | 需要网卡 |
|------|------|------|----------|
| Lesson 1 | Hello World - EAL 初始化 | ⭐ | ❌ |
| Lesson 2 | Hash 哈希表使用 | ⭐⭐ | ❌ |
| Lesson 3 | 网卡初始化与数据包捕获 | ⭐⭐⭐ | ✅ |
| Lesson 4 | 数据包协议解析 | ⭐⭐⭐ | ✅ |
| Lesson 5 | Mempool 内存池管理 | ⭐⭐ | ❌ |
| Lesson 6 | Flow Manager 流管理 | ⭐⭐⭐⭐ | ✅ |
| Lesson 7 | 多进程架构 | ⭐⭐⭐⭐ | ❌ |
| Lesson 8 | 进程间通信 | ⭐⭐⭐ | ❌ |
| Lesson 9 | Timer 定时器 | ⭐⭐ | ❌ |
| Lesson 10 | Ring SP/SC & MP/MC | ⭐⭐⭐ | ❌ |
| Lesson 12 | Ring HTS 模式 | ⭐⭐⭐ | ❌ |
| Lesson 13 | Mbuf 入门 | ⭐⭐ | ❌ |

## 快速开始

### 环境要求

- **操作系统**: Linux (推荐 Ubuntu 20.04+)
- **DPDK**: 版本 22.11+
- **编译工具**: GCC 7.0+, CMake 3.10+, pkg-config

### 构建项目

```bash
# 克隆项目
git clone https://github.com/haolipeng/dpdk-hands-on.git
cd dpdk-hands-on

# 配置大页内存
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# 构建
mkdir -p build && cd build
cmake ..
make

# 运行第一个示例
sudo ../bin/helloworld -l 0 --no-pci
```

## 参考资源

- [DPDK 官方文档](https://doc.dpdk.org/)
- [DPDK 编程指南](https://doc.dpdk.org/guides/prog_guide/)
- [DPDK API 参考](https://doc.dpdk.org/api/)
