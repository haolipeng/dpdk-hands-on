# Lesson 25: DPDK Eventdev - 事件驱动编程框架

## 目录
- [1. Eventdev 简介](#1-eventdev-简介)
- [2. 为什么需要 Eventdev](#2-为什么需要-eventdev)
- [3. 核心概念](#3-核心概念)
- [4. 调度模式详解](#4-调度模式详解)
- [5. 事件生命周期](#5-事件生命周期)
- [6. API 详解](#6-api-详解)
- [7. 代码示例解析](#7-代码示例解析)
- [8. 软件 vs 硬件 Eventdev](#8-软件-vs-硬件-eventdev)
- [9. 高级特性](#9-高级特性)
- [10. 性能优化](#10-性能优化)
- [11. 实际应用场景](#11-实际应用场景)
- [12. 常见问题](#12-常见问题)

---

## 1. Eventdev 简介

### 1.1 什么是 Eventdev

DPDK Eventdev（Event Device）是一个**事件驱动编程框架**，提供了统一的 API 来处理事件调度和分发。它抽象了硬件和软件事件调度器，让开发者可以编写可移植的事件驱动应用。

**核心特性**：
- ✅ **统一的 API**：支持多种硬件和软件实现
- ✅ **灵活的调度**：支持 Atomic、Ordered、Parallel 三种调度模式
- ✅ **高性能**：硬件加速 + 软件实现
- ✅ **负载均衡**：自动分发事件到多个 worker
- ✅ **服务优先级**：支持事件优先级和流隔离

### 1.2 版本要求

- **最低 DPDK 版本**：17.11
- **推荐版本**：20.11 或更高（功能更完善）

---

## 2. 为什么需要 Eventdev

### 2.1 传统编程模型的局限性

#### Run-to-Completion 模型

```c
while (1) {
    // 1. 接收数据包
    nb_rx = rte_eth_rx_burst(port, queue, mbufs, 32);

    // 2. 处理每个包
    for (i = 0; i < nb_rx; i++) {
        stage1_process(mbufs[i]);  // 阶段 1
        stage2_process(mbufs[i]);  // 阶段 2
        stage3_process(mbufs[i]);  // 阶段 3
    }

    // 3. 发送数据包
    rte_eth_tx_burst(port, queue, mbufs, nb_rx);
}
```

**问题**：
- ❌ 所有处理在单个线程完成，无法充分利用多核
- ❌ 负载不均衡，某个核心可能很忙，其他核心空闲
- ❌ 扩展性差，增加处理阶段需要重写代码

#### Pipeline 模型

```c
// Stage 1 (lcore 1)
while (1) {
    nb_rx = rte_eth_rx_burst(...);
    rte_ring_enqueue_bulk(ring1, mbufs, nb_rx);
}

// Stage 2 (lcore 2)
while (1) {
    nb = rte_ring_dequeue_bulk(ring1, mbufs, 32);
    process_stage2(mbufs);
    rte_ring_enqueue_bulk(ring2, mbufs, nb);
}

// Stage 3 (lcore 3)
while (1) {
    nb = rte_ring_dequeue_bulk(ring2, mbufs, 32);
    process_stage3(mbufs);
    rte_eth_tx_burst(...);
}
```

**问题**：
- ❌ 需要手动管理 ring 和线程分配
- ❌ 负载不均衡，某个 stage 可能成为瓶颈
- ❌ 难以动态调整，扩展性差

### 2.2 Eventdev 的优势

```
传统 Pipeline 模型:
┌────┐    ┌────┐    ┌────┐    ┌────┐
│ RX │───>│ S1 │───>│ S2 │───>│ TX │  (固定流水线)
└────┘    └────┘    └────┘    └────┘

Eventdev 模型:
┌────┐    ┌─────────────┐    ┌────────────────┐
│ RX │───>│  Eventdev   │───>│ Worker Pool    │
│    │    │  Scheduler  │    │ W1  W2  W3  W4 │  (动态负载均衡)
└────┘    └─────────────┘    └────────────────┘
                                │
                                └──> TX
```

**Eventdev 带来的好处**：

1. **自动负载均衡**
   - Eventdev 自动分发事件到空闲的 worker
   - 无需手动管理 worker 分配

2. **灵活的调度**
   - Atomic：保证同一流的事件串行处理
   - Ordered：保证事件顺序
   - Parallel：并行处理，无顺序要求

3. **简化开发**
   - 统一的 API，支持软件和硬件实现
   - 无需手动管理 ring 和同步

4. **高扩展性**
   - 动态增加 worker
   - 支持多个优先级队列

5. **硬件加速**
   - 利用硬件调度器（如 Intel DLB、Marvell OCTEON）
   - 大幅提升性能

---

## 3. 核心概念

### 3.1 Event（事件）

**Event** 是 Eventdev 中的基本数据单元，包含数据和元数据。

```c
struct rte_event {
    uint64_t event;             // 事件元数据（packed）
    union {
        uint64_t u64;
        void *event_ptr;
        struct rte_mbuf *mbuf;  // 通常使用 mbuf
        // ...
    };
};
```

**事件元数据**包含：
- `queue_id`：目标队列 ID
- `priority`：事件优先级
- `event_type`：事件类型（用户自定义）
- `sub_event_type`：子事件类型
- `sched_type`：调度类型（Atomic/Ordered/Parallel）
- `flow_id`：流 ID（用于调度）

### 3.2 Event Queue（事件队列）

**Event Queue** 是事件的容器，定义了事件的调度策略。

```c
struct rte_event_queue_conf {
    uint32_t nb_atomic_flows;          // Atomic 流的数量
    uint32_t nb_atomic_order_sequences; // Atomic 顺序序列数
    uint32_t event_queue_cfg;           // 队列配置标志
    uint8_t schedule_type;              // 调度类型
    uint8_t priority;                   // 队列优先级
};
```

**队列类型**：
- **Atomic Queue**：保证同一流的事件串行处理
- **Ordered Queue**：保证事件顺序
- **Parallel Queue**：并行处理，无顺序要求
- **Single-Link Queue**：仅链接到一个端口

### 3.3 Event Port（事件端口）

**Event Port** 是 worker 与 eventdev 交互的接口。

```c
struct rte_event_port_conf {
    int32_t new_event_threshold;   // 新事件阈值
    uint16_t dequeue_depth;         // 出队深度
    uint16_t enqueue_depth;         // 入队深度
    uint32_t event_port_cfg;        // 端口配置标志
};
```

**端口作用**：
- Producer 使用端口入队事件
- Worker 使用端口出队和处理事件
- 支持多个端口并发操作

### 3.4 Scheduler（调度器）

**Scheduler** 负责将事件从队列分发到端口。

```
┌──────────┐       ┌─────────────────┐       ┌──────────┐
│ Producer │──────>│   Event Queue   │       │ Worker 1 │
│          │       │                 │  ┌───>│ (Port 1) │
└──────────┘       │   Scheduler     │──┤    └──────────┘
                   │                 │  │
                   │  - Atomic       │  │    ┌──────────┐
                   │  - Ordered      │  ├───>│ Worker 2 │
                   │  - Parallel     │  │    │ (Port 2) │
                   │                 │  │    └──────────┘
                   └─────────────────┘  │
                                        │    ┌──────────┐
                                        └───>│ Worker 3 │
                                             │ (Port 3) │
                                             └──────────┘
```

---

## 4. 调度模式详解

### 4.1 Atomic 调度（RTE_SCHED_TYPE_ATOMIC）

**特性**：保证同一流（flow）的事件被同一个 worker 串行处理。

```
Flow A:  E1 ──> E2 ──> E3
Flow B:  E4 ──> E5 ──> E6

调度结果:
Worker 1: E1 ──> E2 ──> E3  (Flow A 的所有事件)
Worker 2: E4 ──> E5 ──> E6  (Flow B 的所有事件)
```

**应用场景**：
- TCP 连接处理（同一连接的包必须串行）
- 状态机处理（需要维护状态）
- 流统计（需要累加计数器）

**优点**：
- ✅ 自动保证流隔离
- ✅ 无需手动加锁
- ✅ 适合有状态处理

**缺点**：
- ❌ 流之间负载可能不均衡
- ❌ 需要定义好 flow_id

### 4.2 Ordered 调度（RTE_SCHED_TYPE_ORDERED）

**特性**：允许并行处理，但保证输出顺序与输入顺序一致。

```
输入顺序: E1 -> E2 -> E3 -> E4

并行处理:
Worker 1: E1 ────────> (处理完成)
Worker 2:    E2 ──> (处理完成)
Worker 3:       E3 ─────> (处理完成)
Worker 4:          E4 ──> (处理完成)

输出顺序: E1 -> E2 -> E3 -> E4  (保持原顺序)
```

**应用场景**：
- 数据包重排序（IPsec、IP 分片重组）
- 日志记录（需要保持时间顺序）
- 协议处理（需要保证顺序）

**优点**：
- ✅ 并行处理，提高吞吐量
- ✅ 自动保证顺序
- ✅ 适合无状态但需要顺序的场景

**缺点**：
- ❌ 实现复杂度高（硬件支持更好）
- ❌ 慢事件会阻塞后续事件

### 4.3 Parallel 调度（RTE_SCHED_TYPE_PARALLEL）

**特性**：完全并行处理，无顺序保证。

```
输入: E1, E2, E3, E4

并行处理:
Worker 1: E1
Worker 2: E2
Worker 3: E3
Worker 4: E4

输出: E3, E1, E4, E2  (顺序可能不同)
```

**应用场景**：
- 独立包处理（路由查找、ACL）
- 统计聚合（最终汇总）
- 数据包丢弃/转发（无状态）

**优点**：
- ✅ 最高吞吐量
- ✅ 最简单的实现
- ✅ 最佳负载均衡

**缺点**：
- ❌ 无顺序保证
- ❌ 不适合有状态处理

### 4.4 调度模式对比

| 特性 | Atomic | Ordered | Parallel |
|------|--------|---------|----------|
| **并行性** | 流级并行 | 完全并行 | 完全并行 |
| **顺序保证** | 流内有序 | 全局有序 | 无保证 |
| **吞吐量** | 中等 | 高 | 最高 |
| **复杂度** | 中等 | 高 | 低 |
| **适用场景** | 有状态处理 | 需要顺序 | 无状态处理 |

---

## 5. 事件生命周期

### 5.1 事件流程

```
1. 创建事件
   ┌──────────────┐
   │  Producer    │
   │  创建 Event  │
   └──────┬───────┘
          │
          v
2. 入队事件
   ┌──────────────┐
   │ rte_event_   │
   │ enqueue()    │
   └──────┬───────┘
          │
          v
3. 调度
   ┌──────────────┐
   │  Scheduler   │
   │  选择 Worker │
   └──────┬───────┘
          │
          v
4. 出队
   ┌──────────────┐
   │  Worker      │
   │ dequeue()    │
   └──────┬───────┘
          │
          v
5. 处理
   ┌──────────────┐
   │  处理事件     │
   │  业务逻辑     │
   └──────┬───────┘
          │
          v
6. 释放/转发
   ┌──────────────┐
   │ 释放 mbuf /  │
   │ 转发到下一阶段│
   └──────────────┘
```

### 5.2 事件操作类型

```c
enum rte_event_op_type {
    RTE_EVENT_OP_NEW,       // 新事件
    RTE_EVENT_OP_FORWARD,   // 转发到下一个队列
    RTE_EVENT_OP_RELEASE,   // 释放事件
};
```

**典型使用**：
```c
// Producer: 创建新事件
event.op = RTE_EVENT_OP_NEW;
rte_event_enqueue_burst(dev_id, port_id, &event, 1);

// Worker: 处理并转发
event.op = RTE_EVENT_OP_FORWARD;
event.queue_id = next_queue_id;
rte_event_enqueue_burst(dev_id, port_id, &event, 1);

// Worker: 处理并释放
rte_mbuf_free(event.mbuf);
// Atomic 模式下会自动释放，无需显式 RELEASE
```

---

## 6. API 详解

### 6.1 设备管理 API

#### 获取设备信息

```c
int rte_event_dev_info_get(uint8_t dev_id,
                           struct rte_event_dev_info *dev_info);
```

**示例**：
```c
struct rte_event_dev_info info;
rte_event_dev_info_get(dev_id, &info);

printf("Max queues: %u\n", info.max_event_queues);
printf("Max ports: %u\n", info.max_event_ports);
printf("Max events: %d\n", info.max_num_events);
```

#### 配置设备

```c
int rte_event_dev_configure(uint8_t dev_id,
                            const struct rte_event_dev_config *dev_conf);
```

**示例**：
```c
struct rte_event_dev_config config = {
    .nb_events_limit = 4096,
    .nb_event_queues = 2,
    .nb_event_ports = 4,
    .nb_event_queue_flows = 1024,
    .nb_event_port_dequeue_depth = 32,
    .nb_event_port_enqueue_depth = 32,
};

rte_event_dev_configure(dev_id, &config);
```

#### 队列配置

```c
int rte_event_queue_setup(uint8_t dev_id, uint8_t queue_id,
                          const struct rte_event_queue_conf *queue_conf);
```

**示例**：
```c
struct rte_event_queue_conf queue_conf = {
    .nb_atomic_flows = 1024,
    .nb_atomic_order_sequences = 1024,
    .schedule_type = RTE_SCHED_TYPE_ATOMIC,
    .priority = RTE_EVENT_DEV_PRIORITY_NORMAL,
};

rte_event_queue_setup(dev_id, 0, &queue_conf);
```

#### 端口配置

```c
int rte_event_port_setup(uint8_t dev_id, uint8_t port_id,
                         const struct rte_event_port_conf *port_conf);
```

**示例**：
```c
struct rte_event_port_conf port_conf = {
    .new_event_threshold = 1024,
    .dequeue_depth = 32,
    .enqueue_depth = 32,
};

rte_event_port_setup(dev_id, 0, &port_conf);
```

#### 端口-队列链接

```c
int rte_event_port_link(uint8_t dev_id, uint8_t port_id,
                        const uint8_t queues[], const uint8_t priorities[],
                        uint16_t nb_links);
```

**示例**：
```c
// 链接所有队列到端口 0（自动链接）
rte_event_port_link(dev_id, 0, NULL, NULL, 0);

// 手动指定链接
uint8_t queues[] = {0, 1};
uint8_t priorities[] = {0, 1};
rte_event_port_link(dev_id, 0, queues, priorities, 2);
```

#### 启动/停止设备

```c
int rte_event_dev_start(uint8_t dev_id);
void rte_event_dev_stop(uint8_t dev_id);
```

### 6.2 事件操作 API

#### 入队事件

```c
uint16_t rte_event_enqueue_burst(uint8_t dev_id, uint8_t port_id,
                                 const struct rte_event ev[],
                                 uint16_t nb_events);
```

**示例**：
```c
struct rte_event events[BURST_SIZE];

// 构造事件
for (int i = 0; i < BURST_SIZE; i++) {
    events[i].queue_id = 0;
    events[i].op = RTE_EVENT_OP_NEW;
    events[i].sched_type = RTE_SCHED_TYPE_ATOMIC;
    events[i].flow_id = hash_value;  // 流 ID
    events[i].mbuf = mbufs[i];
}

// 批量入队
uint16_t sent = rte_event_enqueue_burst(dev_id, port_id, events, BURST_SIZE);
```

#### 出队事件

```c
uint16_t rte_event_dequeue_burst(uint8_t dev_id, uint8_t port_id,
                                 struct rte_event ev[], uint16_t nb_events,
                                 uint64_t timeout_ticks);
```

**示例**：
```c
struct rte_event events[BURST_SIZE];

// 出队事件（非阻塞）
uint16_t nb = rte_event_dequeue_burst(dev_id, port_id, events, BURST_SIZE, 0);

// 处理事件
for (int i = 0; i < nb; i++) {
    process_event(&events[i]);
}
```

---

## 7. 代码示例解析

### 7.1 示例架构

我们的示例实现了一个简单的事件驱动应用：

```
┌────────────┐       ┌─────────────────┐       ┌──────────────┐
│  Producer  │──────>│  Event Queue 0  │──────>│  Worker 0    │
│  (lcore 1) │       │                 │       │  (lcore 2)   │
└────────────┘       │   (Atomic)      │       └──────────────┘
                     │                 │
                     │   Scheduler     │       ┌──────────────┐
                     │                 │──────>│  Worker 1    │
                     │                 │       │  (lcore 3)   │
                     └─────────────────┘       └──────────────┘
```

### 7.2 Eventdev 配置

```c
static int
setup_eventdev(void)
{
    uint8_t dev_id = app_cfg.eventdev_id;
    struct rte_event_dev_config dev_conf = {0};
    struct rte_event_queue_conf queue_conf = {0};
    struct rte_event_port_conf port_conf = {0};

    /* 配置 eventdev */
    dev_conf.nb_events_limit = MAX_EVENTS;
    dev_conf.nb_event_queues = 1;  /* 1 个队列 */
    dev_conf.nb_event_ports = 1 + app_cfg.num_workers;  /* 1 producer + N workers */
    dev_conf.nb_event_queue_flows = 1024;
    dev_conf.nb_event_port_dequeue_depth = 32;
    dev_conf.nb_event_port_enqueue_depth = 32;

    rte_event_dev_configure(dev_id, &dev_conf);

    /* 配置队列（Atomic 调度） */
    queue_conf.nb_atomic_flows = 1024;
    queue_conf.schedule_type = RTE_SCHED_TYPE_ATOMIC;
    rte_event_queue_setup(dev_id, 0, &queue_conf);

    /* 配置端口 */
    port_conf.dequeue_depth = 32;
    port_conf.enqueue_depth = 32;
    port_conf.new_event_threshold = MAX_EVENTS;

    for (int i = 0; i < (1 + app_cfg.num_workers); i++) {
        rte_event_port_setup(dev_id, i, &port_conf);
        rte_event_port_link(dev_id, i, NULL, NULL, 0);  /* 链接所有队列 */
    }

    /* 启动 eventdev */
    rte_event_dev_start(dev_id);

    return 0;
}
```

**关键点**：
- 使用 1 个 Atomic 队列
- Producer 使用 port 0
- Workers 使用 port 1-N
- 所有端口都链接到队列 0

### 7.3 Producer 线程

```c
static int
producer_thread(void *arg)
{
    uint8_t dev_id = app_cfg.eventdev_id;
    uint8_t port_id = 0;  /* Producer port */
    struct rte_event events[BURST_SIZE];
    struct rte_mbuf *mbufs[BURST_SIZE];
    uint32_t event_count = 0;

    while (!force_quit) {
        /* 分配 mbuf */
        rte_pktmbuf_alloc_bulk(mbuf_pool, mbufs, BURST_SIZE);

        /* 构造事件 */
        for (int i = 0; i < BURST_SIZE; i++) {
            uint32_t *data = rte_pktmbuf_mtod(mbufs[i], uint32_t *);
            *data = event_count++;

            events[i].queue_id = 0;
            events[i].op = RTE_EVENT_OP_NEW;
            events[i].sched_type = RTE_SCHED_TYPE_ATOMIC;
            events[i].event_type = EVENT_TYPE_NORMAL;
            events[i].priority = RTE_EVENT_DEV_PRIORITY_NORMAL;
            events[i].mbuf = mbufs[i];
        }

        /* 入队事件 */
        uint16_t sent = rte_event_enqueue_burst(dev_id, port_id,
                                                events, BURST_SIZE);

        producer_stats.events_produced += sent;

        rte_delay_us(100);  /* 限速 */
    }

    return 0;
}
```

**工作流程**：
1. 分配 mbuf 并填充数据
2. 构造 rte_event 结构
3. 批量入队到 eventdev
4. 更新统计信息

### 7.4 Worker 线程

```c
static int
worker_thread(void *arg)
{
    uint8_t dev_id = app_cfg.eventdev_id;
    uint8_t port_id = *((uint8_t *)arg);  /* Worker port ID */
    struct rte_event events[BURST_SIZE];
    int worker_id = port_id - 1;

    while (!force_quit) {
        /* 出队事件 */
        uint16_t nb = rte_event_dequeue_burst(dev_id, port_id,
                                              events, BURST_SIZE, 0);

        if (nb == 0) {
            rte_pause();
            continue;
        }

        /* 处理事件 */
        for (int i = 0; i < nb; i++) {
            struct rte_mbuf *m = events[i].mbuf;
            uint32_t *data = rte_pktmbuf_mtod(m, uint32_t *);

            /* 业务处理 */
            (void)data;

            /* 释放 mbuf */
            rte_pktmbuf_free(m);

            worker_stats[worker_id].events_consumed++;
        }
    }

    return 0;
}
```

**工作流程**：
1. 从 eventdev 出队事件
2. 处理事件数据
3. 释放 mbuf
4. 更新统计信息

**注意**：Atomic 模式下，事件处理完成后会自动释放锁，无需手动 RELEASE。

---

## 8. 软件 vs 硬件 Eventdev

### 8.1 软件 Eventdev (SW PMD)

**特性**：
- 纯软件实现的事件调度器
- 适合测试和开发
- 支持所有调度模式

**使用方式**：
```bash
# 启动时添加 vdev
sudo ./app -l 0-3 --vdev=event_sw0
```

**优点**：
- ✅ 无需硬件支持
- ✅ 灵活，易于调试
- ✅ 适合开发和测试

**缺点**：
- ❌ 性能较硬件差
- ❌ CPU 开销较大

### 8.2 硬件 Eventdev

**支持的硬件**：

| 厂商 | 产品 | PMD 名称 |
|------|------|---------|
| **Intel** | DLB (Dynamic Load Balancer) | `dlb2` |
| **Marvell** | OCTEON TX/TX2 | `octeontx` |
| **NXP** | DPAA/DPAA2 | `dpaa/dpaa2` |

**特性**：
- 硬件加速调度
- 超高性能（数百 Mpps）
- 低延迟（纳秒级）

**使用方式**：
```bash
# DLB 示例
sudo ./app -l 0-15 -a 0000:6f:00.0
```

**优点**：
- ✅ 极高性能
- ✅ 低 CPU 开销
- ✅ 支持大规模并发

**缺点**：
- ❌ 需要特定硬件
- ❌ 成本较高

### 8.3 性能对比

| 指标 | 软件 PMD | 硬件 PMD (DLB) |
|------|----------|----------------|
| **吞吐量** | 10-50 Mpps | 200+ Mpps |
| **延迟** | 微秒级 | 纳秒级 |
| **CPU 开销** | 高 | 低 |
| **成本** | 无 | 高 |

---

## 9. 高级特性

### 9.1 事件优先级

```c
// 设置队列优先级
queue_conf.priority = RTE_EVENT_DEV_PRIORITY_HIGHEST;

// 设置事件优先级
event.priority = RTE_EVENT_DEV_PRIORITY_HIGHEST;
```

**应用**：
- 控制平面 vs 数据平面
- 紧急消息优先处理

### 9.2 事件定时器（Event Timer）

```c
#include <rte_event_timer_adapter.h>

// 创建定时器适配器
struct rte_event_timer_adapter *adapter;
adapter = rte_event_timer_adapter_create(&adapter_conf);

// 启动定时器
struct rte_event_timer timer;
timer.ev = event;
timer.timeout_ticks = rte_get_timer_hz() * 5;  // 5 秒
rte_event_timer_arm_burst(adapter, &timer, 1);
```

**应用**：
- 超时处理
- 周期性任务
- 心跳检测

### 9.3 以太网 RX 适配器

```c
#include <rte_event_eth_rx_adapter.h>

// 创建 RX 适配器
rte_event_eth_rx_adapter_create(adapter_id, dev_id, &adapter_conf);

// 添加接收队列
rte_event_eth_rx_adapter_queue_add(adapter_id, port_id, queue_id, &queue_conf);

// 启动适配器
rte_event_eth_rx_adapter_start(adapter_id);
```

**优势**：
- 自动将网卡收到的包转换为事件
- 无需手动轮询和入队

### 9.4 以太网 TX 适配器

```c
#include <rte_event_eth_tx_adapter.h>

// 创建 TX 适配器
rte_event_eth_tx_adapter_create(adapter_id, dev_id, &adapter_conf);

// 添加发送队列
rte_event_eth_tx_adapter_queue_add(adapter_id, port_id, queue_id);
```

---

## 10. 性能优化

### 10.1 批量处理

```c
// ❌ 不推荐：单个事件
for (int i = 0; i < count; i++) {
    rte_event_enqueue_burst(dev_id, port_id, &events[i], 1);
}

// ✅ 推荐：批量操作
rte_event_enqueue_burst(dev_id, port_id, events, count);
```

### 10.2 流 ID 优化

```c
// 使用哈希计算流 ID
uint32_t hash = rte_hash_crc_4byte(src_ip ^ dst_ip, 0);
event.flow_id = hash % queue_conf.nb_atomic_flows;
```

**原则**：
- 流 ID 分布均匀
- 避免哈希冲突

### 10.3 端口深度调优

```c
// 根据工作负载调整
port_conf.dequeue_depth = 64;  // 增加批量大小
port_conf.enqueue_depth = 64;
```

### 10.4 NUMA 优化

```c
// 在本地 socket 创建资源
int socket_id = rte_socket_id();
mbuf_pool = rte_pktmbuf_pool_create("pool", 8192, 0, 0, 2048, socket_id);
```

---

## 11. 实际应用场景

### 11.1 5G 网络处理

```c
// UPF (User Plane Function)
Atomic Queue:  GTP-U 隧道处理（同一会话串行）
Ordered Queue: 包重排序
Parallel Queue: 路由查找
```

### 11.2 IPsec Gateway

```c
Queue 0 (Parallel):  路由查找
Queue 1 (Ordered):   加密/解密
Queue 2 (Ordered):   包重排序
Queue 3 (Parallel):  发送
```

### 11.3 DPI（深度包检测）

```c
Queue 0 (Atomic):    协议解析（同一流）
Queue 1 (Parallel):  模式匹配
Queue 2 (Parallel):  日志记录
```

### 11.4 负载均衡

```c
Queue 0 (Parallel):  接收
Queue 1 (Atomic):    会话查找（同一会话）
Queue 2 (Parallel):  转发
```

---

## 12. 常见问题

### 12.1 编译错误

**问题**：找不到 `rte_eventdev.h`

**解决**：
```bash
# 确保 DPDK >= 17.11
pkg-config --modversion libdpdk
```

### 12.2 运行时错误

**问题**：`No event devices found`

**解决**：
```bash
# 使用软件 eventdev
sudo ./app --vdev=event_sw0

# 检查硬件设备
dpdk-devbind.py --status-dev event
```

### 12.3 性能问题

**问题**：吞吐量低于预期

**检查清单**：
- [ ] 批量大小是否足够（32-64）
- [ ] 是否使用了硬件 eventdev
- [ ] Worker 数量是否匹配 CPU 核心
- [ ] 流 ID 分布是否均匀
- [ ] NUMA 配置是否正确

### 12.4 调度问题

**问题**：Atomic 模式下出现乱序

**原因**：flow_id 设置错误

**解决**：
```c
// 确保同一流的 flow_id 相同
event.flow_id = hash(src_ip, dst_ip, src_port, dst_port);
```

---

## 总结

DPDK Eventdev 是一个**强大而灵活**的事件驱动框架：

✅ **优势**：
- 统一的 API，支持软硬件
- 自动负载均衡
- 多种调度模式（Atomic/Ordered/Parallel）
- 硬件加速支持

⚠️ **注意事项**：
- 需要理解调度模式的差异
- 正确设置 flow_id
- 注意 NUMA 优化

🎯 **适用场景**：
- 复杂的事件驱动应用
- 需要负载均衡的场景
- 多阶段流水线处理
- 5G/NFV/云原生应用

📚 **下一步学习**：
1. 尝试不同调度模式
2. 集成 RX/TX 适配器
3. 使用 Event Timer
4. 测试硬件 eventdev（如果有条件）

---

## 参考资料

- [DPDK Eventdev Library](https://doc.dpdk.org/guides/prog_guide/eventdev.html)
- [Event Device Drivers](https://doc.dpdk.org/guides/eventdevs/index.html)
- [L3fwd with Eventdev](https://doc.dpdk.org/guides/sample_app_ug/l3_forward.html#l3-fwd-eventdev)
- [Intel DLB Documentation](https://www.intel.com/content/www/us/en/products/docs/processors/xeon/dynamic-load-balancer.html)
