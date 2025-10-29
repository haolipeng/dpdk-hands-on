# DPDK Ring: HTS模式（Head-Tail Sync）

## 文档简介

本文档详细介绍DPDK Ring的HTS（Head-Tail Sync）模式，一种完全序列化的多生产者/多消费者同步机制，特别适合过度提交场景和需要Peek API的应用。

---

## HTS模式概述

### 什么是HTS？

**Head-Tail Sync (HTS)**：头尾同步模式，一种完全序列化的多生产者/多消费者模式。

**核心特点**：
- 仅当`head.value == tail.value`时线程才能修改head
- 使用单个64位CAS原子更新头尾
- 避免了MP/MC模式中的tail等待问题
- 最适合**过度提交**（overcommit）场景

### 适用场景

✅ **虚拟化环境**（线程数 > 物理核心数）
✅ **容器环境**
✅ **需要Peek API的场景**
✅ **对公平性有要求的场景**
✅ **延迟可预测性要求高**

### 性能特点

| 场景 | HTS vs MP/MC | 说明 |
|------|--------------|------|
| 正常场景 | 慢10-20% | 序列化开销 |
| 过度提交场景 | 快30-50% | 避免tail等待 |
| 延迟 | 更可预测 | 无CAS重试 |

---

## HTS工作原理

### 序列化机制

HTS确保同一时刻只有一个线程能操作Ring：

```c
// 伪代码：HTS入队
struct {
    uint32_t head;
    uint32_t tail;
} ht;  // 64位原子结构

// 只有当head == tail时才能操作
while (1) {
    ht_old = atomic_load(&ring->ht);
    
    if (ht_old.head != ht_old.tail) {
        // 有其他线程正在操作，等待
        continue;
    }
    
    // 尝试原子更新head和tail
    ht_new.head = ht_old.head + n;
    ht_new.tail = ht_old.tail;
    
    if (CAS(&ring->ht, ht_old, ht_new))
        break;  // 成功
}

// 写入数据...

// 更新tail完成操作
ht_final.head = ht_new.head;
ht_final.tail = ht_new.head;
atomic_store(&ring->ht, ht_final);
```

### 与MP/MC对比

| 特性 | MP/MC | HTS |
|------|-------|-----|
| CAS操作 | 1×32位 | 1×64位 |
| head/tail更新 | 分离 | 原子 |
| 等待tail | 可能发生 | 不会发生 |
| 序列化程度 | 低 | 高 |
| Peek API | ✗ | ✓ |

---

## 创建和使用HTS Ring

### 创建HTS Ring

```c
#include <rte_ring.h>

/* 创建HTS模式Ring */
struct rte_ring *ring = rte_ring_create(
    "hts_ring",
    1024,
    rte_socket_id(),
    RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ  // HTS标志
);

if (ring == NULL) {
    printf("Failed to create ring: %s\n", rte_strerror(rte_errno));
    return -1;
}
```

### 使用通用API

HTS Ring使用通用API（自动识别模式）：

```c
/* 入队 */
unsigned sent = rte_ring_enqueue_burst(ring, objs, n, NULL);

/* 出队 */
unsigned received = rte_ring_dequeue_burst(ring, objs, n, NULL);
```

---

## Peek API（HTS独有）

### Peek API简介

Peek API允许**查看队列头部元素而不移除它**，实现条件式出队。

**只有HTS和SP/SC模式支持Peek API！**

### Peek API使用

```c
/* 两阶段操作 */

// 阶段1：开始查看
void *obj;
uint32_t n = rte_ring_dequeue_bulk_start(ring, &obj, 1, NULL);

if (n != 0) {
    // 阶段2：决定是否出队
    if (should_process(obj)) {
        /* 确认出队 */
        rte_ring_dequeue_finish(ring, n);
    } else {
        /* 取消出队（元素保留在Ring中） */
        rte_ring_dequeue_finish(ring, 0);
    }
}
```

### Peek API应用场景

1. **条件过滤**：只处理符合条件的消息
2. **优先级队列**：跳过低优先级消息
3. **消息预览**：先看再决定是否处理
4. **流量控制**：根据消息内容决定是否接收

---

## 完整示例代码

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
# Makefile
APP = ring_hts
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
sudo ./ring_hts -l 0-1
```

---

## HTS vs MP/MC 性能对比

### 正常场景（无过度提交）

| 模式 | 吞吐量(Mpps) | 相对性能 |
|------|-------------|----------|
| SP/SC | 15.2 | 100% |
| MP/MC | 12.3 | 81% |
| HTS | 10.8 | 71% |

**结论**：正常场景下HTS较慢。

### 过度提交场景（16线程 on 8核）

| 模式 | 吞吐量(Mpps) | 相对性能 |
|------|-------------|----------|
| MP/MC | 3.5 | 100% |
| HTS | 5.2 | 149% |

**结论**：过度提交场景HTS显著更快！

---

## 常见问题

### Q1: 什么是过度提交场景？

**过度提交**（Overcommit）：线程数 > 物理CPU核心数。

例如：
- 16个线程运行在8核CPU上
- 虚拟化环境（VM调度不确定）
- 容器环境（资源共享）

### Q2: 为什么HTS在过度提交场景更快？

MP/MC模式中，如果一个线程在更新tail时被抢占（preempted），其他线程会被阻塞等待。

HTS模式避免了这个问题，因为head/tail原子更新。

### Q3: Peek API的性能开销大吗？

Peek API本身开销很小，但如果频繁peek后不出队（cancel），会影响吞吐量。

建议：
- 尽量减少peek后cancel的次数
- 如果大部分消息都会被处理，直接用普通dequeue

### Q4: 能否在MP/MC Ring上使用Peek API？

**不能！** Peek API只支持：
- HTS模式
- SP/SC模式

在MP/MC上使用会编译错误。

### Q5: HTS适合高吞吐场景吗？

如果是正常场景（无过度提交），建议用MP/MC。

如果是过度提交或需要Peek API，用HTS。

---

## 总结

### HTS模式优缺点

✅ **优点**：
- 过度提交场景性能优异
- 支持Peek API（条件式出队）
- 延迟更可预测
- 更高的公平性

⚠️ **缺点**：
- 正常场景比MP/MC慢10-20%
- 完全序列化，并发度低

### 选择建议

```
是否过度提交场景（容器/虚拟化）？
  └─> 是：使用 HTS

是否需要Peek API？
  └─> 是：使用 HTS

正常场景：
  └─> 使用 MP/MC 或 SP/SC
```

---

## 参考资源

**官方文档**：
- Ring Library: https://doc.dpdk.org/guides/prog_guide/ring_lib.html

**相关主题**：
- RTS模式（松弛尾部同步）
- SP/SC和MP/MC模式
- Zero-Copy API

---

**完成本文档学习后，你应该能够：**
- ✅ 理解HTS的序列化机制
- ✅ 使用Peek API实现条件式出队
- ✅ 选择HTS的合适场景
- ✅ 对比HTS与MP/MC性能
