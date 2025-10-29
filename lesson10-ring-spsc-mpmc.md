# DPDK Ring: 单生产者单消费者与多生产者多消费者模式

## 文档简介

本文档详细介绍DPDK Ring的两种基础同步模式：
- **SP/SC模式**（Single Producer/Single Consumer）：单生产者单消费者
- **MP/MC模式**（Multi Producer/Multi Consumer）：多生产者多消费者

通过理论讲解和完整代码示例，帮助你掌握这两种模式的使用场景、性能特点和实现方法。

---

## Ring核心概念回顾

### 什么是DPDK Ring？

DPDK Ring是一个**无锁、固定大小的FIFO环形队列**，专为高性能数据交换设计。

**核心特性**：
- FIFO顺序：先入先出，保证顺序性
- 固定容量：创建时确定大小
- 无锁设计：使用CAS原子操作代替锁
- 高性能：批量操作、缓存友好

### Ring的工作原理

Ring由两对头尾指针组成：

```
       生产者端                     消费者端
    prod_head, prod_tail         cons_head, cons_tail
           ↓                              ↓
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
    |  |  |XX|XX|XX|XX|XX|  |  |  |  |  |  |  |  |  |
    +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
              ↑              ↑
           cons_tail      prod_head
```

**入队流程（单生产者）**：
1. 读取`prod_head`和`cons_tail`，计算可用空间
2. 将`prod_head`向前移动N个位置
3. 将数据写入Ring
4. 更新`prod_tail`完成入队

**多生产者的协调**：使用Compare-And-Swap (CAS)原子指令确保只有一个线程能成功修改`prod_head`。

### Bulk vs Burst操作

- **Bulk操作**：必须完全成功（入队/出队N个）或完全失败（0个）
- **Burst操作**：尽可能多地入队/出队（0到N个）

```c
/* Bulk：要么成功10个，要么失败 */
ret = rte_ring_enqueue_bulk(ring, objs, 10, NULL);

/* Burst：可能返回0-10之间任意值 */
ret = rte_ring_enqueue_burst(ring, objs, 10, NULL);
```

---

## 单生产者单消费者模式 (SP/SC)

### SP/SC模式详解

**Single Producer/Single Consumer**：专用的生产者线程和消费者线程。

**适用场景**：
- 专用的生产者线程和消费者线程
- 最高性能要求
- 线程间数据传递（如RX thread → Worker thread）
- Pipeline架构中的单点连接

**性能优势**：
- ✅ 无需CAS原子操作
- ✅ 无竞争开销
- ✅ CPU cache友好
- ✅ 比MP/MC快20-40%

**注意事项**：
- ⚠️ 必须严格保证只有一个生产者、一个消费者
- ⚠️ 违反规则会导致数据竞争和数据损坏

### 创建SP/SC Ring

```c
/* 创建SP/SC Ring */
struct rte_ring *ring = rte_ring_create(
    "spsc_ring",                    // Ring名称
    1024,                           // 容量
    rte_socket_id(),                // NUMA节点
    RING_F_SP_ENQ | RING_F_SC_DEQ   // SP/SC标志
);
```

### SP/SC API使用

```c
/* 生产者使用SP API */
unsigned sent = rte_ring_sp_enqueue_burst(ring,
                                          (void **)msgs,
                                          batch_size,
                                          NULL);

/* 消费者使用SC API */
unsigned received = rte_ring_sc_dequeue_burst(ring,
                                              (void **)msgs,
                                              batch_size,
                                              NULL);
```

### 完整示例：SP/SC生产者-消费者

