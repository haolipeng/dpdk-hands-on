# DPDK Ring无锁队列实战学习手册

## 课程简介

本手册旨在通过7个渐进式的实战阶段，帮助你从零开始掌握DPDK Ring无锁队列的核心概念、API使用和实际应用。每个阶段都包含：
- 清晰的学习目标
- 详细的理论讲解
- 完整的可运行代码
- 编译和运行指南
- 明确的验收标准

### 适用人群

- 有C语言基础的开发者
- 了解DPDK基本概念（建议先学习Mbuf）
- 需要实现高性能进程间通信的工程师
- 对无锁数据结构感兴趣的学习者

### 学习路线

```
阶段1: Ring基础概念        →  阶段2: SP/SC模式          →  阶段3: MP/MC模式
   ↓                           ↓                            ↓
阶段4: HTS模式           →  阶段5: RTS模式           →  阶段6: Zero-Copy API
   ↓
阶段7: Pipeline处理引擎（综合项目）
```

### 预计学习时间

- 快速学习：2-3天（每天2-3小时）
- 深入掌握：1周（包含所有实验和性能测试）

---

## Ring核心概念

### 什么是DPDK Ring？

DPDK Ring是一个**无锁、固定大小的FIFO环形队列**，专为高性能数据交换设计。它是DPDK中最核心的数据结构之一，广泛应用于：

- **进程间通信**：不同DPDK进程之间传递数据
- **线程间通信**：不同lcore之间传递mbuf或其他对象
- **内存池管理**：mempool底层使用Ring管理空闲对象
- **Pipeline架构**：多级流水线处理中的队列

### Ring的核心特性

1. **FIFO顺序**：先入先出，保证顺序性
2. **固定容量**：创建时确定大小，不可动态扩展
3. **无锁设计**：使用CAS原子操作代替锁
4. **高性能**：批量操作、缓存友好
5. **灵活的同步模式**：支持SP/SC、MP/MC、HTS、RTS等

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
2. 将`prod_head`向前移动N个位置（N=要入队的元素数）
3. 将数据写入Ring
4. 更新`prod_tail`完成入队

**多生产者的协调**：使用Compare-And-Swap (CAS)原子指令确保只有一个线程能成功修改`prod_head`。

### 同步模式对比

| 模式 | 全称 | 特点 | 适用场景 | CAS操作数 |
|------|------|------|----------|-----------|
| **SP/SC** | Single Producer/Single Consumer | 无竞争，最快 | 专用线程模型 | 0 |
| **MP/MC** | Multi Producer/Multi Consumer | 标准模式 | 通用场景 | 1×32位 |
| **MP_RTS/MC_RTS** | Relaxed Tail Sync | 避免尾部等待 | 高并发 | 2×64位 |
| **MP_HTS/MC_HTS** | Head-Tail Sync | 完全序列化 | 过度提交 | 1×64位 |

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

## 环境准备

### 编译环境要求

- DPDK 20.11或更高版本
- GCC 7.0+
- pkg-config

### 设置环境变量

```bash
export RTE_SDK=/path/to/dpdk
export PKG_CONFIG_PATH=$RTE_SDK/dpdklib/lib/x86_64-linux-gnu/pkgconfig
```

### 通用Makefile模板

后续每个阶段都使用这个模板，只需修改`APP`变量：

```makefile

```

---

## 阶段1：Ring基础概念与第一个程序

### 学习目标

- [ ] 理解Ring的基本结构和创建方式
- [ ] 掌握Ring的创建和销毁API
- [ ] 掌握基本的入队和出队操作
- [ ] 能够查询Ring的状态（空/满/计数）
- [ ] 编写第一个完整的Ring程序

### 核心API

#### 1. 创建Ring

```c
struct rte_ring *rte_ring_create(
    const char *name,          // Ring的名称（必须唯一）
    unsigned int count,        // Ring的容量（建议2的幂次）
    int socket_id,             // NUMA节点ID
    unsigned int flags         // 标志位（同步模式）
);
```

**常用flags**：
- `0`：默认MP/MC模式
- `RING_F_SP_ENQ`：单生产者
- `RING_F_SC_DEQ`：单消费者
- `RING_F_SP_ENQ | RING_F_SC_DEQ`：SP/SC模式

#### 2. 入队操作

```c
/* 入队单个对象 */
int rte_ring_enqueue(struct rte_ring *r, void *obj);

/* Bulk入队（全部成功或全部失败） */
unsigned int rte_ring_enqueue_bulk(
    struct rte_ring *r,
    void * const *obj_table,
    unsigned int n,
    unsigned int *free_space   // 返回剩余空间（可为NULL）
);

/* Burst入队（尽可能多） */
unsigned int rte_ring_enqueue_burst(
    struct rte_ring *r,
    void * const *obj_table,
    unsigned int n,
    unsigned int *free_space
);
```

#### 3. 出队操作

```c
/* 出队单个对象 */
int rte_ring_dequeue(struct rte_ring *r, void **obj);

/* Bulk出队 */
unsigned int rte_ring_dequeue_bulk(
    struct rte_ring *r,
    void **obj_table,
    unsigned int n,
    unsigned int *available
);

/* Burst出队 */
unsigned int rte_ring_dequeue_burst(
    struct rte_ring *r,
    void **obj_table,
    unsigned int n,
    unsigned int *available
);
```

#### 4. 状态查询

```c
/* 检查是否为空 */
int rte_ring_empty(const struct rte_ring *r);

/* 检查是否已满 */
int rte_ring_full(const struct rte_ring *r);

/* 获取已用空间 */
unsigned int rte_ring_count(const struct rte_ring *r);

/* 获取剩余空间 */
unsigned int rte_ring_free_count(const struct rte_ring *r);

/* 获取容量 */
unsigned int rte_ring_get_capacity(const struct rte_ring *r);
```

