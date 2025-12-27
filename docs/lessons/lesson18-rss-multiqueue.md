# DPDK RSS 和多队列处理完全指南

> 从单核到多核: 解锁 10Gbps+ 流量处理能力

---

## 开始之前: 为什么需要多队列?

### 问题: 单队列的瓶颈

想象你正在处理 **10Gbps** 的网络流量:

```
传统单队列模式:
┌────────┐      ┌────────┐      ┌────────┐
│  NIC   │─────>│ 队列0  │─────>│ CPU 0  │  ← 单核处理,成为瓶颈!
└────────┘      └────────┘      └────────┘
               14.88 Mpps
             (64字节包 @ 10Gbps)

问题:
  • 单核最多处理 3-5 Mpps → 丢包!
  • CPU 利用率 100% → 无法扩展!
  • 其他 CPU 核心空闲 → 浪费!
```

### 解决方案: RSS + 多队列

```
RSS 多队列模式:
                    ┌─────────┐
                    │   NIC   │
                    │  (RSS)  │  ← 硬件自动分发
                    └────┬────┘
                         │
        ┌────────┬───────┼───────┬────────┐
        │        │       │       │        │
    ┌───▼──┐ ┌──▼───┐ ┌─▼────┐ ┌▼─────┐
    │队列0 │ │队列1 │ │队列2 │ │队列3 │
    └───┬──┘ └──┬───┘ └─┬────┘ └┬─────┘
        │       │       │       │
    ┌───▼──┐ ┌──▼───┐ ┌─▼────┐ ┌▼─────┐
    │CPU 0 │ │CPU 1 │ │CPU 2 │ │CPU 3 │  ← 多核并行!
    └──────┘ └──────┘ └──────┘ └──────┘
    3.7Mpps  3.7Mpps  3.7Mpps  3.7Mpps
           = 14.8 Mpps 总吞吐量 ✓

优势:
  ✓ 线性扩展: 4核 = 4倍性能
  ✓ 负载均衡: 自动分发
  ✓ 同流有序: 同一流总是到相同队列
```

---

## 第一课: 核心概念

### 1.1 什么是 RSS?

**RSS (Receive Side Scaling)** = 接收端扩展

**核心思想**:
- NIC 硬件根据包的特征计算哈希值
- 哈希值映射到不同的 RX 队列
- 每个队列由不同的 CPU 核心处理

**关键特性**:
- **硬件实现**: 零 CPU 开销
- **流亲和性**: 同一条流总是到同一个队列(保证有序)
- **负载均衡**: 多条流自动分散

### 1.2 RSS 工作原理

```
步骤1: 提取包的关键字段 (5-tuple)
┌──────────────────────────────────────────┐
│ 源IP │ 目的IP │ 源端口 │ 目的端口 │ 协议 │
└──────────────────────────────────────────┘
         ↓
步骤2: 计算哈希值
    Hash = Toeplitz(Key, Tuple)
         ↓
步骤3: 映射到队列
    Queue = Hash % NumQueues
         ↓
┌────────────────┐
│   队列 2       │  ← 这个包到队列2
└────────────────┘
```

### 1.3 术语表

| 术语 | 说明 |
|------|------|
| **RSS** | Receive Side Scaling - 接收端扩展 |
| **RX Queue** | 接收队列 - 存储收到的包 |
| **TX Queue** | 发送队列 - 存储要发送的包 |
| **RSS Hash Key** | 哈希密钥 - 用于计算哈希值 |
| **Indirection Table** | 重定向表 - 哈希到队列的映射 |
| **Flow Affinity** | 流亲和性 - 同一流到同一队列 |
| **Burst** | 批量 - 一次处理多个包 |

---

##第二课: RSS 配置详解

### 2.1 配置 RSS 哈希函数

```c
#include <rte_ethdev.h>

struct rte_eth_conf port_conf = {
    .rxmode = {
        .mq_mode = RTE_ETH_MQ_RX_RSS,  // 启用 RSS 模式
    },
    .rx_adv_conf = {
        .rss_conf = {
            .rss_key = NULL,           // NULL = 使用默认密钥
            .rss_key_len = 40,         // 密钥长度(字节)
            .rss_hf = RTE_ETH_RSS_IP | // 基于 IP 哈希
                      RTE_ETH_RSS_TCP | // 基于 TCP 哈希
                      RTE_ETH_RSS_UDP,  // 基于 UDP 哈希
        },
    },
};
```