```c
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_launch.h>
#include <rte_malloc.h>

#define RING_SIZE 1024
#define BATCH_SIZE 32
#define TOTAL_MESSAGES (10 * 1000 * 1000)  // 10M messages

/* 消息结构 */
struct message {
    uint64_t seq_num;
    uint64_t timestamp;
    uint32_t producer_id;
    char payload[52];
} __rte_cache_aligned;

/* 全局变量 */
static struct rte_ring *g_ring;
static volatile int g_stop = 0;

/* 统计信息 */
struct stats {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t start_cycles;
    uint64_t end_cycles;
} __rte_cache_aligned;

static struct stats producer_stats;
static struct stats consumer_stats;

/* 信号处理 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nSignal received, stopping...\n");
        g_stop = 1;
    }
}

/* 生产者线程 */
static int producer_thread(__rte_unused void *arg)
{
    struct message *msgs[BATCH_SIZE];
    unsigned int i, sent;
    uint64_t seq_num = 0;
    unsigned int lcore_id = rte_lcore_id();

    printf("[Producer] Starting on lcore %u\n", lcore_id);

    /* 预分配消息 */
    for (i = 0; i < BATCH_SIZE; i++) {
        msgs[i] = rte_malloc(NULL, sizeof(struct message), 0);
        if (msgs[i] == NULL) {
            printf("Failed to allocate message\n");
            return -1;
        }
    }

    producer_stats.start_cycles = rte_get_tsc_cycles();

    while (!g_stop && seq_num < TOTAL_MESSAGES) {
        /* 准备一批消息 */
        for (i = 0; i < BATCH_SIZE && seq_num < TOTAL_MESSAGES; i++) {
            msgs[i]->seq_num = seq_num++;
            msgs[i]->timestamp = rte_get_tsc_cycles();
            msgs[i]->producer_id = lcore_id;
            snprintf(msgs[i]->payload, sizeof(msgs[i]->payload),
                     "Message %lu", msgs[i]->seq_num);
        }

        /* SP Burst入队 */
        sent = rte_ring_sp_enqueue_burst(g_ring, (void **)msgs, i, NULL);
        producer_stats.messages_sent += sent;

        if (sent < i) {
            /* Ring满，暂停 */
            rte_pause();
        }
    }

    producer_stats.end_cycles = rte_get_tsc_cycles();
    printf("[Producer] Finished: sent %lu messages\n",
           producer_stats.messages_sent);

    return 0;
}

/* 消费者线程 */
static int consumer_thread(__rte_unused void *arg)
{
    struct message *msgs[BATCH_SIZE];
    unsigned int i, received;
    unsigned int lcore_id = rte_lcore_id();

    printf("[Consumer] Starting on lcore %u\n", lcore_id);

    consumer_stats.start_cycles = rte_get_tsc_cycles();

    while (!g_stop || !rte_ring_empty(g_ring)) {
        /* SC Burst出队 */
        received = rte_ring_sc_dequeue_burst(g_ring, (void **)msgs,
                                             BATCH_SIZE, NULL);

        if (received == 0) {
            rte_pause();
            continue;
        }

        /* 处理消息 */
        for (i = 0; i < received; i++) {
            /* 模拟处理 */
            rte_pause();
            rte_free(msgs[i]);
        }

        consumer_stats.messages_received += received;
    }

    consumer_stats.end_cycles = rte_get_tsc_cycles();
    printf("[Consumer] Finished: received %lu messages\n",
           consumer_stats.messages_received);

    return 0;
}

/* 打印统计信息 */
static void print_stats(void)
{
    uint64_t hz = rte_get_tsc_hz();
    double producer_time = (double)(producer_stats.end_cycles -
                                    producer_stats.start_cycles) / hz;
    double consumer_time = (double)(consumer_stats.end_cycles -
                                    consumer_stats.start_cycles) / hz;
    double producer_mpps = producer_stats.messages_sent / producer_time / 1000000.0;
    double consumer_mpps = consumer_stats.messages_received / consumer_time / 1000000.0;

    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   SP/SC Performance Statistics         ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Producer: %lu messages                 ║\n", producer_stats.messages_sent);
    printf("║   Time: %.3f seconds                   ║\n", producer_time);
    printf("║   Throughput: %.2f Mpps                ║\n", producer_mpps);
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Consumer: %lu messages                 ║\n", consumer_stats.messages_received);
    printf("║   Time: %.3f seconds                   ║\n", consumer_time);
    printf("║   Throughput: %.2f Mpps                ║\n", consumer_mpps);
    printf("╚════════════════════════════════════════╝\n\n");
}

int main(int argc, char **argv)
{
    int ret;
    unsigned int producer_lcore, consumer_lcore;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    if (rte_lcore_count() < 2) {
        printf("Error: Need at least 2 lcores\n");
        return -1;
    }

    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   DPDK Ring SP/SC Mode Demo           ║\n");
    printf("╚════════════════════════════════════════╝\n\n");

    /* 创建SP/SC Ring */
    g_ring = rte_ring_create("spsc_ring", RING_SIZE, rte_socket_id(),
                             RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (g_ring == NULL) {
        printf("Failed to create ring\n");
        return -1;
    }

    printf("✓ Created SP/SC ring (capacity: %u)\n", rte_ring_get_capacity(g_ring));

    /* 分配lcores */
    producer_lcore = rte_get_next_lcore(-1, 1, 0);
    consumer_lcore = rte_get_next_lcore(producer_lcore, 1, 0);

    printf("  Producer on lcore %u\n", producer_lcore);
    printf("  Consumer on lcore %u\n\n", consumer_lcore);

    /* 启动线程 */
    rte_eal_remote_launch(producer_thread, NULL, producer_lcore);
    rte_eal_remote_launch(consumer_thread, NULL, consumer_lcore);

    /* 等待完成 */
    rte_eal_mp_wait_lcore();

    /* 打印统计 */
    print_stats();

    rte_ring_free(g_ring);
    rte_eal_cleanup();

    return 0;
}
```

