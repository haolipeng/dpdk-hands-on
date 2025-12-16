# DPDK Ring: RTS模式、Zero-Copy API与Pipeline实战

## 文档简介

本文档整合了DPDK Ring的三个进阶主题：
1. **RTS模式**（Relaxed Tail Sync）：松弛尾部同步
2. **Zero-Copy API**：零拷贝优化
3. **Pipeline实战**：完整的流水线处理引擎

---

# 第一部分：RTS模式（Relaxed Tail Sync）

## RTS模式概述

**Relaxed Tail Sync (RTS)**：松弛尾部同步，介于MP/MC和HTS之间的折衷方案。

**核心机制**：
- 尾值不是由每个完成入队的线程增加
- 而仅由最后一个线程增加
- 避免了Lock-Waiter-Preemption问题
- 每次操作需要2个64位CAS

### 性能特点

| 场景 | RTS vs MP/MC | 说明 |
|------|--------------|------|
| 正常场景 | 慢5-15% | 额外的CAS开销 |
| 过度提交场景 | 快20-40% | 避免tail等待 |
| vs HTS | 快10-20% | 更高并发度 |

### 创建RTS Ring

```c
/* RTS模式创建 */
struct rte_ring *ring = rte_ring_create("rts_ring",
                                        RING_SIZE,
                                        rte_socket_id(),
                                        RING_F_MP_RTS_ENQ | RING_F_MC_RTS_DEQ);

/* 使用通用API */
rte_ring_enqueue_burst(ring, objs, n, NULL);
rte_ring_dequeue_burst(ring, objs, n, NULL);
```

## 四种模式对比

| 特性 | SP/SC | MP/MC | RTS | HTS |
|------|-------|-------|-----|-----|
| CAS操作 | 0 | 1×32位 | 2×64位 | 1×64位 |
| 序列化程度 | N/A | 低 | 中 | 高 |
| 正常场景性能 | ★★★★★ | ★★★★☆ | ★★★☆☆ | ★★★☆☆ |
| 过度提交性能 | ★★★★★ | ★★☆☆☆ | ★★★★☆ | ★★★★★ |
| Peek API | ✓ | ✗ | ✗ | ✓ |
| 公平性 | 高 | 低 | 中 | 高 |
| 适用场景 | 1对1 | 通用 | 高并发 | 过度提交 |

## 模式选择决策树

```
需要最高性能？
  └─> 是否单生产者单消费者？
        └─> 是：使用 SP/SC
        └─> 否：继续

是否过度提交场景（容器/虚拟化）？
  └─> 是：使用 HTS 或 RTS
        └─> 需要Peek API？
              └─> 是：HTS
              └─> 否：RTS（更快）

是否需要Peek API？
  └─> 是：使用 HTS 或 SP/SC
  └─> 否：继续

默认场景：使用 MP/MC
```

---

# 第二部分：Zero-Copy API

## Zero-Copy简介

**传统方式的问题**：需要两次内存拷贝

```c
/* 传统方式 */
memcpy(local_buf, data_source, size);     // 第1次拷贝
rte_ring_enqueue(ring, local_buf);         // 第2次拷贝（内部）
```

**Zero-Copy方式**：三阶段操作，只拷贝一次

```c
/* Zero-Copy三阶段 */
// 1. 预留空间
struct rte_ring_zc_data zcd;
uint32_t n = rte_ring_enqueue_zc_bulk_start(ring, 32, &zcd, NULL);

// 2. 直接写入Ring内存
memcpy(zcd.ptr1, data_source, n * elem_size);

// 3. 完成操作
rte_ring_enqueue_zc_finish(ring, n);
```

## 支持的模式

| 模式 | Zero-Copy支持 |
|------|---------------|
| SP/SC | ✓ 支持 |
| MP/MC | ✗ 不支持 |
| RTS | ✗ 不支持 |
| HTS | ✓ 支持 |

## 自定义元素大小

DPDK Ring支持4字节倍数的元素：

```c
/* 创建自定义元素Ring */
struct rte_ring *ring = rte_ring_create_elem("custom_ring",
                                              sizeof(struct my_data),  // 如16字节
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

## Zero-Copy完整示例

```c
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_cycles.h>

#define RING_SIZE 1024
#define TEST_COUNT 1000000