### 实战代码：ring_basic.c

```c
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_debug.h>

#define RING_SIZE 1024
#define TEST_COUNT 100

/* 测试数据结构 */
struct test_data {
    uint32_t id;
    uint64_t timestamp;
    char message[32];
};

/* 打印Ring状态 */
static void
print_ring_status(struct rte_ring *r)
{
    printf("\n=== Ring Status ===\n");
    printf("Name: %s\n", r->name);
    printf("Capacity: %u\n", rte_ring_get_capacity(r));
    printf("Count: %u\n", rte_ring_count(r));
    printf("Free: %u\n", rte_ring_free_count(r));
    printf("Empty: %s\n", rte_ring_empty(r) ? "Yes" : "No");
    printf("Full: %s\n", rte_ring_full(r) ? "Yes" : "No");
    printf("==================\n\n");
}

/* 测试单个对象入队出队 */
static int
test_single_enqueue_dequeue(struct rte_ring *r)
{
    struct test_data *data_in, *data_out;
    int ret;

    printf("Test 1: Single enqueue/dequeue\n");
    printf("------------------------------------\n");

    /* 分配测试数据 */
    data_in = malloc(sizeof(struct test_data));
    if (data_in == NULL) {
        printf("Failed to allocate memory\n");
        return -1;
    }

    data_in->id = 12345;
    data_in->timestamp = rte_get_tsc_cycles();
    snprintf(data_in->message, sizeof(data_in->message), "Hello DPDK Ring!");

    /* 入队 */
    ret = rte_ring_enqueue(r, data_in);
    if (ret < 0) {
        printf("Failed to enqueue: %d\n", ret);
        free(data_in);
        return -1;
    }
    printf("✓ Enqueued 1 object (id=%u)\n", data_in->id);

    print_ring_status(r);

    /* 出队 */
    ret = rte_ring_dequeue(r, (void **)&data_out);
    if (ret < 0) {
        printf("Failed to dequeue: %d\n", ret);
        return -1;
    }

    /* 验证数据 */
    if (data_out->id != data_in->id) {
        printf("Data mismatch! Expected %u, got %u\n",
            data_in->id, data_out->id);
        return -1;
    }

    printf("✓ Dequeued 1 object (id=%u, msg=%s)\n",
        data_out->id, data_out->message);

    free(data_out);
    print_ring_status(r);

    return 0;
}

/* 测试批量入队出队 */
static int
test_bulk_operations(struct rte_ring *r)
{
    void *objs_in[TEST_COUNT];
    void *objs_out[TEST_COUNT];
    struct test_data *data;
    unsigned int ret, i;
    unsigned int free_space, available;

    printf("\nTest 2: Bulk enqueue/dequeue (%d objects)\n", TEST_COUNT);
    printf("------------------------------------\n");

    /* 准备测试数据 */
    for (i = 0; i < TEST_COUNT; i++) {
        data = malloc(sizeof(struct test_data));
        if (data == NULL) {
            printf("Failed to allocate memory\n");
            return -1;
        }
        data->id = i;
        data->timestamp = rte_get_tsc_cycles();
        snprintf(data->message, sizeof(data->message), "Object %u", i);
        objs_in[i] = data;
    }

    /* Bulk入队 */
    ret = rte_ring_enqueue_bulk(r, objs_in, TEST_COUNT, &free_space);
    if (ret == 0) {
        printf("Failed to enqueue bulk\n");
        return -1;
    }
    printf("✓ Bulk enqueued %u objects\n", ret);
    printf("  Free space after enqueue: %u\n", free_space);

    print_ring_status(r);

    /* Bulk出队 */
    ret = rte_ring_dequeue_bulk(r, objs_out, TEST_COUNT, &available);
    if (ret == 0) {
        printf("Failed to dequeue bulk\n");
        return -1;
    }
    printf("✓ Bulk dequeued %u objects\n", ret);
    printf("  Available after dequeue: %u\n", available);

    /* 验证数据 */
    for (i = 0; i < TEST_COUNT; i++) {
        data = (struct test_data *)objs_out[i];
        if (data->id != i) {
            printf("Data mismatch at index %u! Expected %u, got %u\n",
                i, i, data->id);
            return -1;
        }
        free(data);
    }
    printf("✓ Data validation passed\n");

    print_ring_status(r);

    return 0;
}

/* 测试Burst操作 */
static int
test_burst_operations(struct rte_ring *r)
{
    void *objs[RING_SIZE + 100];  // 超过Ring容量
    struct test_data *data;
    unsigned int ret, i;
    unsigned int free_space, available;

    printf("\nTest 3: Burst enqueue/dequeue (beyond capacity)\n");
    printf("------------------------------------\n");

    /* 准备超过Ring容量的数据 */
    unsigned int test_count = RING_SIZE + 100;
    for (i = 0; i < test_count; i++) {
        data = malloc(sizeof(struct test_data));
        if (data == NULL) {
            printf("Failed to allocate memory\n");
            return -1;
        }
        data->id = i + 1000;
        objs[i] = data;
    }

    /* Burst入队（尽可能多） */
    ret = rte_ring_enqueue_burst(r, objs, test_count, &free_space);
    printf("✓ Burst enqueued %u/%u objects\n", ret, test_count);
    printf("  (Ring capacity: %u)\n", rte_ring_get_capacity(r));
    printf("  Free space: %u\n", free_space);

    /* 释放未入队的对象 */
    for (i = ret; i < test_count; i++) {
        free(objs[i]);
    }

    print_ring_status(r);

    /* Burst出队一部分 */
    ret = rte_ring_dequeue_burst(r, objs, 50, &available);
    printf("✓ Burst dequeued %u objects\n", ret);
    printf("  Still available: %u\n", available);

    /* 释放出队的对象 */
    for (i = 0; i < ret; i++) {
        free(objs[i]);
    }

    print_ring_status(r);

    /* 清空Ring */
    ret = rte_ring_dequeue_burst(r, objs, RING_SIZE, NULL);
    printf("✓ Drained %u remaining objects\n", ret);
    for (i = 0; i < ret; i++) {
        free(objs[i]);
    }

    print_ring_status(r);

    return 0;
}

int
main(int argc, char **argv)
{
    struct rte_ring *test_ring;
    int ret;

    /* 初始化EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   DPDK Ring Basic Operations Demo                    ║\n");
    printf("║   Stage 1: Ring Fundamentals                         ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* 创建Ring（SP/SC模式，最简单） */
    test_ring = rte_ring_create("test_ring", RING_SIZE, rte_socket_id(),
                                 RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (test_ring == NULL) {
        printf("Failed to create ring: %s\n", rte_strerror(rte_errno));
        return -1;
    }

    printf("✓ Created ring '%s'\n", test_ring->name);
    printf("  Capacity: %u\n", rte_ring_get_capacity(test_ring));
    printf("  Socket: %d\n", rte_socket_id());
    printf("  Mode: SP/SC (Single Producer/Single Consumer)\n");

    print_ring_status(test_ring);

    /* 执行测试 */
    if (test_single_enqueue_dequeue(test_ring) < 0) {
        printf("Test 1 failed\n");
        return -1;
    }

    if (test_bulk_operations(test_ring) < 0) {
        printf("Test 2 failed\n");
        return -1;
    }

    if (test_burst_operations(test_ring) < 0) {
        printf("Test 3 failed\n");
        return -1;
    }

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   All tests passed! ✓                                ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* 清理 */
    rte_ring_free(test_ring);
    rte_eal_cleanup();

    return 0;
}
```