### 编译和运行

```bash
# Makefile
APP = ring_spsc
SRCS-y := $(APP).c
PKGCONF ?= pkg-config
PC_FILE := libdpdk.pc
CFLAGS += -O3 $(shell $(PKGCONF) --cflags $(PC_FILE))
LDFLAGS_SHARED = $(shell $(PKGCONF) --libs $(PC_FILE))

build: $(APP)
$(APP): $(SRCS-y) Makefile
	$(CC) $(CFLAGS) $(SRCS-y) -o $(APP) $(LDFLAGS_SHARED)
clean:
	rm -f $(APP)

# 编译运行
make clean && make
sudo ./ring_spsc -l 0-1
```

### 预期性能

- **Intel Xeon E5-2680 v3**: 约8-12 Mpps
- **AMD EPYC 7551**: 约10-15 Mpps
- **Intel Xeon Gold 6248R**: 约15-20 Mpps

---

## 多生产者多消费者模式 (MP/MC)

### MP/MC模式详解

**Multi Producer/Multi Consumer**：默认模式，支持多个生产者和多个消费者并发操作。

**工作原理**：
使用CAS（Compare-And-Swap）原子指令协调多个生产者/消费者：

```c
// 伪代码：多生产者入队
do {
    prod_head = ring->prod.head;
    cons_tail = ring->cons.tail;
    free_entries = capacity - (prod_head - cons_tail);

    if (n > free_entries)
        return 0;  // 空间不足

    prod_next = prod_head + n;
} while (!CAS(&ring->prod.head, prod_head, prod_next));  // CAS可能失败，需重试

// 写入数据...

// 等待轮到我更新tail
while (ring->prod.tail != prod_head)
    rte_pause();

ring->prod.tail = prod_next;
```

**适用场景**：
- 多个worker线程共享一个Ring
- RX线程池将包分发到worker pool
- 通用的多对多通信场景
- 不确定生产者/消费者数量的场景

**性能特点**：
- 比SP/SC慢20-40%（有竞争开销）
- 竞争越激烈，性能下降越明显
- 可通过增大BATCH_SIZE缓解

### 创建MP/MC Ring

```c
/* 创建MP/MC Ring（默认模式，flags=0） */
struct rte_ring *ring = rte_ring_create(
    "mpmc_ring",
    4096,              // 增大容量减少竞争
    rte_socket_id(),
    0                  // flags=0表示MP/MC模式
);
```

### MP/MC API使用

```c
/* 多生产者使用MP API */
unsigned sent = rte_ring_mp_enqueue_burst(ring,
                                          (void **)msgs,
                                          batch_size,
                                          NULL);

/* 多消费者使用MC API */
unsigned received = rte_ring_mc_dequeue_burst(ring,
                                              (void **)msgs,
                                              batch_size,
                                              NULL);
```

### 完整示例：MP/MC多生产者多消费者

