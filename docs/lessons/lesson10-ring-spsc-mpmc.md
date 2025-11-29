# DPDK Ring: 单生产者单消费者与多生产者多消费者模式

## 文档简介

本文档详细介绍DPDK Ring的两种基础同步模式：
- **SP/SC模式**（Single Producer/Single Consumer）：单生产者单消费者
- **MP/MC模式**（Multi Producer/Multi Consumer）：多生产者多消费者

通过理论讲解和完整代码示例，帮助你掌握这两种模式的使用场景、性能特点和实现方法。

---

老规矩，我们先看下dpdk的Ring队列的官网文档



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
unsigned sent = rte_ring_sp_enqueue

/* 消费者使用SC API */
unsigned received = rte_ring_sc_dequeue
```

### 完整示例：SP/SC生产者-消费者

```c
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_launch.h>
#include <rte_malloc.h>

#define RING_SIZE 1024
#define BATCH_SIZE 32
#define TOTAL_MESSAGES (10)  // 10 messages

/* 消息结构 */
struct message {
    uint64_t seq_num;
    char payload[52];
} __rte_cache_aligned;

/* 全局变量 */
static struct rte_ring *g_ring;
static volatile int g_stop = 0;

/* 统计信息 */
struct stats {
    uint64_t messages_sent;
    uint64_t messages_received;
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
    uint64_t seq_num = 0;
    unsigned int lcore_id = rte_lcore_id();

    printf("[Producer] Starting on lcore %u\n", lcore_id);

    while (!g_stop && seq_num < TOTAL_MESSAGES) {
        struct message *msg = rte_malloc(NULL, sizeof(struct message), 0);
        int ret;

        msg->seq_num = seq_num++;
        snprintf(msg->payload, sizeof(msg->payload), "Message %lu", msg->seq_num);

        ret = rte_ring_sp_enqueue(g_ring, msg);
        if (ret < 0) {
            rte_free(msg);
            break;
        }

        producer_stats.messages_sent++;
    }

    printf("[Producer] Finished: sent %lu messages\n",
           producer_stats.messages_sent);

    return 0;
}

/* 消费者线程 */
static int consumer_thread(__rte_unused void *arg)
{
    unsigned int lcore_id = rte_lcore_id();

    printf("[Consumer] Starting on lcore %u\n", lcore_id);

    for (;;) {
        if (g_stop)
		{
			printf("Ring is empty and stop is set, breaking\n");
			break;
		}

        struct message *msg = NULL;
        int ret = rte_ring_sc_dequeue(g_ring, (void **)&msg);

        if (ret == -ENOENT) {
            rte_pause();
            continue;
        }
        if (ret < 0 || msg == NULL)
            continue;

        /* 打印接收的信息并释放资源*/
		printf("Consumer received message: %s\n", msg->payload);
        rte_free(msg);

        consumer_stats.messages_received++;
    }

    printf("[Consumer] Finished: received %lu messages\n",
           consumer_stats.messages_received);

    return 0;
}

/* 打印统计信息 */
static void print_stats(void)
{
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   SP/SC Performance Statistics         ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Producer: %lu messages                 ║\n", producer_stats.messages_sent);
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Consumer: %lu messages                 ║\n", consumer_stats.messages_received);
    printf("╚════════════════════════════════════════╝\n\n");
	rte_delay_us_sleep(1000000);
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
    rte_eal_remote_launch(producer_thread, NULL, producer_lcore);//生产元素，投递到队列中
    rte_eal_remote_launch(consumer_thread, NULL, consumer_lcore);//从队列中消费元素

    /* 等待完成 */
    rte_eal_mp_wait_lcore();

    /* 打印统计 */
    print_stats();

    rte_ring_free(g_ring);
    rte_eal_cleanup();

    return 0;
}
```



---

## 多生产者多消费者模式 (MP/MC)

### MP/MC模式详解

**Multi Producer/Multi Consumer**：默认模式，支持多个生产者和多个消费者并发操作。



**使用场景：网络数据包处理**

- 多个收包线程分别从多个网卡采集数据包，放入同一个队列
- 多个工作线程从队列取包进行处理
- 这样可以充分利用多核CPU的性能



**性能特点**：

- 比SP/SC性能差一些（没具体测试过）

  

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
int rte_ring_mp_enqueue	(	struct rte_ring * 	r, void * 	obj)

/* 多消费者使用MC API */
 int rte_ring_mc_dequeue	(	struct rte_ring * 	r, void ** 	obj_p)	
```

### 完整示例：MP/MC多生产者多消费者

**生产者线程函数**