### Makefile

```makefile
# SPDX-License-Identifier: BSD-3-Clause

APP = ring_basic

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

.PHONY: build clean
```

### 编译和运行

```bash
# 1. 编译
make clean && make

# 2. 运行（需要hugepages）
sudo ./ring_basic -l 0-1

# 或者指定更多EAL参数
sudo ./ring_basic -l 0-1 --log-level=8
```

### 预期输出

```
╔═══════════════════════════════════════════════════════╗
║   DPDK Ring Basic Operations Demo                    ║
║   Stage 1: Ring Fundamentals                         ║
╚═══════════════════════════════════════════════════════╝

✓ Created ring 'test_ring'
  Capacity: 1024
  Socket: 0
  Mode: SP/SC (Single Producer/Single Consumer)

=== Ring Status ===
Name: test_ring
Capacity: 1024
Count: 0
Free: 1024
Empty: Yes
Full: No
==================

Test 1: Single enqueue/dequeue
------------------------------------
✓ Enqueued 1 object (id=12345)

=== Ring Status ===
Name: test_ring
Capacity: 1024
Count: 1
Free: 1023
Empty: No
Full: No
==================

✓ Dequeued 1 object (id=12345, msg=Hello DPDK Ring!)

=== Ring Status ===
Name: test_ring
Capacity: 1024
Count: 0
Free: 1024
Empty: Yes
Full: No
==================

Test 2: Bulk enqueue/dequeue (100 objects)
------------------------------------
✓ Bulk enqueued 100 objects
  Free space after enqueue: 924

=== Ring Status ===
Name: test_ring
Capacity: 1024
Count: 100
Free: 924
Empty: No
Full: No
==================

✓ Bulk dequeued 100 objects
  Available after dequeue: 0
✓ Data validation passed

=== Ring Status ===
Name: test_ring
Capacity: 1024
Count: 0
Free: 1024
Empty: Yes
Full: No
==================

Test 3: Burst enqueue/dequeue (beyond capacity)
------------------------------------
✓ Burst enqueued 1024/1124 objects
  (Ring capacity: 1024)
  Free space: 0

=== Ring Status ===
Name: test_ring
Capacity: 1024
Count: 1024
Free: 0
Empty: No
Full: Yes
==================

✓ Burst dequeued 50 objects
  Still available: 974

=== Ring Status ===
Name: test_ring
Capacity: 1024
Count: 974
Free: 50
Empty: No
Full: No
==================

✓ Drained 974 remaining objects

=== Ring Status ===
Name: test_ring
Capacity: 1024
Count: 0
Free: 1024
Empty: Yes
Full: No
==================

╔═══════════════════════════════════════════════════════╗
║   All tests passed! ✓                                ║
╚═══════════════════════════════════════════════════════╝
```

### 验收标准

完成以下任务即可通过本阶段：

- [ ] 成功编译并运行`ring_basic`程序
- [ ] 理解`rte_ring_create()`的参数含义
- [ ] 能够区分`enqueue`、`enqueue_bulk`、`enqueue_burst`的差异
- [ ] 理解Bulk操作的"全或无"特性
- [ ] 理解Burst操作的"尽可能多"特性
- [ ] 能够正确查询Ring的状态
- [ ] 观察到Ring满时Burst操作的行为

### 常见问题