```c
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_launch.h>
#include <rte_malloc.h>

#define RING_SIZE 4096
#define BATCH_SIZE 32
#define NUM_PRODUCERS 2
#define NUM_CONSUMERS 2
#define MESSAGES_PER_PRODUCER (5 * 1000 * 1000)  // 5M per producer

/* 消息结构 */
struct message {
    uint64_t seq_num;
    uint64_t timestamp;
    uint32_t producer_id;
    uint32_t consumer_id;
    char payload[48];
} __rte_cache_aligned;

/* 全局Ring */
static struct rte_ring *g_ring;
static volatile int g_stop = 0;

/* 统计信息 */
struct worker_stats {
    uint64_t messages_processed;
    uint64_t operations_failed;
    uint64_t start_cycles;
    uint64_t end_cycles;
    uint32_t lcore_id;
} __rte_cache_aligned;

static struct worker_stats producer_stats[NUM_PRODUCERS];
static struct worker_stats consumer_stats[NUM_CONSUMERS];

/* 信号处理 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nSignal received, stopping...\n");
        g_stop = 1;
    }
}

/* 生产者线程 */
static int producer_thread(void *arg)
{
    int producer_id = *(int *)arg;
    struct message *msgs[BATCH_SIZE];
    unsigned int i, sent;
    uint64_t seq_num = 0;
    unsigned int lcore_id = rte_lcore_id();
    struct worker_stats *stats = &producer_stats[producer_id];

    stats->lcore_id = lcore_id;
    printf("[Producer %d] Starting on lcore %u\n", producer_id, lcore_id);

    /* 预分配消息 */
    for (i = 0; i < BATCH_SIZE; i++) {
        msgs[i] = rte_zmalloc(NULL, sizeof(struct message), 0);
        if (msgs[i] == NULL)
            return -1;
    }

    stats->start_cycles = rte_get_tsc_cycles();

    while (!g_stop && seq_num < MESSAGES_PER_PRODUCER) {
        /* 准备一批消息 */
        unsigned int batch = RTE_MIN(BATCH_SIZE,
                                      MESSAGES_PER_PRODUCER - seq_num);
        for (i = 0; i < batch; i++) {
            msgs[i]->seq_num = seq_num++;
            msgs[i]->timestamp = rte_get_tsc_cycles();
            msgs[i]->producer_id = producer_id;
            snprintf(msgs[i]->payload, sizeof(msgs[i]->payload),
                     "P%d-MSG%lu", producer_id, msgs[i]->seq_num);
        }

        /* MP Burst入队 */
        sent = rte_ring_mp_enqueue_burst(g_ring, (void **)msgs,
                                          batch, NULL);

        stats->messages_processed += sent;

        if (sent < batch) {
            stats->operations_failed++;
            rte_pause();
        }
    }

    stats->end_cycles = rte_get_tsc_cycles();

    /* 释放未使用的消息 */
    for (i = 0; i < BATCH_SIZE; i++)
        rte_free(msgs[i]);

    printf("[Producer %d] Finished: sent %lu messages\n",
           producer_id, stats->messages_processed);

    return 0;
}

/* 消费者线程 */
static int consumer_thread(void *arg)
{
    int consumer_id = *(int *)arg;
    struct message *msgs[BATCH_SIZE];
    unsigned int i, received;
    unsigned int lcore_id = rte_lcore_id();
    struct worker_stats *stats = &consumer_stats[consumer_id];
    uint64_t producer_count[NUM_PRODUCERS] = {0};

    stats->lcore_id = lcore_id;
    printf("[Consumer %d] Starting on lcore %u\n", consumer_id, lcore_id);

    stats->start_cycles = rte_get_tsc_cycles();

    while (!g_stop || !rte_ring_empty(g_ring)) {
        /* MC Burst出队 */
        received = rte_ring_mc_dequeue_burst(g_ring, (void **)msgs,
                                              BATCH_SIZE, NULL);

        if (received == 0) {
            stats->operations_failed++;
            rte_pause();
            continue;
        }

        /* 处理消息 */
        for (i = 0; i < received; i++) {
            msgs[i]->consumer_id = consumer_id;

            /* 统计每个生产者的消息 */
            if (msgs[i]->producer_id < NUM_PRODUCERS)
                producer_count[msgs[i]->producer_id]++;

            rte_pause();
            rte_free(msgs[i]);
        }

        stats->messages_processed += received;
    }

    stats->end_cycles = rte_get_tsc_cycles();

    printf("[Consumer %d] Finished: received %lu messages\n",
           consumer_id, stats->messages_processed);
    printf("[Consumer %d] Distribution: ", consumer_id);
    for (i = 0; i < NUM_PRODUCERS; i++)
        printf("P%d=%lu ", i, producer_count[i]);
    printf("\n");

    return 0;
}

/* 打印统计信息 */
static void print_stats(void)
{
    uint64_t hz = rte_get_tsc_hz();
    uint64_t total_produced = 0, total_consumed = 0;
    double max_producer_time = 0, max_consumer_time = 0;
    int i;

    printf("\n╔════════════════════════════════════════════════╗\n");
    printf("║   MP/MC Performance Statistics                 ║\n");
    printf("╠════════════════════════════════════════════════╣\n");

    /* 生产者统计 */
    printf("║ Producers:                                     ║\n");
    for (i = 0; i < NUM_PRODUCERS; i++) {
        double time = (double)(producer_stats[i].end_cycles -
                               producer_stats[i].start_cycles) / hz;
        double mpps = producer_stats[i].messages_processed / time / 1000000.0;

        printf("║   [P%d] Lcore %u: %8lu msgs, %.2f Mpps    ║\n",
               i, producer_stats[i].lcore_id,
               producer_stats[i].messages_processed, mpps);

        total_produced += producer_stats[i].messages_processed;
        if (time > max_producer_time)
            max_producer_time = time;
    }

    printf("║   Total: %lu messages                         ║\n", total_produced);
    printf("║   Aggregate: %.2f Mpps                        ║\n",
           total_produced / max_producer_time / 1000000.0);

    printf("╠════════════════════════════════════════════════╣\n");

    /* 消费者统计 */
    printf("║ Consumers:                                     ║\n");
    for (i = 0; i < NUM_CONSUMERS; i++) {
        double time = (double)(consumer_stats[i].end_cycles -
                               consumer_stats[i].start_cycles) / hz;
        double mpps = consumer_stats[i].messages_processed / time / 1000000.0;

        printf("║   [C%d] Lcore %u: %8lu msgs, %.2f Mpps    ║\n",
               i, consumer_stats[i].lcore_id,
               consumer_stats[i].messages_processed, mpps);

        total_consumed += consumer_stats[i].messages_processed;
        if (time > max_consumer_time)
            max_consumer_time = time;
    }

    printf("║   Total: %lu messages                         ║\n", total_consumed);
    printf("║   Aggregate: %.2f Mpps                        ║\n",
           total_consumed / max_consumer_time / 1000000.0);

    printf("╠════════════════════════════════════════════════╣\n");

    /* 验证 */
    if (total_produced == total_consumed)
        printf("║ ✓ Verification: All messages accounted for    ║\n");
    else
        printf("║ ✗ ERROR: Mismatch (P=%lu, C=%lu)             ║\n",
               total_produced, total_consumed);

    printf("╚════════════════════════════════════════════════╝\n\n");
}

int main(int argc, char **argv)
{
    int ret, i;
    static int producer_ids[NUM_PRODUCERS];
    static int consumer_ids[NUM_CONSUMERS];
    unsigned int lcore_id, assigned = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    if (rte_lcore_count() < (NUM_PRODUCERS + NUM_CONSUMERS + 1)) {
        printf("Error: Need at least %d lcores\n",
               NUM_PRODUCERS + NUM_CONSUMERS + 1);
        return -1;
    }

    printf("\n╔════════════════════════════════════════════════╗\n");
    printf("║   DPDK Ring MP/MC Mode Demo                   ║\n");
    printf("╚════════════════════════════════════════════════╝\n\n");

    /* 创建MP/MC Ring */
    g_ring = rte_ring_create("mpmc_ring", RING_SIZE, rte_socket_id(), 0);
    if (g_ring == NULL) {
        printf("Failed to create ring\n");
        return -1;
    }

    printf("✓ Created MP/MC ring (capacity: %u)\n", rte_ring_get_capacity(g_ring));
    printf("  Config: %d producers, %d consumers\n\n",
           NUM_PRODUCERS, NUM_CONSUMERS);

    /* 启动生产者和消费者 */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (assigned < NUM_PRODUCERS) {
            producer_ids[assigned] = assigned;
            printf("  Producer %d on lcore %u\n", assigned, lcore_id);
            rte_eal_remote_launch(producer_thread,
                                  &producer_ids[assigned], lcore_id);
            assigned++;
        } else if (assigned < NUM_PRODUCERS + NUM_CONSUMERS) {
            int consumer_idx = assigned - NUM_PRODUCERS;
            consumer_ids[consumer_idx] = consumer_idx;
            printf("  Consumer %d on lcore %u\n", consumer_idx, lcore_id);
            rte_eal_remote_launch(consumer_thread,
                                  &consumer_ids[consumer_idx], lcore_id);
            assigned++;
        } else {
            break;
        }
    }

    printf("\n");

    /* 等待完成 */
    rte_eal_mp_wait_lcore();

    /* 打印统计 */
    print_stats();

    rte_ring_free(g_ring);
    rte_eal_cleanup();

    return 0;
}
```

