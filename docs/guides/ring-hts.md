# DPDK Ring: HTS模式（Head-Tail Sync）

## 文档简介

HTS模式是DPDK Ring的一种同步方式，特别适合**虚拟机/容器环境**和需要**"先看再取"**的场景。

---

## HTS模式概述

### 什么是HTS？

**Head-Tail Sync (HTS)**：头尾同步模式，一种更严格的多生产者/多消费者模式。

**简单理解**：
- 同一时刻只允许一个线程操作Ring（像排队，一次只能一个人）
- 避免了线程之间复杂的协调问题
- 最适合虚拟机和容器环境

### 什么时候用HTS？

✅ **虚拟机/容器环境**（资源被调度器分配）
✅ **需要Peek API**（先看内容再决定是否取出）
✅ **线程数多于CPU核心数**（过度提交场景）
✅ **需要更稳定的延迟**

### 性能特点

| 场景 | HTS vs MP/MC |
|------|--------------|
| 物理机正常运行 | 慢10-20% |
| 虚拟机/容器 | **快30-50%** |
| 延迟稳定性 | **更好** |

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

**过度提交**：线程数 > CPU核心数。

**例子**：
- 8核CPU上运行16个线程
- 虚拟机/容器（资源被调度器动态分配）

### Q2: 为什么虚拟机/容器中HTS更快？

**原因**：虚拟机中线程可能被调度器随时暂停。

- **MP/MC**：如果一个线程被暂停了，其他线程可能要一直等它
- **HTS**：避免了这种等待，一次只有一个线程在操作

### Q3: Peek API的性能开销大吗？

开销不大，但要注意：
- 如果大部分消息都会取出，直接用`dequeue`更好
- 如果频繁"看了又不取"，会影响性能

### Q4: 所有Ring都支持Peek API吗？

**不是！** 只有两种模式支持：
- ✅ HTS模式
- ✅ SP/SC模式
- ❌ MP/MC模式（不支持）

### Q5: 什么时候用HTS？

**用HTS**：
- 运行在虚拟机/容器中
- 需要Peek API
- 线程数多于CPU核心数

**用MP/MC**：
- 运行在物理机上
- 线程数 ≤ CPU核心数
- 追求最高吞吐量

---

## 总结

### HTS模式特点

✅ **优点**：
- 虚拟机/容器环境性能更好
- 支持Peek API（先看再取）
- 延迟更稳定

⚠️ **缺点**：
- 物理机上比MP/MC慢10-20%
- 同一时刻只能一个线程操作

### 快速选择指南

**选HTS**：虚拟机/容器 或 需要Peek API
**选MP/MC**：物理机 + 追求高吞吐
**选SP/SC**：单生产者单消费者

---

## 参考资源

- [DPDK Ring Library 官方文档](https://doc.dpdk.org/guides/prog_guide/ring_lib.html)
- RTS模式（另一种同步方式）
- SP/SC和MP/MC模式

---

**学完本文档，你应该能够：**
- ✅ 知道什么时候用HTS模式
- ✅ 使用Peek API先看再取消息
- ✅ 在虚拟机/容器中优化性能