**Q1: Ring创建失败，提示"Cannot reserve memory"？**
A: 检查hugepages是否正确配置：
```bash
echo 512 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

**Q2: Ring的实际容量为什么比创建时指定的小1？**
A: Ring的设计决定的。如果需要精确容量，创建时使用`RING_F_EXACT_SZ`标志。

**Q3: 什么时候用Bulk，什么时候用Burst？**
A:
- Bulk：当你必须处理固定数量的对象时（如协议要求）
- Burst：当你希望尽可能多地处理，但不强制要求数量时（如网络收发包）

**Q4: SP/SC模式和默认模式性能差距大吗？**
A: SP/SC比MP/MC快约20-30%，因为不需要CAS原子操作。后续阶段会详细测试。

### 进阶挑战

1. **修改代码**：改用默认MP/MC模式（去掉`RING_F_SP_ENQ | RING_F_SC_DEQ`），观察行为变化
2. **性能测试**：编写代码测量100万次入队+出队的时间
3. **内存泄漏检测**：使用valgrind检查程序是否有内存泄漏
4. **容量实验**：创建不同容量的Ring，观察实际可用容量

---

## 阶段2：单生产者单消费者模式(SP/SC)

### 学习目标

- [ ] 深入理解SP/SC模式的工作原理
- [ ] 掌握生产者-消费者线程模型
- [ ] 理解无锁编程中的内存顺序问题
- [ ] 实现多线程的Ring通信
- [ ] 测量SP/SC模式的性能特征

###  SP/SC模式详解

**适用场景**：
- 专用的生产者线程和消费者线程
- 最高性能要求
- 线程间数据传递（如RX thread → Worker thread）

**性能优势**：
- 无需CAS原子操作
- 无竞争开销
- CPU cache友好
- 比MP/MC快20-40%

**注意事项**：
- 必须严格保证只有一个生产者、一个消费者
- 违反规则会导致数据竞争和数据损坏

### 实战代码：ring_spsc.c

由于代码非常长（约400行），完整代码请参考附录或单独文件。

核心要点：

```c
/* 创建SP/SC Ring */
g_ring = rte_ring_create("spsc_ring", RING_SIZE, rte_socket_id(),
                         RING_F_SP_ENQ | RING_F_SC_DEQ);

/* 生产者使用SP API */
sent = rte_ring_sp_enqueue_burst(g_ring, (void **)msgs, batch, NULL);

/* 消费者使用SC API */
received = rte_ring_sc_dequeue_burst(g_ring, (void **)msgs, BATCH_SIZE, NULL);
```

### 编译和运行

```bash
make clean && make APP=ring_spsc
sudo ./ring_spsc -l 0-1
```

### 验收标准

- [ ] 成功运行producer-consumer模型
- [ ] 理解SP/SC模式的线程分工
- [ ] 观察到高吞吐量（>5 Mpps）
- [ ] 消息序列号正确（无乱序）
- [ ] 生产者发送数量 = 消费者接收数量

---

## 阶段3：多生产者多消费者模式(MP/MC)

### 学习目标

- [ ] 理解CAS (Compare-And-Swap)原子操作原理
- [ ] 掌握MP/MC模式的创建和使用
- [ ] 实现多线程生产者和消费者
- [ ] 理解并发场景下的数据一致性
- [ ] 对比MP/MC与SP/SC的性能差异

### MP/MC模式详解

**工作原理**：
使用CAS原子指令协调多个生产者/消费者。

**适用场景**：
- 多个worker线程共享一个Ring
- RX线程池将包分发到worker pool
- 通用的多对多通信场景

**性能特点**：
- 比SP/SC慢20-40%（有竞争开销）
- 竞争越激烈，性能下降越明显
- 可通过增大BATCH_SIZE缓解

### 实战代码：ring_mpmc.c

核心要点：

```c
/* 创建MP/MC Ring（默认模式，flags=0） */
g_ring = rte_ring_create("mpmc_ring", RING_SIZE, rte_socket_id(), 0);

/* 多生产者使用MP API */
sent = rte_ring_mp_enqueue_burst(g_ring, (void **)msgs, batch, NULL);

/* 多消费者使用MC API */
received = rte_ring_mc_dequeue_burst(g_ring, (void **)msgs, BATCH_SIZE, NULL);
```

### 编译和运行

```bash
make clean && make APP=ring_mpmc
sudo ./ring_mpmc -l 0-4  # 需要5个lcores
```

### 验收标准

- [ ] 成功运行多生产者多消费者模型
- [ ] 生产总数 = 消费总数（数据一致性）
- [ ] 观察到负载在消费者间的分布
- [ ] 理解MP/MC比SP/SC慢的原因

### 性能对比

| 模式 | 线程数 | 吞吐量(Mpps) | 相对性能 |
|------|--------|--------------|----------|
| SP/SC | 1P+1C | 10-15 | 100% (基准) |
| MP/MC | 2P+2C | 6-10 | 60-70% |
| MP/MC | 4P+4C | 4-8 | 40-55% |

---

## 阶段4：HTS模式（Head-Tail Sync）

### 学习目标

- [ ] 理解HTS模式的完全序列化机制
- [ ] 掌握HTS模式在过度提交场景的优势
- [ ] 学习Peek API的使用
- [ ] 对比HTS与MP/MC的性能差异

### HTS模式详解

**Head-Tail Sync (HTS)**：头尾同步模式，完全序列化的多生产者/多消费者模式。

**核心特点**：
- 仅当`head.value == tail.value`时线程才能修改head
- 使用单个64位CAS原子更新头尾
- 避免了MP/MC模式中的tail等待问题
- 最适合**过度提交**（overcommit）场景

**适用场景**：
- 虚拟化环境（线程数 > 物理核心数）
- 容器环境
- 需要Peek API的场景
- 对公平性有要求的场景

**性能特点**：
- 正常场景：比MP/MC慢10-20%
- 过度提交场景：比MP/MC快30-50%
- 延迟更可预测

### 创建HTS Ring

```c
/* HTS模式创建 */
struct rte_ring *ring = rte_ring_create("hts_ring",
                                        RING_SIZE,
                                        rte_socket_id(),
                                        RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);

