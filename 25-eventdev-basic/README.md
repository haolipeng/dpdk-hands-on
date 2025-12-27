# Lesson 25: DPDK Eventdev 基础示例

## 简介

本示例展示了如何使用 DPDK Eventdev 框架构建一个事件驱动应用，实现生产者-消费者模型。

## 架构

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

                                               ┌──────────────┐
                                          ────>│  Worker N    │
                                               │  (lcore N+1) │
                                               └──────────────┘
```

**处理流程**：
1. **Producer**：生成事件并入队到 Eventdev
2. **Eventdev Scheduler**：自动将事件分发到空闲的 Worker
3. **Workers**：并发处理事件

## 编译

```bash
cd build
make eventdev_demo
```

## 运行

### 使用软件 Eventdev（推荐用于测试）

```bash
# 2 个 worker
sudo ./bin/eventdev_demo -l 0-3 --vdev=event_sw0 -- -w 2

# 4 个 worker
sudo ./bin/eventdev_demo -l 0-5 --vdev=event_sw0 -- -w 4
```

**参数说明**：
- `-l 0-3`：使用 4 个 CPU 核心（1 main + 1 producer + 2 workers）
- `--vdev=event_sw0`：创建软件 eventdev 设备
- `-- -w 2`：指定 2 个 worker 线程

### 使用硬件 Eventdev（需要硬件支持）

```bash
# Intel DLB
sudo ./bin/eventdev_demo -l 0-15 -a 0000:6f:00.0 -- -w 8
```

## 输出示例

```
Mbuf pool created: 8192 mbufs
Event device 0 info:
  Max event queues   : 255
  Max event ports    : 255
  Max events         : 4096
Eventdev 0 configured successfully
  Event queues : 1
  Event ports  : 3 (1 producer + 2 workers)

Launching producer on lcore 1
Launching worker 0 on lcore 2
Launching worker 1 on lcore 3

Producer thread started on lcore 1
Worker 0 thread started on lcore 2 (port 1)
Worker 1 thread started on lcore 3 (port 2)

Press Ctrl+C to stop...

============================================
       Eventdev Statistics
============================================
Producer:
  Events Produced : 320000
  Events Dropped  : 0

Worker 0:
  Events Consumed : 160128

Worker 1:
  Events Consumed : 159872

Total Consumed    : 320000
============================================
```

## 关键概念

### 1. Event（事件）

事件是 Eventdev 的基本单元：

```c
struct rte_event {
    uint64_t event;        // 元数据（packed）
    struct rte_mbuf *mbuf; // 数据载体
};
```

**元数据包括**：
- `queue_id`：目标队列
- `sched_type`：调度类型（Atomic/Ordered/Parallel）
- `priority`：优先级
- `flow_id`：流 ID（用于调度）

### 2. 调度模式

#### Atomic（原子调度）
- 保证同一流的事件被同一个 worker 串行处理
- 适合有状态处理（TCP 连接、流统计）

#### Ordered（顺序调度）
- 允许并行处理，但保证输出顺序
- 适合需要重排序的场景（IPsec）

#### Parallel（并行调度）
- 完全并行，无顺序保证
- 最高吞吐量，适合无状态处理

### 3. Event Queue（事件队列）

队列定义了事件的调度策略：

```c
struct rte_event_queue_conf queue_conf = {
    .nb_atomic_flows = 1024,
    .schedule_type = RTE_SCHED_TYPE_ATOMIC,
    .priority = RTE_EVENT_DEV_PRIORITY_NORMAL,
};
```

### 4. Event Port（事件端口）

端口是 worker 与 eventdev 的接口：

```c
struct rte_event_port_conf port_conf = {
    .dequeue_depth = 32,  // 出队深度
    .enqueue_depth = 32,  // 入队深度
};
```

## 代码关键点

### Producer 入队事件

```c
// 1. 构造事件
events[i].queue_id = 0;
events[i].op = RTE_EVENT_OP_NEW;
events[i].sched_type = RTE_SCHED_TYPE_ATOMIC;
events[i].mbuf = mbufs[i];

// 2. 批量入队
uint16_t sent = rte_event_enqueue_burst(dev_id, port_id, events, BURST_SIZE);
```

### Worker 出队和处理

```c
// 1. 出队事件
uint16_t nb = rte_event_dequeue_burst(dev_id, port_id, events, BURST_SIZE, 0);

// 2. 处理事件
for (int i = 0; i < nb; i++) {
    struct rte_mbuf *m = events[i].mbuf;
    process_data(m);
    rte_pktmbuf_free(m);  // 释放
}
```

## 性能优化建议

### 1. 批量大小

```c
#define BURST_SIZE 32  // 推荐 32-64
```

### 2. Worker 数量

- 根据 CPU 核心数调整
- 通常设置为 `核心数 - 2`（main + producer）

### 3. 流 ID 设置

```c
// 使用哈希保证均匀分布
event.flow_id = hash(key) % nb_atomic_flows;
```

### 4. NUMA 感知

```c
// 在本地 socket 创建资源
mbuf_pool = rte_pktmbuf_pool_create("pool", 8192, 0, 0, 2048,
                                    rte_socket_id());
```

## 扩展练习

1. **尝试不同调度模式**
   - 修改 `queue_conf.schedule_type`
   - 观察性能差异

2. **增加多级队列**
   - 创建多个队列
   - 实现流水线处理

3. **添加优先级**
   - 高优先级事件优先处理
   - 测试效果

4. **性能测试**
   - 测试不同 worker 数量
   - 测试不同批量大小
   - 对比软件和硬件 eventdev

## 调试技巧

### 查看设备信息

```bash
# 查看可用的 eventdev
dpdk-devbind.py --status-dev event
```

### 启用调试日志

```bash
sudo ./bin/eventdev_demo --log-level=event,8 ...
```

### 统计信息

程序会每 2 秒打印一次统计信息，包括：
- Producer 生成的事件数
- 每个 Worker 消费的事件数
- 总消费量

## 故障排除

### 问题：No event devices found

**解决**：确保添加了 `--vdev=event_sw0` 参数。

### 问题：Insufficient lcores

**解决**：增加 `-l` 参数中的核心数。例如：
```bash
# 需要：1 main + 1 producer + 2 workers = 4 核心
-l 0-3
```

### 问题：性能不佳

**检查**：
- 批量大小是否足够（BURST_SIZE）
- Worker 数量是否匹配核心数
- 是否使用了硬件 eventdev

## 参考文档

- [完整教程](../../docs/lessons/lesson25-eventdev-framework.md)
- [DPDK Eventdev 官方文档](https://doc.dpdk.org/guides/prog_guide/eventdev.html)
- [Eventdev Drivers](https://doc.dpdk.org/guides/eventdevs/index.html)

## 与其他课程的关系

- **Lesson 9 (Timer)**：Eventdev 也支持事件定时器
- **Lesson 24 (Graph)**：Graph 和 Eventdev 可以结合使用
- **Lesson 10-12 (Ring)**：Eventdev 内部使用类似机制

## 实际应用

Eventdev 广泛应用于：
- 5G UPF（用户平面功能）
- IPsec Gateway
- DPI（深度包检测）
- 负载均衡器
- NFV 应用