### 编译和运行

```bash
# 编译
make clean && make APP=ring_mpmc

# 运行（需要5个lcores：1主+2生产者+2消费者）
sudo ./ring_mpmc -l 0-4
```

---

## SP/SC vs MP/MC 性能对比

### 性能对比表

| 模式 | 线程配置 | 吞吐量(Mpps) | 相对性能 | 适用场景 |
|------|---------|--------------|----------|----------|
| SP/SC | 1P+1C | 10-15 | 100% (基准) | 专用线程 |
| MP/MC | 1P+1C | 8-12 | 80-85% | 单对通信 |
| MP/MC | 2P+2C | 6-10 | 60-70% | 中等并发 |
| MP/MC | 4P+4C | 4-8 | 40-55% | 高并发 |

### 性能下降原因分析

1. **CAS原子操作开销**：MP/MC需要CAS协调，SP/SC不需要
2. **Cache line竞争**：多个线程竞争同一cache line
3. **等待tail更新**：生产者需等待前一个生产者更新tail
4. **重试开销**：CAS失败需要重试

### 优化建议

**对于SP/SC**：
- ✅ 确保严格的单生产者单消费者
- ✅ 使用专用的lcore绑定
- ✅ NUMA本地访问

**对于MP/MC**：
- ✅ 增大RING_SIZE（减少冲突概率）
- ✅ 增大BATCH_SIZE（减少CAS次数）
- ✅ 使用`__rte_cache_aligned`避免false sharing
- ✅ 考虑多个Ring代替单个Ring