/* 自定义数据结构（128字节） */
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
    unsigned int i;

    rte_eal_init(argc, argv);

    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║   DPDK Ring Zero-Copy Demo           ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    /* 创建HTS模式Ring（支持Zero-Copy） */
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

    hz = rte_get_tsc_hz();

    /* 测试1：传统方式 */
    printf("\nTest 1: Traditional enqueue/dequeue\n");
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

    start = rte_get_tsc_cycles();
    for (i = 0; i < TEST_COUNT / 32; i++) {
        struct rte_ring_zc_data zcd_enq, zcd_deq;

        /* Zero-copy入队 */
        uint32_t n = rte_ring_enqueue_zc_bulk_elem_start(ring,
                                  sizeof(struct packet_data),
                                  32, &zcd_enq, NULL);
        if (n > 0) {
            memcpy(zcd_enq.ptr1, data, n * sizeof(struct packet_data));
            rte_ring_enqueue_zc_finish(ring, n);
        }

        /* Zero-copy出队 */
        n = rte_ring_dequeue_zc_bulk_elem_start(ring,
                                  sizeof(struct packet_data),
                                  32, &zcd_deq, NULL);
        if (n > 0) {
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

### 预期性能提升

- **小数据包（<128B）**：提升10-20%
- **中等数据包（128-512B）**：提升20-30%
- **大数据包（>512B）**：提升30-40%

---

# 第三部分：Pipeline处理引擎实战

## Pipeline架构

```
┌─────────┐   Ring1   ┌─────────────┐   Ring2   ┌─────────┐
│ RX      │ ─────────>│  Worker     │ ─────────>│ TX      │
│ Thread  │           │  Pool       │           │ Thread  │
│         │           │  (4 threads)│           │         │
└─────────┘           └─────────────┘           └─────────┘
    │                       │                        │
    v                       v                        v
  收包                   处理packet               发包
  (模拟)                 (统计/修改)              (模拟)
```

## 核心组件

### 1. RX Thread
```c
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
```

### 2. Worker Pool
```c
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
```

### 3. TX Thread
```c
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

## 性能目标

- **吞吐量**：>10 Mpps（4-worker配置）
- **延迟**：<10 微秒（平均端到端）
- **CPU使用率**：<80%（单核）
- **扩展性**：线性扩展到8 workers

## 关键设计要点

### 1. Ring大小选择
```c
#define RX_RING_SIZE  4096   // RX→Worker: 大容量缓冲突发
#define TX_RING_SIZE  2048   // Worker→TX: 中等容量
```

### 2. 批量大小
```c
#define BATCH_SIZE  32       // 32是性能和延迟的平衡点
```

### 3. 背压机制
```c
/* 检测Ring使用率 */
if (rte_ring_free_count(ring) < RING_SIZE / 4) {
    // Ring接近满，触发背压
    backpressure_active = 1;
    rte_pause();
}
```

### 4. 负载均衡
Workers自动从Ring1竞争出队，实现动态负载均衡。

## 编译运行

```bash
# 编译
make clean && make APP=ring_pipeline

# 运行（1 RX + 4 Workers + 1 TX）
sudo ./ring_pipeline -l 0-6
```

## 性能调优建议

### 1. NUMA优化
```bash
# 绑定到同一Socket
sudo ./ring_pipeline -l 0-5  # Socket 0的6个核心
```

### 2. CPU隔离
```bash
# 内核启动参数
isolcpus=0-7 nohz_full=0-7 rcu_nocbs=0-7
```

### 3. Huge页面
```bash
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

### 4. 监控工具
```bash
# 实时监控Ring状态
watch -n 1 'cat /proc/<pid>/status | grep -E "Cpu|Ring"'
```

---

# 总结与最佳实践

## 性能优化Checklist

- [ ] **批量操作**：使用Bulk/Burst，BATCH_SIZE=32-64
- [ ] **Ring大小**：设置为2的幂次（1024-8192）
- [ ] **Cache对齐**：数据结构使用`__rte_cache_aligned`
- [ ] **NUMA亲和**：确保同一Socket访问
- [ ] **Zero-Copy**：大数据包场景使用Zero-Copy API
- [ ] **模式选择**：根据场景选择SP/SC、MP/MC、RTS或HTS

## 常见陷阱

1. ⚠️ **在MP/MC Ring上使用SP/SC API** → 数据竞争
2. ⚠️ **RING_SIZE过小** → 频繁满/空
3. ⚠️ **忘记检查返回值** → 数据丢失
4. ⚠️ **内存泄漏** → 忘记free出队的对象
5. ⚠️ **NUMA不匹配** → 跨Socket访问慢50%+

## 进阶方向

1. **与Mbuf结合**：使用Ring传递mbuf指针
2. **多进程通信**：Secondary进程共享Ring
3. **Event Dev**：学习Event-driven模型
4. **性能分析**：使用VTune/perf深入优化

---

## 参考资源

**官方文档**：
- Ring Library: https://doc.dpdk.org/guides/prog_guide/ring_lib.html
- Mempool Library: https://doc.dpdk.org/guides/prog_guide/mempool_lib.html

**示例代码**：
- Multi-process: `examples/multi_process/`
- Packet Ordering: `examples/packet_ordering/`

---

**完成本文档学习后，你应该能够：**
- ✅ 选择合适的Ring同步模式
- ✅ 使用Zero-Copy API优化性能
- ✅ 构建完整的Pipeline处理系统
- ✅ 进行性能调优和问题排查