### 2.2 RSS 哈希类型

```c
/* L3 层哈希 */
RTE_ETH_RSS_IPV4          // IPv4 (源IP + 目的IP)
RTE_ETH_RSS_IPV6          // IPv6

/* L4 层哈希 */
RTE_ETH_RSS_TCP           // TCP (包含端口)
RTE_ETH_RSS_UDP           // UDP (包含端口)
RTE_ETH_RSS_SCTP          // SCTP

/* 组合哈希 */
RTE_ETH_RSS_IP            // 所有 IP 协议
RTE_ETH_RSS_L3_L4         // L3 + L4 (最常用)

/* 特殊哈希 */
RTE_ETH_RSS_NONFRAG_IPV4_TCP  // 非分片 TCP/IPv4
RTE_ETH_RSS_FRAG_IPV4         // 分片的 IPv4
```

**推荐配置**:
```c
// 流量分析系统推荐配置
.rss_hf = RTE_ETH_RSS_IP |
          RTE_ETH_RSS_TCP |
          RTE_ETH_RSS_UDP |
          RTE_ETH_RSS_SCTP;
```

### 2.3 配置多队列

```c
#define NB_RX_QUEUE 4  // 4个接收队列
#define NB_TX_QUEUE 4  // 4个发送队列

/* 初始化网卡 */
ret = rte_eth_dev_configure(port_id,
                            NB_RX_QUEUE,  // RX 队列数
                            NB_TX_QUEUE,  // TX 队列数
                            &port_conf);

/* 为每个队列分配资源 */
for (uint16_t q = 0; q < NB_RX_QUEUE; q++) {
    ret = rte_eth_rx_queue_setup(
        port_id,           // 端口ID
        q,                 // 队列ID
        nb_rxd,            // 描述符数量 (通常 1024)
        rte_socket_id(),   // NUMA 节点
        NULL,              // RX 配置 (NULL = 默认)
        mbuf_pool          // Mbuf 内存池
    );
}

for (uint16_t q = 0; q < NB_TX_QUEUE; q++) {
    ret = rte_eth_tx_queue_setup(
        port_id,           // 端口ID
        q,                 // 队列ID
        nb_txd,            // 描述符数量 (通常 1024)
        rte_socket_id(),   // NUMA 节点
        NULL               // TX 配置 (NULL = 默认)
    );
}
```

### 2.4 查询 RSS 配置

```c
struct rte_eth_rss_conf rss_conf;
uint8_t rss_key[40];

rss_conf.rss_key = rss_key;
rss_conf.rss_key_len = sizeof(rss_key);

// 获取当前 RSS 配置
ret = rte_eth_dev_rss_hash_conf_get(port_id, &rss_conf);

printf("RSS Hash Functions: 0x%lx\n", rss_conf.rss_hf);
printf("RSS Key Length: %u\n", rss_conf.rss_key_len);
```

---

## 第三课: Worker 核心模式

### 3.1 基本架构

```
主核心模式:
┌─────────────┐
│ 主核心 (0)  │
│  - 初始化   │
│  - 配置     │
│  - 统计     │
└─────────────┘
       │ 启动
       ▼
┌─────────────────────────────────────┐
│         Worker 核心池               │
│                                     │
│  ┌──────────┐  ┌──────────┐       │
│  │Worker 1  │  │Worker 2  │  ...  │
│  │- 收包    │  │- 收包    │       │
│  │- 处理    │  │- 处理    │       │
│  │- 统计    │  │- 统计    │       │
│  └──────────┘  └──────────┘       │
└─────────────────────────────────────┘
```

### 3.2 Worker 函数实现

```c
#define BURST_SIZE 32

/* Worker 核心处理函数 */
static int worker_main(void *arg)
{
    struct worker_params *params = (struct worker_params *)arg;
    uint16_t port_id = params->port_id;
    uint16_t queue_id = params->queue_id;
    unsigned lcore_id = rte_lcore_id();

    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;

    printf("Worker %u started on queue %u\n", lcore_id, queue_id);

    while (!force_quit) {
        /* Burst 收包 */
        nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0))
            continue;

        /* 处理每个包 */
        for (uint16_t i = 0; i < nb_rx; i++) {
            /* Prefetch 优化 */
            if (i + 3 < nb_rx)
                rte_prefetch0(rte_pktmbuf_mtod(bufs[i + 3], void *));

            /* 处理包 */
            process_packet(bufs[i], lcore_id, queue_id);

            /* 释放包 */
            rte_pktmbuf_free(bufs[i]);
        }

        /* 更新统计 */
        worker_stats[lcore_id].rx_packets += nb_rx;
    }

    return 0;
}
```

