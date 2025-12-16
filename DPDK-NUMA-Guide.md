# DPDK NUMA 架构基础指南

## 什么是 NUMA?

**NUMA (Non-Uniform Memory Access)** 非一致性内存访问架构，是现代多处理器系统的主流架构。

### 核心概念

- **传统 UMA (Uniform Memory Access)**: 所有CPU访问内存的速度一致
- **NUMA**: 每个CPU访问本地内存更快，访问远程内存较慢

```
传统 UMA 架构:
┌─────┐  ┌─────┐
│CPU 0│  │CPU 1│
└──┬──┘  └──┬──┘
   └────┬────┘
        │ (总线竞争)
    ┌───┴────┐
    │ Memory │
    └────────┘

NUMA 架构:
┌─────────────────┐      ┌─────────────────┐
│   NUMA Node 0   │      │   NUMA Node 1   │
│  ┌─────┐┌─────┐ │      │  ┌─────┐┌─────┐ │
│  │CPU 0││CPU 1│ │      │  │CPU 2││CPU 3│ │
│  └──┬──┘└──┬──┘ │      │  └──┬──┘└──┬──┘ │
│     └───┬───┘   │      │      └───┬───┘   │
│         │       │      │          │       │
│   ┌─────┴─────┐ │      │   ┌──────┴────┐ │
│   │Local Mem  │ │      │   │Local Mem  │ │
│   └───────────┘ │      │   └───────────┘ │
└─────────────────┘      └─────────────────┘
         │ 快速 QPI/UPI 互联 │ (跨节点访问慢)
```

## NUMA 术语

| 术语 | 说明 |
|------|------|
| **NUMA Node** | 一组CPU和本地内存的集合，通常对应一个物理CPU插槽(Socket) |
| **Socket** | 物理CPU插座，一个Socket = 一个NUMA节点 |
| **Local Memory** | 节点本地内存，访问延迟最低 |
| **Remote Memory** | 其他节点的内存，需要通过互联总线访问，延迟高2-3倍 |
| **lcore** | DPDK的逻辑核心，通常映射到物理CPU核心 |

## 性能差异

**内存访问延迟对比** (典型值):

| 访问类型 | 延迟 | 相对速度 |
|---------|------|---------|
| L1 Cache | ~4 cycles | 1x |
| L2 Cache | ~12 cycles | 3x |
| L3 Cache | ~40 cycles | 10x |
| **本地内存** | **~60-80 ns** | **1x** |
| **远程内存** | **~120-180 ns** | **2-3x** ⚠️ |

**实际影响**:
- 跨NUMA访问可导致 **30-50%** 性能下降
- 高频内存操作场景(如包处理)影响更大

## 查看 NUMA 信息

### 1. 使用 `numactl`

```bash
# 查看NUMA拓扑
numactl --hardware

# 输出示例:
# available: 2 nodes (0-1)
# node 0 cpus: 0 1 2 3
# node 0 size: 16384 MB
# node 1 cpus: 4 5 6 7
# node 1 size: 16384 MB
```

### 2. 使用 `lscpu`

```bash
lscpu | grep NUMA

# 输出示例:
# NUMA node(s):          2
# NUMA node0 CPU(s):     0-3
# NUMA node1 CPU(s):     4-7
```

### 3. 查看网卡所在NUMA节点

```bash
cat /sys/class/net/eth0/device/numa_node
# 输出: 0 (表示网卡在NUMA节点0上)
```

## DPDK 中的 NUMA

### 关键原则: **本地性优先**

> **Golden Rule**: 让数据处理发生在数据所在的NUMA节点

### 核心 API

#### 1. 获取当前lcore的NUMA节点

```c
unsigned socket_id = rte_socket_id();
printf("当前运行在Socket %u\n", socket_id);
```

#### 2. 在指定NUMA节点分配资源

```c
// 创建Ring时指定socket
struct rte_ring *ring = rte_ring_create(
    "my_ring",
    1024,
    socket_id,  // ← 关键参数: 在哪个NUMA节点分配
    0
);

// 创建Mempool时指定socket
struct rte_mempool *mp = rte_pktmbuf_pool_create(
    "mbuf_pool",
    NUM_MBUFS,
    CACHE_SIZE,
    0,
    RTE_MBUF_DEFAULT_BUF_SIZE,
    socket_id  // ← 关键参数
);
```

#### 3. 获取设备所在NUMA节点

```c
uint16_t port_id = 0;
int port_socket = rte_eth_dev_socket_id(port_id);

if (port_socket == SOCKET_ID_ANY) {
    port_socket = 0;  // 单NUMA系统或虚拟设备
}

printf("Port %u on Socket %d\n", port_id, port_socket);
```

## 最佳实践

### ✅ DO: 推荐做法

#### 1. **资源绑定到正确的NUMA节点**

```c
uint16_t port_id = 0;
int port_socket = rte_eth_dev_socket_id(port_id);

// 在网卡所在的NUMA节点创建mbuf pool
struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
    "mbuf_pool",
    NUM_MBUFS,
    MBUF_CACHE_SIZE,
    0,
    RTE_MBUF_DEFAULT_BUF_SIZE,
    port_socket  // 与网卡同节点
);

// 在当前lcore所在节点创建ring
struct rte_ring *ring = rte_ring_create(
    "process_ring",
    RING_SIZE,
    rte_socket_id(),  // 与处理线程同节点
    0
);
```