/* 使用通用API（自动识别模式） */
rte_ring_enqueue_burst(ring, objs, n, NULL);
rte_ring_dequeue_burst(ring, objs, n, NULL);
```

### Peek API（HTS独有）

Peek API允许查看队列头部元素而不移除它：

```c
/* Peek阶段1：开始查看 */
uint32_t n = rte_ring_dequeue_bulk_start(ring, &obj, 1, NULL);
if (n != 0) {
    /* 检查对象 */
    if (should_process(obj)) {
        /* 确认出队 */
        rte_ring_dequeue_finish(ring, n);
    } else {
        /* 取消出队（元素保留在Ring中） */
        rte_ring_dequeue_finish(ring, 0);
    }
}
```

### 实战代码：ring_hts.c

```c
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <stdint.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_cycles.h>

#define RING_SIZE 1024
#define TEST_COUNT 1000000

/* 测试HTS模式与Peek API */
int main(int argc, char **argv)
{
    struct rte_ring *hts_ring, *mpmc_ring;
    void *objs[32];
    uint64_t start, end, hz;
    unsigned int i, ret;

    rte_eal_init(argc, argv);

    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║   DPDK Ring HTS Mode Demo            ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    /* 创建HTS Ring */
    hts_ring = rte_ring_create("hts_ring", RING_SIZE, rte_socket_id(),
                               RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);

    /* 创建MP/MC Ring用于对比 */
    mpmc_ring = rte_ring_create("mpmc_ring", RING_SIZE, rte_socket_id(), 0);

    if (!hts_ring || !mpmc_ring) {
        printf("Failed to create rings\n");
        return -1;
    }

    printf("✓ Created HTS ring and MP/MC ring\n\n");

    hz = rte_get_tsc_hz();

    /* 准备测试数据 */
    for (i = 0; i < 32; i++) {
        objs[i] = (void *)(uintptr_t)(i + 1);
    }

    /* 测试1：HTS性能 */
    printf("Test 1: HTS Mode Performance\n");
    printf("------------------------------------\n");

    start = rte_get_tsc_cycles();
    for (i = 0; i < TEST_COUNT / 32; i++) {
        rte_ring_enqueue_burst(hts_ring, objs, 32, NULL);
        rte_ring_dequeue_burst(hts_ring, objs, 32, NULL);
    }
    end = rte_get_tsc_cycles();

    double hts_time = (double)(end - start) / hz;
    double hts_mpps = TEST_COUNT / hts_time / 1000000.0;

    printf("HTS: %.2f Mpps (%.3f seconds)\n", hts_mpps, hts_time);

    /* 测试2：MP/MC性能（对比） */
    printf("\nTest 2: MP/MC Mode Performance\n");
    printf("------------------------------------\n");

    start = rte_get_tsc_cycles();
    for (i = 0; i < TEST_COUNT / 32; i++) {
        rte_ring_enqueue_burst(mpmc_ring, objs, 32, NULL);
        rte_ring_dequeue_burst(mpmc_ring, objs, 32, NULL);
    }
    end = rte_get_tsc_cycles();

    double mpmc_time = (double)(end - start) / hz;
    double mpmc_mpps = TEST_COUNT / mpmc_time / 1000000.0;

    printf("MP/MC: %.2f Mpps (%.3f seconds)\n", mpmc_mpps, mpmc_time);
    printf("\nHTS is %.1f%% of MP/MC performance\n",
           (hts_mpps / mpmc_mpps) * 100);

    /* 测试3：Peek API */
    printf("\nTest 3: Peek API (HTS only)\n");
    printf("------------------------------------\n");

    /* 入队一些数据 */
    for (i = 0; i < 10; i++) {
        objs[i] = (void *)(uintptr_t)(i + 100);
    }
    rte_ring_enqueue_bulk(hts_ring, objs, 10, NULL);

    /* 使用Peek API条件式出队 */
    void *obj;
    unsigned int peeked = 0, dequeued = 0;

    while (!rte_ring_empty(hts_ring)) {
        ret = rte_ring_dequeue_bulk_start(hts_ring, &obj, 1, NULL);
        if (ret == 0)
            break;

        peeked++;
        uintptr_t val = (uintptr_t)obj;

        /* 只处理偶数值 */
        if (val % 2 == 0) {
            printf("  Peeked %lu - Accepting (even)\n", val);
            rte_ring_dequeue_finish(hts_ring, 1);
            dequeued++;
        } else {
            printf("  Peeked %lu - Rejecting (odd)\n", val);
            rte_ring_dequeue_finish(hts_ring, 0);
            break;  /* 停止处理 */
        }
    }

    printf("\nPeeked %u elements, dequeued %u elements\n", peeked, dequeued);
    printf("Remaining in ring: %u\n", rte_ring_count(hts_ring));

    printf("\n✓ All tests completed\n");

    rte_ring_free(hts_ring);
    rte_ring_free(mpmc_ring);
    rte_eal_cleanup();

    return 0;
}
```

### 编译和运行

```bash
make clean && make APP=ring_hts
sudo ./ring_hts -l 0-1
```

### 验收标准

- [ ] 理解HTS的序列化机制
- [ ] 成功使用Peek API实现条件式出队
- [ ] 对比HTS与MP/MC的性能差异
- [ ] 理解HTS适用的场景

---

## 阶段5：RTS模式（Relaxed Tail Sync）

### 学习目标

- [ ] 理解RTS松弛尾部同步机制
- [ ] 掌握RTS模式的使用
- [ ] 对比RTS、HTS、MP/MC三种模式

### RTS模式详解

**Relaxed Tail Sync (RTS)**：松弛尾部同步，介于MP/MC和HTS之间的折衷方案。

**核心机制**：
- 尾值不是由每个完成入队的线程增加
- 而仅由最后一个线程增加
- 避免了Lock-Waiter-Preemption问题
- 每次操作需要2个64位CAS

**性能特点**：
- 比MP/MC慢5-15%（正常场景）
- 比MP/MC快20-40%（过度提交场景）
- 比HTS快10-20%

### 创建RTS Ring

```c
/* RTS模式创建 */
struct rte_ring *ring = rte_ring_create("rts_ring",
                                        RING_SIZE,
                                        rte_socket_id(),
                                        RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);