### 3.3 启动 Worker 核心

```c
struct worker_params params[RTE_MAX_LCORE];
unsigned lcore_id;
uint16_t queue_id = 0;

/* 为每个 Worker 核心分配队列 */
RTE_LCORE_FOREACH_WORKER(lcore_id) {
    if (queue_id >= NB_RX_QUEUE)
        break;

    params[lcore_id].port_id = port_id;
    params[lcore_id].queue_id = queue_id;

    /* 在 Worker 核心上启动处理函数 */
    rte_eal_remote_launch(worker_main, &params[lcore_id], lcore_id);

    queue_id++;
}

/* 等待所有 Worker 完成 */
rte_eal_mp_wait_lcore();
```

---

## 第四课: Burst 处理优化

### 4.1 为什么 Burst 处理快?

```
单包处理:
for (int i = 0; i < 1000; i++) {
    nb_rx = rte_eth_rx_burst(port, queue, &buf, 1);  // 收1个包
    process(buf);
    // 开销: 1000次函数调用 + 1000次上下文切换
}

Burst 处理:
nb_rx = rte_eth_rx_burst(port, queue, bufs, 32);  // 收32个包
for (int i = 0; i < nb_rx; i++) {
    process(bufs[i]);
    // 开销: 1次函数调用 + 少量上下文切换
}

性能提升: 10-50倍!
```

### 4.2 Prefetch 优化

```c
#define PREFETCH_OFFSET 3

/* 收包 */
nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

/* 处理包 - 带 Prefetch */
for (uint16_t i = 0; i < nb_rx; i++) {
    /* 提前加载后面的包到缓存 */
    if (i + PREFETCH_OFFSET < nb_rx) {
        rte_prefetch0(rte_pktmbuf_mtod(bufs[i + PREFETCH_OFFSET], void *));
    }

    /* 处理当前包 (数据已在缓存中) */
    process_packet(bufs[i]);
}

/*
原理:
  当处理包 i 时,包 i+3 已经加载到 CPU 缓存
  避免了缓存缺失(Cache Miss)的延迟
  性能提升: 20-40%
*/
```

### 4.3 批量查表优化

```c
/* 传统方式: 单个查询 */
for (int i = 0; i < nb_rx; i++) {
    flow_entry = hash_lookup(flow_key[i]);  // 单次查询
}

/* 优化方式: 批量查询 */
const void *keys[BURST_SIZE];
void *data[BURST_SIZE];
int32_t positions[BURST_SIZE];

/* 准备所有 key */
for (int i = 0; i < nb_rx; i++) {
    keys[i] = &flow_key[i];
}

/* 批量查询 (一次调用查多个) */
rte_hash_lookup_bulk(hash, keys, nb_rx, positions);

/* 处理结果 */
for (int i = 0; i < nb_rx; i++) {
    if (positions[i] >= 0) {
        // 找到了
    }
}

/* 性能提升: 2-5倍 */
```

---

## 第五课: 负载均衡策略

### 5.1 RSS 默认负载均衡

**优点**:
- 硬件实现,零开销
- 流亲和性保证(同流同队列)
- 自动均衡

**缺点**:
- 无法调整权重
- 大象流(Elephant Flow)问题

```
大象流问题:
队列0: ████████████████████  (1条超大流 - 过载!)
队列1: ██                    (几条小流)
队列2: ███                   (几条小流)
队列3: ██                    (几条小流)

RSS 无法识别大象流!
```

### 5.2 软件二次分发