#### 2. **使用EAL参数绑定核心**

```bash
# 只使用NUMA节点0的核心
sudo ./app -l 0-3 --socket-mem=1024,0

# socket-mem: 指定每个NUMA节点预留的hugepage内存(MB)
# 格式: --socket-mem=<node0_mem>,<node1_mem>,...
```

#### 3. **多队列网卡按NUMA分配**

```c
// 假设网卡在Node 0，有4个RX队列
// Queue 0-1 → Node 0的CPU处理
// Queue 2-3 → Node 1的CPU处理(如果必须)

rte_eth_rx_queue_setup(port_id, 0, nb_rxd, 0, NULL, mbuf_pool_node0);
rte_eth_rx_queue_setup(port_id, 1, nb_rxd, 0, NULL, mbuf_pool_node0);
```

### ❌ DON'T: 避免的做法

#### 1. **跨NUMA访问内存池**

```c
// ❌ 错误: Node 1的lcore使用Node 0的mempool
// CPU在Node 1，但从Node 0的内存池分配mbuf
struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool_node0);
```

#### 2. **忽略网卡位置**

```c
// ❌ 错误: 网卡在Node 0，但使用Node 1的CPU处理
// 每次收包都跨NUMA访问PCIe设备
rte_eal_remote_launch(rx_thread, NULL, 4);  // CPU 4在Node 1
```

#### 3. **使用 SOCKET_ID_ANY**

```c
// ❌ 不推荐: 让系统随机选择
struct rte_ring *ring = rte_ring_create(
    "ring",
    1024,
    SOCKET_ID_ANY,  // 不可预测的性能
    0
);
```

## 多NUMA场景模式

### 模式1: 单NUMA处理 (推荐)

```
NUMA Node 0              NUMA Node 1
┌──────────────┐         ┌──────────────┐
│ NIC0 (eth0)  │         │ NIC1 (eth1)  │
│ CPU 0-3      │         │ CPU 4-7      │
│ Mempool 0    │         │ Mempool 1    │
│ Ring 0       │         │ Ring 1       │
└──────────────┘         └──────────────┘
    完全本地访问              完全本地访问
```

**启动示例**:
```bash
# 进程1处理Node 0
sudo ./app -l 0-3 --socket-mem=2048,0 -- -p 0x1

# 进程2处理Node 1
sudo ./app -l 4-7 --socket-mem=0,2048 -- -p 0x2
```

### 模式2: 跨NUMA转发 (性能损失)

```
NUMA Node 0              NUMA Node 1
┌──────────────┐         ┌──────────────┐
│ NIC (eth0)   │────────>│ CPU 4-7      │
│ Mempool      │  跨NUMA  │ 处理线程      │
└──────────────┘  访问    └──────────────┘
                  ⚠️ 性能下降30-50%
```

## 验证NUMA配置

### 检查运行时绑定

```c
void print_numa_info(void) {
    unsigned lcore_id;

    RTE_LCORE_FOREACH(lcore_id) {
        printf("Lcore %u on Socket %u\n",
               lcore_id,
               rte_lcore_to_socket_id(lcore_id));
    }
}
```

### 使用 `numastat` 监控

```bash
# 监控进程的NUMA内存使用
watch -n 1 'numastat -p $(pidof your_app)'

# 输出示例:
# Per-node process memory usage (in MBs)
# PID              Node 0 Node 1 Total
# 12345 (your_app)   1024      5  1029
#                    ↑本地   ↑远程
```

## 常见问题

### Q1: 虚拟机中没有NUMA怎么办?

**A**: 虚拟机通常只有一个NUMA节点，此时:
- `rte_socket_id()` 返回 0
- `rte_eth_dev_socket_id()` 可能返回 `SOCKET_ID_ANY`
- 代码仍可正常运行，只是没有NUMA优化效果

```c
int socket = rte_socket_id();
if (socket == SOCKET_ID_ANY)
    socket = 0;  // 默认使用Node 0
```

### Q2: 如何处理网卡跨NUMA的场景?

**A**: 三种策略:

1. **最优**: 使用网卡所在NUMA节点的CPU (推荐)
2. **次优**: 使用跨NUMA Core，但将Mempool放在网卡节点
3. **避免**: 不要将网卡、CPU、内存分散在多个节点

### Q3: 多进程如何处理NUMA?

**A**:
- Primary进程创建资源时指定socket
- Secondary进程attach时自动映射到正确的NUMA地址
- 需确保Primary运行在正确的NUMA节点上

```bash
# Primary在Node 0运行
sudo ./primary -l 0-1 --proc-type=primary --socket-mem=1024,0

# Secondary在Node 1运行
sudo ./secondary -l 4-5 --proc-type=secondary --socket-mem=0,1024
```

## 总结

| 场景 | 推荐做法 |
|------|---------|
| **单NUMA系统** | 无需特殊处理，使用`rte_socket_id()` |
| **多NUMA系统** | 网卡-CPU-内存必须在同一节点 |
| **创建Ring** | 使用worker lcore所在的socket |
| **创建Mempool** | 使用网卡所在的socket |
| **启动参数** | 用`-l`限制核心，`--socket-mem`预分配内存 |
| **性能调优** | 用`numastat`监控，避免跨节点访问 >10% |

## 延伸阅读

- DPDK Programmer's Guide - CPU Layout and NUMA
- Intel VTune Performance Profiler - NUMA分析
- `man numactl` - NUMA策略配置
