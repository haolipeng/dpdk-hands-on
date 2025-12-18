# Lesson 15: NUMA Demo

## 概述

本示例演示了 DPDK 中 NUMA (Non-Uniform Memory Access) 架构的使用和最佳实践。

## 功能特性

1. **NUMA 拓扑查询**
   - 查看系统中所有 NUMA 节点
   - 显示每个 lcore 所在的 NUMA 节点
   - 获取网卡所在的 NUMA 节点

2. **资源分配演示**
   - 在指定 NUMA 节点上创建 mempool
   - 在指定 NUMA 节点上创建 ring queue
   - 演示正确和错误的 NUMA 资源分配方式

3. **性能对比测试**
   - 测试本地 NUMA 节点访问性能
   - 测试跨 NUMA 节点访问性能
   - 量化跨 NUMA 访问的性能开销

4. **最佳实践指导**
   - 展示推荐的 NUMA 资源分配模式
   - 说明常见的 NUMA 使用误区
   - 提供实用的调试和监控建议

## 编译

```bash
cd build
cmake ..
make numa_demo
```

## 运行示例

### 基本运行 (单 NUMA 或虚拟机)

```bash
sudo ./bin/numa_demo -l 0 --no-pci
```

### 多 NUMA 系统运行

```bash
# 使用多个核心和指定内存分配
sudo ./bin/numa_demo -l 0-3 --socket-mem=1024,1024 --no-pci
```

### 指定运行在特定 NUMA 节点

```bash
# 只在 NUMA 节点 0 运行
sudo ./bin/numa_demo -l 0-1 --socket-mem=1024,0 --no-pci

# 只在 NUMA 节点 1 运行
sudo ./bin/numa_demo -l 4-5 --socket-mem=0,1024 --no-pci
```

## 输出说明

程序会输出以下信息:

1. **NUMA Topology Information**: 系统 NUMA 拓扑结构
2. **Network Ports NUMA Information**: 网卡所在的 NUMA 节点
3. **NUMA Best Practices**: 正确和错误的用法示例
4. **Performance Test Results**: 本地 vs 跨 NUMA 访问的性能对比

## 理解输出

### 性能影响指标

- **< 5% overhead**: 很好,可能是单 NUMA 系统或良好的缓存局部性
- **10-30% overhead**: 典型的跨 NUMA 访问开销
- **> 30% overhead**: 显著的性能损失,需要优化 NUMA 亲和性

## 系统要求

### 检查 NUMA 拓扑

```bash
# 查看 NUMA 节点信息
numactl --hardware

# 查看 CPU 和 NUMA 映射
lscpu | grep NUMA

# 查看网卡所在的 NUMA 节点
cat /sys/class/net/eth0/device/numa_node
```

### 监控 NUMA 内存使用

```bash
# 实时监控进程的 NUMA 内存分布
watch -n 1 'numastat -p $(pidof numa_demo)'
```

## 核心概念

### 什么是 NUMA?

NUMA 是一种多处理器架构,其中每个 CPU 有自己的本地内存:
- **本地内存访问**: 快速 (基准)
- **远程内存访问**: 慢 2-3 倍

### DPDK 中的关键 API

```c
// 获取当前 lcore 所在的 NUMA 节点
unsigned socket_id = rte_socket_id();

// 在指定 NUMA 节点创建 mempool
struct rte_mempool *pool = rte_pktmbuf_pool_create(
    "pool", NUM_MBUFS, CACHE_SIZE, 0,
    RTE_MBUF_DEFAULT_BUF_SIZE,
    socket_id  // ← 关键参数
);

// 获取网卡所在的 NUMA 节点
int port_socket = rte_eth_dev_socket_id(port_id);
```

### 黄金法则

> **让数据处理发生在数据所在的 NUMA 节点**

## 最佳实践

### ✅ 推荐做法

1. **资源和处理器绑定**
   ```bash
   # 网卡在 Node 0,使用 Node 0 的 CPU
   sudo ./app -l 0-3 --socket-mem=2048,0
   ```

2. **代码中正确指定 socket**
   ```c
   unsigned socket = rte_socket_id();
   struct rte_ring *ring = rte_ring_create("ring", 1024, socket, 0);
   ```

3. **按网卡位置创建 mempool**
   ```c
   int port_socket = rte_eth_dev_socket_id(port_id);
   struct rte_mempool *pool = rte_pktmbuf_pool_create(
       "pool", NUM_MBUFS, CACHE_SIZE, 0,
       RTE_MBUF_DEFAULT_BUF_SIZE, port_socket
   );
   ```

### ❌ 避免的做法

1. 使用 `SOCKET_ID_ANY` (不可预测)
2. 跨 NUMA 节点处理网卡数据
3. 在远程 NUMA 节点上创建频繁访问的资源

## 实际应用场景

### 场景 1: 单 NUMA 处理 (推荐)

```
NUMA Node 0              NUMA Node 1
┌──────────────┐         ┌──────────────┐
│ NIC0         │         │ NIC1         │
│ CPU 0-3      │         │ CPU 4-7      │
│ Mempool 0    │         │ Mempool 1    │
└──────────────┘         └──────────────┘
   完全本地访问              完全本地访问
```

### 场景 2: 跨 NUMA 转发 (性能损失)

```
NUMA Node 0              NUMA Node 1
┌──────────────┐         ┌──────────────┐
│ NIC          │────────>│ CPU 4-7      │
│ Mempool      │  跨NUMA  │ 处理线程      │
└──────────────┘  访问    └──────────────┘
                 ⚠️ 30-50% 性能下降
```

## 故障排查

### 虚拟机中没有 NUMA

虚拟机通常只有一个 NUMA 节点,代码仍可运行但不会有性能差异。

### 性能没有明显差异

可能原因:
1. 单 NUMA 系统
2. 虚拟化环境
3. 测试数据量太小,未触发内存瓶颈

### 无法创建资源

确保:
1. Hugepages 已配置
2. 有足够的内存在目标 NUMA 节点
3. 使用 `--socket-mem` 参数预分配内存

## 相关文档

- [docs/lessons/lesson15-basic-numa.md](../docs/lessons/lesson15-basic-numa.md) - NUMA 基础理论
- DPDK Programming Guide - CPU Layout
- `man numactl` - NUMA 策略配置

## 实用命令参考

```bash
# 查看 NUMA 拓扑
numactl --hardware

# 监控 NUMA 内存使用
numastat

# 查看进程的 NUMA 统计
numastat -p <pid>

# 在特定 NUMA 节点运行程序
numactl --cpunodebind=0 --membind=0 ./numa_demo

# 检查网卡 NUMA 位置
cat /sys/class/net/*/device/numa_node
```