```c
/*
架构:
  NIC → RSS → 少量队列 → 主 Worker → Ring → 多个子 Worker
                               ↓
                          二次哈希分发
*/

/* 主 Worker: 从队列收包,二次分发 */
static int main_worker(void *arg)
{
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;

    while (!force_quit) {
        nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

        /* 二次分发到 Ring */
        for (uint16_t i = 0; i < nb_rx; i++) {
            /* 根据流计算 ring ID */
            uint32_t hash = calculate_flow_hash(bufs[i]);
            uint16_t ring_id = hash % NUM_RINGS;

            /* 分发到对应 ring */
            if (rte_ring_enqueue(rings[ring_id], bufs[i]) < 0) {
                rte_pktmbuf_free(bufs[i]);
            }
        }
    }
    return 0;
}

/* 子 Worker: 从 Ring 取包处理 */
static int sub_worker(void *arg)
{
    uint16_t ring_id = *(uint16_t *)arg;
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_deq;

    while (!force_quit) {
        nb_deq = rte_ring_dequeue_burst(rings[ring_id],
                                        (void **)bufs,
                                        BURST_SIZE, NULL);

        for (uint16_t i = 0; i < nb_deq; i++) {
            process_packet(bufs[i]);
            rte_pktmbuf_free(bufs[i]);
        }
    }
    return 0;
}
```

### 5.3 动态负载均衡

```c
/* 监控每个 Worker 的负载 */
struct worker_load {
    uint64_t packets;
    uint64_t cpu_cycles;
    double load_factor;  // 0.0 - 1.0
};

/* 周期性调整 */
void rebalance_workers(void)
{
    /* 计算每个 Worker 的负载 */
    for (int i = 0; i < num_workers; i++) {
        double load = calculate_load(&workers[i]);

        if (load > 0.9) {
            /* 过载: 将一些流迁移到其他 Worker */
            migrate_flows(i, find_lightest_worker());
        }
    }
}
```

---

## 第六课: 常见问题和最佳实践

### 6.1 队列数量选择

```
推荐配置:
┌────────────────────┬─────────────┬─────────────┐
│   场景             │ RX 队列数   │ 说明        │
├────────────────────┼─────────────┼─────────────┤
│ 测试/开发          │ 1-2         │ 简单        │
│ 小规模生产 (< 1G)  │ 2-4         │ 足够        │
│ 中等流量 (1-10G)   │ 4-8         │ 推荐        │
│ 高流量 (> 10G)     │ 8-16        │ 多核并行    │
│ 超高流量 (40G+)    │ 16-32       │ 最大化      │
└────────────────────┴─────────────┴─────────────┘

原则:
  1. 队列数 ≤ CPU 核心数
  2. 队列数 = 2^n (2, 4, 8, 16...)
  3. 考虑 NUMA: 每个 NUMA 节点配置独立队列组
```

### 6.2 RSS vs Flow Director

```
┌──────────────┬─────────────────┬─────────────────┐
│              │      RSS        │ Flow Director   │
├──────────────┼─────────────────┼─────────────────┤
│ 实现方式     │ 硬件哈希        │ 硬件规则匹配    │
│ 灵活性       │ 低(固定算法)    │ 高(可编程)      │
│ 性能开销     │ 零              │ 零              │
│ 负载均衡     │ 自动            │ 手动配置        │
│ 流亲和性     │ ✓               │ ✓               │
│ 精确匹配     │ ✗               │ ✓               │
│ 优先级处理   │ ✗               │ ✓               │
└──────────────┴─────────────────┴─────────────────┘

使用建议:
  • RSS: 通用负载均衡 (大多数场景)
  • Flow Director: 特殊流量分类 (DDoS 防护等)
  • 组合使用: RSS + Flow Director (高级场景)
```

### 6.3 避免常见陷阱

#### 陷阱1: 队列数超过 CPU 核心数

```c
// ❌ 错误: 8个队列但只有4个核心
#define NB_RX_QUEUE 8
// 运行时只启动4个 Worker
// 结果: 4个队列无人处理,包堆积!

// ✓ 正确: 队列数 = Worker 核心数
#define NB_RX_QUEUE 4
```

#### 陷阱2: Burst Size 过小

```c
// ❌ 错误: Burst 太小
#define BURST_SIZE 4
// 性能差,频繁函数调用

// ✓ 正确: 使用合理的 Burst Size
#define BURST_SIZE 32  // 推荐: 32-64
```

#### 陷阱3: 忘记 NUMA 亲和性