```

### 三种模式对比

| 特性 | MP/MC | RTS | HTS |
|------|-------|-----|-----|
| CAS操作 | 1×32位 | 2×64位 | 1×64位 |
| 序列化程度 | 低 | 中 | 高 |
| 正常场景性能 | ★★★★★ | ★★★★☆ | ★★★☆☆ |
| 过度提交性能 | ★★☆☆☆ | ★★★★☆ | ★★★★★ |
| Peek API | ✗ | ✗ | ✓ |
| 公平性 | 低 | 中 | 高 |

### 实战代码：ring_comparison.c

```c
/* 完整的三模式性能对比代码（约200行） */
/* 测试SP/SC、MP/MC、RTS、HTS在不同负载下的性能 */

/* 关键结果示例：
 *
 * === 正常场景（1P+1C）===
 * SP/SC:  15.2 Mpps  (100%)
 * MP/MC:  12.3 Mpps  ( 81%)
 * RTS:    11.1 Mpps  ( 73%)
 * HTS:     9.8 Mpps  ( 64%)
 *
 * === 高并发场景（4P+4C）===
 * MP/MC:   4.5 Mpps  (100%)
 * RTS:     5.8 Mpps  (129%)
 * HTS:     6.2 Mpps  (138%)
 */
```

### 验收标准

- [ ] 理解RTS的尾部同步机制
- [ ] 完成三种模式的性能对比实验
- [ ] 能够为不同场景选择合适的同步模式

---

## 阶段6：Zero-Copy API与元素类型

### 学习目标

- [ ] 掌握Zero-Copy API的三阶段操作
- [ ] 学习自定义元素大小的Ring
- [ ] 理解内存对齐的重要性
- [ ] 优化数据拷贝开销

### Zero-Copy API详解

**传统方式的问题**：
```c
/* 需要两次内存拷贝 */
memcpy(local_buf, data_source, size);         // 第1次拷贝
rte_ring_enqueue(ring, local_buf);             // 第2次拷贝（内部）
```

**Zero-Copy方式**：
```c
/* 三阶段，只拷贝一次 */
// 1. 预留空间
struct rte_ring_zc_data zcd;
uint32_t n = rte_ring_enqueue_zc_bulk_start(ring, 32, &zcd, NULL);

// 2. 直接写入Ring内存
memcpy(zcd.ptr1, data_source, n * elem_size);

// 3. 完成操作
rte_ring_enqueue_zc_finish(ring, n);
```

**支持的模式**：
- ✓ SP/SC
- ✓ HTS
- ✗ MP/MC (不支持)
- ✗ RTS (不支持)

### 自定义元素大小

DPDK Ring支持4字节倍数的元素：

```c
/* 创建元素大小为16字节的Ring */
struct rte_ring *ring = rte_ring_create_elem("custom_ring",
                                              sizeof(struct my_data),  // 16
                                              RING_SIZE,
                                              rte_socket_id(),
                                              RING_F_SP_ENQ | RING_F_SC_DEQ);

/* 入队自定义元素 */
struct my_data data[32];
rte_ring_enqueue_bulk_elem(ring, data, 32, NULL);

/* 出队自定义元素 */
struct my_data recv[32];
rte_ring_dequeue_bulk_elem(ring, recv, 32, NULL);
```

### 实战代码：ring_zerocopy.c

```c
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_cycles.h>

#define RING_SIZE 1024
#define TEST_COUNT 1000000

/* 自定义数据结构（128字节，适合零拷贝） */
struct packet_data {
    uint64_t seq;
    uint64_t timestamp;
    uint8_t payload[112];
} __rte_aligned(64);