```c
static int producer_thread(void *arg)
{
    int producer_id = *(int *)arg;
    uint32_t seq = 0;

    //打印从哪个lcore上启动的producer线程
    printf("[Producer %d] start on lcore %u\n",
           producer_id, rte_lcore_id());

    while (!g_stop && seq < MESSAGES_PER_PRODUCER) {
        //为msg消息申请内存空间
        struct message *msg = rte_zmalloc(NULL, sizeof(*msg), 0);
        int ret;

        //为msg字段赋值
        msg->producer_id = producer_id;
        snprintf(msg->payload, sizeof(msg->payload),
                 "P%d-%u", producer_id, seq);

        do {
            //入队操作，如无可用空间，则rte_pause()等待一会重试
            ret = rte_ring_mp_enqueue(g_ring, msg);
            if (ret == -ENOBUFS) {
                rte_pause();
            }
        } while (ret == -ENOBUFS && !g_stop);

        if (ret < 0) {
            rte_free(msg);
            break;
        }

        seq++;
        __atomic_add_fetch(&g_total_produced, 1, __ATOMIC_RELAXED);
    }

    //如果所有生产者都完成了，则设置停止标志
    if (__atomic_add_fetch(&g_finished_producers, 1, __ATOMIC_RELAXED) ==
        NUM_PRODUCERS) {
        g_stop = 1;
    }

    printf("[Producer %d] finished, sent %u messages\n", producer_id, seq);
    return 0;
}
```

**消费者线程函数**

```
static int consumer_thread(void *arg)
{
    int consumer_id = *(int *)arg;

    printf("[Consumer %d] start on lcore %u\n",
           consumer_id, rte_lcore_id());

    for (;;) {
        struct message *msg = NULL;
        int ret;

        if (g_stop && rte_ring_empty(g_ring))
            break;

        ret = rte_ring_mc_dequeue(g_ring, (void **)&msg);
        if (ret == -ENOENT) {
            rte_pause();
            continue;
        }

        if (ret < 0 || msg == NULL)
            continue;

        msg->consumer_id = consumer_id;
        //记录消费了多少条消息
        __atomic_add_fetch(&g_total_consumed, 1, __ATOMIC_RELAXED);

        rte_free(msg);
    }

    printf("[Consumer %d] finished\n", consumer_id);
    return 0;
}
```

**主函数**

```
int main(int argc, char **argv)
{
    static int producer_ids[NUM_PRODUCERS];
    static int consumer_ids[NUM_CONSUMERS];
    int ret;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    //初始化EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

    if (rte_lcore_count() < NUM_PRODUCERS + NUM_CONSUMERS + 1) {
        rte_exit(EXIT_FAILURE,
                 "Need at least %d lcores\n",
                 NUM_PRODUCERS + NUM_CONSUMERS + 1);
    }

    //创建多生产者/多消费者类型的无锁队列
    g_ring = rte_ring_create(RING_NAME, RING_SIZE,
                             rte_socket_id(), 0);
    if (g_ring == NULL)
        rte_exit(EXIT_FAILURE, "Failed to create ring\n");

    printf("Ring created: capacity=%u, producers=%d, consumers=%d\n\n",
           rte_ring_get_capacity(g_ring), NUM_PRODUCERS, NUM_CONSUMERS);

    // Producer: 核心 1, 2
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_ids[i] = i;
        rte_eal_remote_launch(producer_thread, &producer_ids[i], PRODUCER_START_CORE + i);
    }

    // Consumer: 核心 3, 4
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        consumer_ids[i] = i;
        rte_eal_remote_launch(consumer_thread, &consumer_ids[i], CONSUMER_START_CORE + i);
    }

    rte_eal_mp_wait_lcore();

    printf("\nSummary:\n");
    printf("  Produced: %lu messages\n", g_total_produced);
    printf("  Consumed: %lu messages\n", g_total_consumed);
    printf("  %s\n",
           (g_total_produced == g_total_consumed) ?
           "Result: OK" : "Result: mismatch!");

    rte_ring_free(g_ring);
    rte_eal_cleanup();

    return 0;
}
```



### 编译和运行

```bash
# 生成构建目录并配置(三步骤)
cd build
cmake .. 
make
```

如果需要体验多生产者多消费者版本（MP/MC），可以在同一个构建目录中继续编译对应的示例程序：

```bash
./mp_mc_ring -l 0-6
```

> 所有示例程序的可执行文件都统一生成在 `build/bin` 目录下，便于管理与运行。

---

## 

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



### Q5:Bulk vs Burst操作的区别

- **Bulk操作**：必须完全成功（入队/出队N个）或完全失败（0个）
- **Burst操作**：尽可能多地入队/出队（0到N个）

```c
/* Bulk：要么成功10个，要么失败 */
ret = rte_ring_enqueue_bulk(ring, objs, 10, NULL);

/* Burst：可能返回0-10之间任意值 */
ret = rte_ring_enqueue_burst(ring, objs, 10, NULL);
```