```c
// ❌ 错误: 所有队列用 socket 0 的 mbuf pool
for (q = 0; q < NB_RX_QUEUE; q++) {
    rte_eth_rx_queue_setup(port, q, nb_rxd, 0, NULL, mbuf_pool_socket0);
}

// ✓ 正确: 根据 Worker 核心的 socket 选择 mbuf pool
for (q = 0; q < NB_RX_QUEUE; q++) {
    unsigned lcore = worker_lcores[q];
    unsigned socket = rte_lcore_to_socket_id(lcore);
    rte_eth_rx_queue_setup(port, q, nb_rxd, socket, NULL, mbuf_pools[socket]);
}
```

#### 陷阱4: 不检查队列能力

```c
// ❌ 错误: 假设网卡支持16个队列
#define NB_RX_QUEUE 16

// ✓ 正确: 查询网卡能力
struct rte_eth_dev_info dev_info;
rte_eth_dev_info_get(port_id, &dev_info);

uint16_t max_rx_queues = dev_info.max_rx_queues;
uint16_t nb_rx_queue = RTE_MIN(NB_RX_QUEUE, max_rx_queues);
```

---

## 第七课: 性能调优指南

### 7.1 性能指标

```c
/* 关键性能指标 */
struct performance_metrics {
    /* 吞吐量 */
    uint64_t rx_pps;         // 接收包速率 (packets/sec)
    uint64_t rx_bps;         // 接收比特率 (bits/sec)

    /* CPU */
    double cpu_usage;        // CPU 利用率 (%)
    uint64_t cycles_per_pkt; // 每包 CPU 周期数

    /* 队列 */
    uint64_t queue_drops;    // 队列丢包数
    double queue_utilization;// 队列使用率 (%)

    /* 延迟 */
    uint64_t avg_latency_ns; // 平均延迟 (纳秒)
};
```

### 7.2 性能基准

```
目标性能 (64字节包):
┌───────────┬──────────┬──────────────┬─────────────┐
│ 网卡速率  │ 理论 pps │ 实际 pps     │ CPU 核心    │
├───────────┼──────────┼──────────────┼─────────────┤
│ 1 Gbps    │ 1.488 M  │ 1.4+ M       │ 1 核        │
│ 10 Gbps   │ 14.88 M  │ 13+ M        │ 4-8 核      │
│ 40 Gbps   │ 59.52 M  │ 50+ M        │ 16-24 核    │
│ 100 Gbps  │ 148.8 M  │ 120+ M       │ 32-48 核    │
└───────────┴──────────┴──────────────┴─────────────┘

单核性能:
  • 空转收发: 3-5 Mpps
  • 简单处理: 2-3 Mpps  (哈希查找)
  • 复杂处理: 1-2 Mpps  (深度解析)
```

### 7.3 优化清单

```
□ RSS 已启用
□ 队列数 = Worker 核心数
□ Burst Size = 32-64
□ 使用 Prefetch (offset = 3)
□ Mbuf pool 大小充足 (> 8191)
□ CPU 频率设为 performance
□ 核心隔离 (isolcpus)
□ NUMA 亲和性正确
□ 禁用不必要的统计
□ 使用 Jumbo Frame (大包场景)
```

---

## 总结

### 核心要点

| 技术 | 作用 | 性能提升 |
|------|------|---------|
| **RSS** | 硬件负载均衡 | 线性扩展 (N核 = N倍) |
| **多队列** | 多核并行处理 | 10-100倍 |
| **Burst** | 批量处理 | 10-50倍 |
| **Prefetch** | 缓存优化 | 20-40% |

### 典型架构

```c
// 推荐配置
#define NB_RX_QUEUE 4      // 4个队列
#define BURST_SIZE 32      // Burst 32个包
#define PREFETCH_OFFSET 3  // Prefetch offset

// RSS 配置
.rss_hf = RTE_ETH_RSS_IP | RTE_ETH_RSS_TCP | RTE_ETH_RSS_UDP

// Worker 核心: 1个队列 = 1个核心
```

### 下一步

学完本课程后,你应该掌握:
1. ✅ RSS 的原理和配置
2. ✅ 多队列的设置和管理
3. ✅ Worker 核心模式
4. ✅ Burst 处理和 Prefetch 优化
5. ✅ 性能调优方法

**下一课预告**: Flow Director - 硬件流量分类和精确匹配

---

## 参考资料

- DPDK Programmer's Guide - Poll Mode Driver
- Intel Data Plane Performance Tuning Guide
- RSS 技术规范 (Microsoft Scalable Networking)