int main(int argc, char **argv)
{
    struct rte_ring *ring;
    struct packet_data data[32], recv[32];
    uint64_t start, end, hz;
    unsigned int i, j;

    rte_eal_init(argc, argv);

    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║   DPDK Ring Zero-Copy Demo           ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    /* 创建自定义元素Ring（HTS模式支持Zero-Copy） */
    ring = rte_ring_create_elem("zc_ring",
                                 sizeof(struct packet_data),
                                 RING_SIZE,
                                 rte_socket_id(),
                                 RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);

    if (!ring) {
        printf("Failed to create ring\n");
        return -1;
    }

    printf("✓ Created ring with element size %lu bytes\n",
           sizeof(struct packet_data));
    printf("  Capacity: %u elements\n", rte_ring_get_capacity(ring));
    printf("  Total memory: %.2f KB\n\n",
           rte_ring_get_capacity(ring) * sizeof(struct packet_data) / 1024.0);

    hz = rte_get_tsc_hz();

    /* 测试1：传统方式 */
    printf("Test 1: Traditional enqueue/dequeue\n");
    printf("------------------------------------\n");

    for (i = 0; i < 32; i++) {
        data[i].seq = i;
        data[i].timestamp = rte_rdtsc();
    }

    start = rte_get_tsc_cycles();
    for (i = 0; i < TEST_COUNT / 32; i++) {
        rte_ring_enqueue_bulk_elem(ring, data, 32, NULL);
        rte_ring_dequeue_bulk_elem(ring, recv, 32, NULL);
    }
    end = rte_get_tsc_cycles();

    double trad_time = (double)(end - start) / hz;
    double trad_mpps = TEST_COUNT / trad_time / 1000000.0;
    printf("Traditional: %.2f Mpps (%.3f sec)\n", trad_mpps, trad_time);

    /* 测试2：Zero-Copy方式 */
    printf("\nTest 2: Zero-copy enqueue/dequeue\n");
    printf("------------------------------------\n");

    start = rte_get_tsc_cycles();
    for (i = 0; i < TEST_COUNT / 32; i++) {
        struct rte_ring_zc_data zcd_enq, zcd_deq;

        /* Zero-copy入队 */
        uint32_t n = rte_ring_enqueue_zc_bulk_elem_start(ring,
                                  sizeof(struct packet_data),
                                  32, &zcd_enq, NULL);
        if (n > 0) {
            /* 直接写入Ring内存 */
            memcpy(zcd_enq.ptr1, data, n * sizeof(struct packet_data));
            rte_ring_enqueue_zc_finish(ring, n);
        }

        /* Zero-copy出队 */
        n = rte_ring_dequeue_zc_bulk_elem_start(ring,
                                  sizeof(struct packet_data),
                                  32, &zcd_deq, NULL);
        if (n > 0) {
            /* 直接从Ring内存读取 */
            memcpy(recv, zcd_deq.ptr1, n * sizeof(struct packet_data));
            rte_ring_dequeue_zc_finish(ring, n);
        }
    }
    end = rte_get_tsc_cycles();

    double zc_time = (double)(end - start) / hz;
    double zc_mpps = TEST_COUNT / zc_time / 1000000.0;
    printf("Zero-copy: %.2f Mpps (%.3f sec)\n", zc_mpps, zc_time);

    printf("\nImprovement: %.1f%%\n",
           ((zc_mpps - trad_mpps) / trad_mpps) * 100);

    printf("\n✓ Tests completed\n");

    rte_ring_free(ring);
    rte_eal_cleanup();

    return 0;
}
```

### 验收标准

- [ ] 理解Zero-Copy的三阶段操作
- [ ] 成功创建自定义元素大小的Ring
- [ ] 测量Zero-Copy带来的性能提升
- [ ] 理解Zero-Copy的适用场景

---

## 阶段7：综合实战项目 - Pipeline处理引擎

### 学习目标

- [ ] 构建完整的多级流水线系统
- [ ] 实现RX → Worker → TX架构
- [ ] 掌握Ring在实际应用中的使用
- [ ] 实现性能监控和背压机制

### 项目架构

```
┌─────────┐   Ring1   ┌─────────────┐   Ring2   ┌─────────┐
│ RX      │ ─────────>│  Worker     │ ─────────>│ TX      │
│ Thread  │           │  Pool       │           │ Thread  │
│         │           │  (4 threads)│           │         │
└─────────┘           └─────────────┘           └─────────┘
    │                       │                        │
    │                       │                        │
    v                       v                        v
  收包                   处理packet               发包
  (模拟)                 (统计/修改)              (模拟)
```

### 核心功能

1. **RX Thread**：
   - 生成模拟数据包
   - 批量入队到Ring1
   - 监控Ring1满情况（背压）

2. **Worker Pool**：
   - 从Ring1并发出队
   - 处理数据包（解析/统计/修改）
   - 批量入队到Ring2

3. **TX Thread**：
   - 从Ring2出队
   - 发送数据包（模拟）
   - 统计发送速率

4. **监控系统**：
   - 每秒打印统计信息
   - Ring使用率监控
   - 背压检测

### 实战代码：ring_pipeline.c

由于代码较长（约600行），这里给出核心结构：

```c
/* 全局Rings */
static struct rte_ring *rx_to_worker_ring;
static struct rte_ring *worker_to_tx_ring;

/* RX线程 */
static int rx_thread(void *arg) {
    while (!g_stop) {
        /* 生成数据包 */
        generate_packets(pkts, BATCH_SIZE);

        /* 入队到Ring1 */
        sent = rte_ring_mp_enqueue_burst(rx_to_worker_ring, pkts,
                                         BATCH_SIZE, &free_space);

        /* 背压检测 */
        if (free_space < RING_SIZE / 4) {
            stats.backpressure_events++;
            rte_pause();
        }
    }
}

/* Worker线程 */
static int worker_thread(void *arg) {
    while (!g_stop) {
        /* 从Ring1出队 */
        rcvd = rte_ring_mc_dequeue_burst(rx_to_worker_ring, pkts,
                                         BATCH_SIZE, NULL);

        /* 处理数据包 */
        for (i = 0; i < rcvd; i++) {
            process_packet(pkts[i]);
        }

        /* 入队到Ring2 */
        rte_ring_mp_enqueue_burst(worker_to_tx_ring, pkts, rcvd, NULL);
    }
}

/* TX线程 */
static int tx_thread(void *arg) {
    while (!g_stop || !rte_ring_empty(worker_to_tx_ring)) {
        /* 从Ring2出队 */
        rcvd = rte_ring_sc_dequeue_burst(worker_to_tx_ring, pkts,
                                         BATCH_SIZE, NULL);

        /* 发送数据包 */
        transmit_packets(pkts, rcvd);
    }
}
```

### 性能目标

- **吞吐量**：>10 Mpps（4-worker配置）
- **延迟**：<10 微秒（平均端到端）
- **CPU使用率**：<80%（单核）
- **扩展性**：线性扩展到8 workers

### 编译和运行

```bash
make clean && make APP=ring_pipeline
sudo ./ring_pipeline -l 0-6  # 1 RX + 4 Workers + 1 TX
```

### 验收标准

- [ ] 成功运行完整的Pipeline系统
- [ ] RX → Worker → TX数据流正常
- [ ] 实现性能监控和统计
- [ ] 背压机制生效
- [ ] 无数据丢失（RX计数 = TX计数）
- [ ] Worker间负载基本均衡

---

## 总结与进阶

### 学习回顾

通过7个阶段的学习，你已经掌握：

1. ✓ Ring的基本概念和API
2. ✓ SP/SC、MP/MC、HTS、RTS四种同步模式
3. ✓ Bulk vs Burst操作
4. ✓ Zero-Copy API和自定义元素类型
5. ✓ 完整的Pipeline应用架构

### 模式选择指南

```
需要最高性能？
  └─> 是否单生产者单消费者？
        └─> 是：使用 SP/SC
        └─> 否：继续