---

## 常见问题

### Q1: 什么时候用SP/SC，什么时候用MP/MC？

**使用SP/SC的场景**：
- 确定只有一个生产者和一个消费者
- 对性能要求极高
- Pipeline架构中的点对点连接

**使用MP/MC的场景**：
- 多个生产者或消费者
- 无法预知线程数量
- 通用场景

### Q2: 在MP/MC Ring上使用SP/SC API会怎样？

会导致数据竞争和数据损坏！必须匹配：
- MP/MC Ring → 使用MP/MC API
- SP/SC Ring → 使用SP/SC API

### Q3: 如何选择RING_SIZE？

- **SP/SC**：1024-2048即可
- **MP/MC**：建议4096-8192，减少竞争
- 必须是2的幂次（或使用`RING_F_EXACT_SZ`）

### Q4: BATCH_SIZE如何设置？

- 建议32-64
- 太小：频繁调用，开销大
- 太大：可能Ring容量不够

### Q5: 能否动态切换模式？

不能。Ring创建时确定模式，运行时无法更改。

---

## 总结

### SP/SC模式

✅ **优点**：
- 最高性能（无CAS开销）
- 延迟低且可预测
- 实现简单

⚠️ **注意**：
- 必须严格保证单生产者单消费者
- 不适合多对多场景

### MP/MC模式

✅ **优点**：
- 通用性强，支持多对多
- 实现了无锁并发
- 数据一致性有保证

⚠️ **注意**：
- 性能比SP/SC低20-40%
- 竞争越多，性能下降越明显
- 需要合理配置RING_SIZE和BATCH_SIZE

### 选择建议

```
需要最高性能？
  └─> 是否单生产者单消费者？
        └─> 是：使用 SP/SC
        └─> 否：使用 MP/MC

默认场景：使用 MP/MC（更安全）
```

---

## 参考资源

**官方文档**：
- Ring Library: https://doc.dpdk.org/guides/prog_guide/ring_lib.html

**示例代码**：
- Multi-process: `examples/multi_process/`
- Packet Ordering: `examples/packet_ordering/`

**下一步学习**：
- HTS模式（适合过度提交场景）
- RTS模式（松弛尾部同步）
- Zero-Copy API（减少拷贝开销）

---

**完成本文档学习后，你应该能够：**
- ✅ 理解SP/SC和MP/MC的工作原理
- ✅ 选择合适的模式
- ✅ 实现生产者-消费者模型
- ✅ 优化Ring性能