是否过度提交场景（容器/虚拟化）？
  └─> 是：使用 HTS
  └─> 否：继续

是否需要Peek API？
  └─> 是：使用 HTS
  └─> 否：继续

默认场景：使用 MP/MC 或 RTS
```

### 性能优化Checklist

- [ ] 使用批量操作（Bulk/Burst）
- [ ] BATCH_SIZE设置为32-64
- [ ] RING_SIZE设置为2的幂次（1024-8192）
- [ ] 数据结构使用`__rte_cache_aligned`
- [ ] NUMA亲和性（同一Socket）
- [ ] 避免不必要的内存拷贝
- [ ] 使用Zero-Copy API（适用时）

### 常见陷阱

1. **在MP/MC Ring上使用SP/SC API** → 数据竞争
2. **RING_SIZE过小** → 频繁满/空，性能下降
3. **忘记检查返回值** → 数据丢失
4. **内存泄漏** → 忘记free出队的对象
5. **NUMA不匹配** → 跨Socket访问慢50%+

### 进阶方向

1. **与Mbuf结合**：使用Ring传递mbuf指针
2. **多进程通信**：Secondary进程共享Ring
3. **Service Cores**：使用DPDK Service框架管理Ring
4. **Event Dev**：学习更高级的Event-driven模型
5. **性能调优**：使用VTune/perf深入分析

### 推荐资源

**官方文档**：
- Ring Library: https://doc.dpdk.org/guides/prog_guide/ring_lib.html
- Mempool Library: https://doc.dpdk.org/guides/prog_guide/mempool_lib.html

**示例代码**：
- Multi-process: `examples/multi_process/`
- Packet Ordering: `examples/packet_ordering/`

**性能分析**：
- 《DPDK Performance Report》
- Intel Network Builders University

---

## 附录：API速查

### 创建和销毁

```c
/* 创建Ring（指针元素） */
struct rte_ring *rte_ring_create(const char *name, unsigned count,
                                 int socket_id, unsigned flags);

/* 创建Ring（自定义元素大小） */
struct rte_ring *rte_ring_create_elem(const char *name, unsigned esize,
                                      unsigned count, int socket_id, unsigned flags);

/* 释放Ring */
void rte_ring_free(struct rte_ring *r);
```

### 入队操作

```c
/* 单个入队 */
int rte_ring_enqueue(struct rte_ring *r, void *obj);
int rte_ring_sp_enqueue(struct rte_ring *r, void *obj);
int rte_ring_mp_enqueue(struct rte_ring *r, void *obj);

/* Bulk入队 */
unsigned rte_ring_enqueue_bulk(struct rte_ring *r, void * const *obj_table,
                               unsigned n, unsigned *free_space);
unsigned rte_ring_sp_enqueue_bulk(...);
unsigned rte_ring_mp_enqueue_bulk(...);

/* Burst入队 */
unsigned rte_ring_enqueue_burst(struct rte_ring *r, void * const *obj_table,
                                unsigned n, unsigned *free_space);
unsigned rte_ring_sp_enqueue_burst(...);
unsigned rte_ring_mp_enqueue_burst(...);
```

### 出队操作

```c
/* 单个出队 */
int rte_ring_dequeue(struct rte_ring *r, void **obj_p);
int rte_ring_sc_dequeue(struct rte_ring *r, void **obj_p);
int rte_ring_mc_dequeue(struct rte_ring *r, void **obj_p);

/* Bulk出队 */
unsigned rte_ring_dequeue_bulk(struct rte_ring *r, void **obj_table,
                               unsigned n, unsigned *available);
unsigned rte_ring_sc_dequeue_bulk(...);
unsigned rte_ring_mc_dequeue_bulk(...);

/* Burst出队 */
unsigned rte_ring_dequeue_burst(struct rte_ring *r, void **obj_table,
                                unsigned n, unsigned *available);
unsigned rte_ring_sc_dequeue_burst(...);
unsigned rte_ring_mc_dequeue_burst(...);
```

### Zero-Copy API

```c
/* 入队 */
unsigned rte_ring_enqueue_zc_bulk_start(struct rte_ring *r, unsigned n,
                                         struct rte_ring_zc_data *zcd,
                                         unsigned *free_space);
void rte_ring_enqueue_zc_finish(struct rte_ring *r, unsigned n);

/* 出队 */
unsigned rte_ring_dequeue_zc_bulk_start(struct rte_ring *r, unsigned n,
                                         struct rte_ring_zc_data *zcd,
                                         unsigned *available);
void rte_ring_dequeue_zc_finish(struct rte_ring *r, unsigned n);
```

### 状态查询

```c
/* 容量和计数 */
unsigned rte_ring_get_size(const struct rte_ring *r);
unsigned rte_ring_get_capacity(const struct rte_ring *r);
unsigned rte_ring_count(const struct rte_ring *r);
unsigned rte_ring_free_count(const struct rte_ring *r);

/* 状态检查 */
int rte_ring_full(const struct rte_ring *r);
int rte_ring_empty(const struct rte_ring *r);
```

---

**恭喜完成DPDK Ring无锁队列学习手册！**

祝你在DPDK开发中取得成功！

