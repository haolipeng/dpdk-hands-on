# Lesson 24: DPDK Graph Framework - 声明式包处理框架

## 目录
- [1. Graph Framework 简介](#1-graph-framework-简介)
- [2. 为什么需要 Graph Framework](#2-为什么需要-graph-framework)
- [3. 核心概念](#3-核心概念)
- [4. Graph Framework vs 传统模型](#4-graph-framework-vs-传统模型)
- [5. API 详解](#5-api-详解)
- [6. 代码示例解析](#6-代码示例解析)
- [7. 高级特性](#7-高级特性)
- [8. 性能优化](#8-性能优化)
- [9. 实际应用场景](#9-实际应用场景)
- [10. 常见问题](#10-常见问题)

---

## 1. Graph Framework 简介

### 1.1 什么是 Graph Framework

DPDK Graph Framework 是在 **DPDK 20.11** 版本中引入的一个**声明式包处理框架**，它允许开发者以**有向无环图（DAG）** 的方式组织数据包处理流水线。

**核心特性**：
- ✅ **模块化**：将复杂的包处理逻辑分解为独立的处理节点（Node）
- ✅ **可重用**：节点可以在不同的 Graph 中复用
- ✅ **可组合**：通过连接节点构建复杂的处理流水线
- ✅ **高性能**：批量处理、Cache 友好、编译期优化
- ✅ **声明式**：用数据结构描述处理流程，而非命令式代码

### 1.2 版本要求

- **最低 DPDK 版本**：20.11
- **推荐版本**：21.11 或更高（功能更完善）

---

## 2. 为什么需要 Graph Framework

### 2.1 传统 run-to-completion 模型的痛点

传统的 DPDK 应用通常采用 **run-to-completion** 模型：

```c
while (1) {
    // 1. 接收数据包
    nb_rx = rte_eth_rx_burst(port_id, queue_id, mbufs, BURST_SIZE);

    // 2. 处理数据包 - 所有逻辑耦合在一起
    for (i = 0; i < nb_rx; i++) {
        // 解析协议
        eth = rte_pktmbuf_mtod(mbufs[i], struct rte_ether_hdr *);

        // IP 处理
        if (eth->ether_type == RTE_ETHER_TYPE_IPV4) {
            ip = (struct rte_ipv4_hdr *)(eth + 1);
            // 路由查找
            // ACL 检查
            // 修改 TTL
            // 重新计算校验和
        }

        // L4 处理
        // ...
    }

    // 3. 发送数据包
    rte_eth_tx_burst(port_id, queue_id, mbufs, nb_rx);
}
```

**存在的问题**：

| 问题 | 描述 | 影响 |
|------|------|------|
| **代码耦合** | 所有处理逻辑混在一起 | 难以维护和扩展 |
| **难以复用** | 功能模块无法在其他项目中复用 | 重复开发 |
| **性能调优难** | 无法针对单个处理阶段优化 | 整体性能受限 |
| **不灵活** | 修改处理流程需要大量代码改动 | 开发效率低 |
| **测试困难** | 难以单独测试某个处理阶段 | 质量保障难 |

### 2.2 Graph Framework 的优势

```
传统模型:
┌─────────────────────────────────────┐
│  RX -> Parse -> Route -> ACL -> TX  │  (一整块耦合代码)
└─────────────────────────────────────┘

Graph 模型:
┌────┐    ┌───────┐    ┌───────┐    ┌─────┐    ┌────┐
│ RX │───>│ Parse │───>│ Route │───>│ ACL │───>│ TX │  (独立可复用节点)
└────┘    └───────┘    └───────┘    └─────┘    └────┘
```

**Graph Framework 带来的好处**：

1. **模块化设计**
   - 每个节点（Node）是独立的处理单元
   - 清晰的输入/输出接口
   - 易于理解和维护

2. **高度可复用**
   - 节点可以在不同的 Graph 中复用
   - DPDK 提供了内置节点库（Ethernet、IP、路由等）
   - 社区可以共享节点实现

3. **灵活的拓扑**
   - 运行时动态构建处理流水线
   - 支持分支、合并等复杂拓扑
   - 易于添加新功能

4. **性能优化**
   - **批量处理**：节点之间批量传递数据包
   - **Cache 友好**：减少 cache miss
   - **编译期优化**：静态分析和代码生成

5. **易于测试**
   - 单独测试每个节点
   - 使用 mock 节点进行单元测试

---

## 3. 核心概念

### 3.1 Node（节点）

**Node** 是 Graph 的基本处理单元，执行特定的包处理逻辑。

**节点的关键要素**：

```c
struct rte_node_register {
    char name[RTE_NODE_NAMESIZE];        // 节点名称
    rte_node_process_t process;          // 处理函数
    uint16_t nb_edges;                   // 边的数量（下一个节点数）
    const char *next_nodes[RTE_NODE_EDGE_MAX];  // 下一个节点列表
    // ... 其他字段
};
```

**节点类型**：

1. **Source Node**（源节点）
   - 生成或接收数据包
   - 例如：`ethdev_rx`（网卡接收）

2. **Intermediate Node**（中间节点）
   - 处理数据包并转发
   - 例如：`ip4_lookup`（IPv4 路由查找）

3. **Sink Node**（汇聚节点）
   - 最终处理数据包
   - 例如：`ethdev_tx`（网卡发送）、`pkt_drop`（丢包）

### 3.2 Edge（边）

**Edge** 定义节点之间的连接关系，表示数据包的流向。

```c
// 定义节点的下一跳
static struct rte_node_register my_node = {
    .name = "my_node",
    .process = my_node_process,
    .nb_edges = 2,
    .next_nodes = {
        [0] = "next_node_1",  // 边 0 连接到 next_node_1
        [1] = "next_node_2",  // 边 1 连接到 next_node_2
    },
};
```

**转发数据包到特定边**：

```c
// 将数据包从边 0 转发到下一个节点
rte_node_enqueue(graph, node, 0, objs, nb_objs);
```

### 3.3 Graph（图）

**Graph** 是多个节点通过边连接而成的有向无环图（DAG）。

```c
// 定义 Graph 拓扑
const char *patterns[] = {
    "ethdev_rx-ip4_lookup-ethdev_tx",  // RX -> IP 路由 -> TX
    "ip4_lookup-pkt_drop",              // 路由失败 -> 丢包
};

// 创建 Graph 实例
struct rte_graph_param graph_conf = {
    .socket_id = rte_socket_id(),
    .nb_node_patterns = 2,
    .node_patterns = patterns,
};

graph = rte_graph_create("my_graph", &graph_conf);
```

### 3.4 Graph Walk（图遍历）

**Graph Walk** 是驱动 Graph 执行的核心机制。

```c
while (!quit) {
    rte_graph_walk(graph);  // 遍历 Graph，调用各节点的处理函数
}
```

**工作原理**：
1. 从 Source Node 开始
2. 批量处理数据包
3. 将处理后的数据包传递给下一个节点
4. 重复直到所有数据包处理完毕

---

## 4. Graph Framework vs 传统模型

### 4.1 代码组织对比

#### 传统 run-to-completion 模型

```c
// 所有逻辑耦合在主循环中
while (1) {
    nb_rx = rte_eth_rx_burst(port, queue, mbufs, 32);

    for (i = 0; i < nb_rx; i++) {
        // 步骤 1: 解析
        parse_packet(mbufs[i]);

        // 步骤 2: 路由
        route_packet(mbufs[i]);

        // 步骤 3: 修改
        modify_packet(mbufs[i]);
    }

    rte_eth_tx_burst(port, queue, mbufs, nb_rx);
}
```

#### Graph Framework 模型

```c
// 定义节点 1: 解析
static uint16_t parse_node_process(...) { ... }
RTE_NODE_REGISTER(parse_node);

// 定义节点 2: 路由
static uint16_t route_node_process(...) { ... }
RTE_NODE_REGISTER(route_node);

// 定义节点 3: 修改
static uint16_t modify_node_process(...) { ... }
RTE_NODE_REGISTER(modify_node);

// 构建 Graph
const char *patterns[] = {
    "ethdev_rx-parse_node-route_node-modify_node-ethdev_tx",
};
graph = rte_graph_create("my_graph", &graph_conf);

// 主循环变得极其简单
while (1) {
    rte_graph_walk(graph);
}
```

### 4.2 性能对比

| 特性 | 传统模型 | Graph Framework |
|------|----------|----------------|
| **批量处理** | 手动实现 | 自动批量传递 |
| **Cache 利用** | 需要手动优化 | 框架自动优化 |
| **分支预测** | 依赖编译器 | 静态分析优化 |
| **扩展性** | 线性下降 | 保持良好 |
| **开发效率** | 低 | 高 |

**性能测试**（参考 DPDK 官方测试）：
- L3 Forwarding：Graph 模型 vs 传统模型 ≈ **95-105%**
- 复杂流水线：Graph 模型 **优于** 传统模型 10-20%

---

## 5. API 详解

### 5.1 节点注册 API

#### 定义节点结构

```c
struct rte_node_register {
    char name[RTE_NODE_NAMESIZE];     // 节点名称，全局唯一
    uint64_t flags;                    // 节点标志（RTE_NODE_SOURCE_F 等）
    rte_node_process_t process;        // 处理函数
    rte_node_init_t init;              // 初始化函数（可选）
    rte_node_fini_t fini;              // 清理函数（可选）
    uint16_t nb_edges;                 // 边的数量
    const char *next_nodes[RTE_NODE_EDGE_MAX];  // 下一个节点名称列表
};
```

#### 注册节点

```c
// 方法 1: 使用宏（推荐）
RTE_NODE_REGISTER(my_node);

// 方法 2: 手动注册
rte_node_register(&my_node);
```

### 5.2 节点处理函数

```c
typedef uint16_t (*rte_node_process_t)(
    struct rte_graph *graph,      // Graph 实例
    struct rte_node *node,        // 当前节点
    void **objs,                  // 输入对象数组（通常是 mbuf）
    uint16_t nb_objs              // 对象数量
);
```

**函数职责**：
1. 处理输入的 `objs` 数组
2. 将处理后的对象传递给下一个节点
3. 返回处理的对象数量

**示例实现**：

```c
static uint16_t
my_node_process(struct rte_graph *graph, struct rte_node *node,
                void **objs, uint16_t nb_objs)
{
    uint16_t i;
    struct rte_mbuf *mbufs[32];
    uint16_t valid_count = 0;

    // 处理每个数据包
    for (i = 0; i < nb_objs; i++) {
        struct rte_mbuf *m = (struct rte_mbuf *)objs[i];

        // 业务逻辑
        if (is_valid(m)) {
            mbufs[valid_count++] = m;
        } else {
            rte_pktmbuf_free(m);  // 丢弃无效包
        }
    }

    // 将有效包传递给下一个节点（边 0）
    rte_node_enqueue(graph, node, 0, (void **)mbufs, valid_count);

    return nb_objs;  // 返回处理的数量
}
```

### 5.3 Graph 管理 API

#### 创建 Graph

```c
struct rte_graph_param {
    int socket_id;                  // NUMA socket ID
    uint16_t nb_node_patterns;      // 模式数量
    const char **node_patterns;     // 节点模式数组
};

struct rte_graph *rte_graph_create(const char *name,
                                   struct rte_graph_param *prm);
```

**节点模式语法**：
```c
// 基本模式：node1-node2-node3
"ethdev_rx-ip4_lookup-ethdev_tx"

// 多个模式定义多条路径
const char *patterns[] = {
    "ethdev_rx-ip4_lookup-ethdev_tx",  // 路径 1
    "ip4_lookup-pkt_drop",              // 路径 2（查找失败丢包）
};
```

#### 销毁 Graph

```c
void rte_graph_destroy(rte_graph_t id);
```

#### Graph 遍历

```c
// 单次遍历
static inline void rte_graph_walk(struct rte_graph *graph);
```

### 5.4 节点操作 API

#### 传递数据包到下一个节点

```c
static inline void
rte_node_enqueue(struct rte_graph *graph, struct rte_node *node,
                 rte_edge_t edge, void **objs, uint16_t nb_objs);
```

**参数说明**：
- `edge`：边的索引（对应 `next_nodes` 数组的索引）
- `objs`：对象数组（通常是 `struct rte_mbuf **`）
- `nb_objs`：对象数量

#### 批量传递（多个边）

```c
static inline void
rte_node_enqueue_x4(struct rte_graph *graph, struct rte_node *node,
                    rte_edge_t edge, void *obj0, void *obj1,
                    void *obj2, void *obj3);
```

#### 获取节点 ID

```c
rte_node_t rte_node_from_name(const char *name);
```

---

## 6. 代码示例解析

### 6.1 示例拓扑结构

我们的示例程序实现了一个简单的三阶段处理流水线：

```
┌─────────────┐     ┌──────────────┐     ┌───────────┐
│ source_node │────>│ process_node │────>│ sink_node │
│ (生成数据包) │     │  (过滤数据包)  │     │ (接收数据包)│
└─────────────┘     └──────────────┘     └───────────┘
```

**处理逻辑**：
1. **source_node**：每次生成 32 个测试数据包（带序列号）
2. **process_node**：只保留奇数序列号的包，丢弃偶数包
3. **sink_node**：接收最终的数据包并统计

### 6.2 Source Node 详解

```c
static uint16_t
source_node_process(struct rte_graph *graph, struct rte_node *node,
                    void **objs, uint16_t nb_objs)
{
    struct rte_mbuf *mbufs[BURST_SIZE];
    uint16_t i;

    // Source Node 不需要处理输入（objs 为空）

    // 从内存池分配 mbuf
    if (rte_pktmbuf_alloc_bulk(mbuf_pool, mbufs, BURST_SIZE) != 0) {
        return 0;
    }

    // 填充数据包内容（序列号）
    for (i = 0; i < BURST_SIZE; i++) {
        uint32_t *data = rte_pktmbuf_mtod(mbufs[i], uint32_t *);
        *data = source_stats.pkts_processed + i;
        mbufs[i]->data_len = sizeof(uint32_t);
        mbufs[i]->pkt_len = sizeof(uint32_t);
    }

    // 将数据包传递到下一个节点（边 0 -> process_node）
    rte_node_enqueue(graph, node, 0, (void **)mbufs, BURST_SIZE);

    source_stats.pkts_processed += BURST_SIZE;

    return BURST_SIZE;
}

// 注册 Source Node
static struct rte_node_register source_node = {
    .name = "source_node",
    .process = source_node_process,
    .flags = RTE_NODE_SOURCE_F,  // 标记为源节点
    .nb_edges = 1,
    .next_nodes = {
        [0] = "process_node",  // 唯一的下一个节点
    },
};

RTE_NODE_REGISTER(source_node);
```

**关键点**：
- Source Node 通常设置 `RTE_NODE_SOURCE_F` 标志
- 不处理输入 `objs`，而是生成新的数据包
- 使用 `rte_node_enqueue()` 将数据包传递给下游

### 6.3 Process Node 详解

```c
static uint16_t
process_node_process(struct rte_graph *graph, struct rte_node *node,
                     void **objs, uint16_t nb_objs)
{
    struct rte_mbuf *valid_mbufs[BURST_SIZE];
    uint16_t valid_count = 0;
    uint16_t i;

    // 处理接收到的数据包
    for (i = 0; i < nb_objs; i++) {
        struct rte_mbuf *mbuf = (struct rte_mbuf *)objs[i];
        uint32_t *data = rte_pktmbuf_mtod(mbuf, uint32_t *);

        // 过滤逻辑：只保留奇数序列号
        if (*data % 2 == 1) {
            valid_mbufs[valid_count++] = mbuf;
            process_stats.pkts_processed++;
        } else {
            rte_pktmbuf_free(mbuf);  // 丢弃偶数包
            process_stats.pkts_dropped++;
        }
    }

    // 将有效包传递到下一个节点（边 0 -> sink_node）
    if (valid_count > 0) {
        rte_node_enqueue(graph, node, 0, (void **)valid_mbufs, valid_count);
    }

    return nb_objs;  // 返回处理的总数
}

static struct rte_node_register process_node = {
    .name = "process_node",
    .process = process_node_process,
    .nb_edges = 1,
    .next_nodes = {
        [0] = "sink_node",
    },
};

RTE_NODE_REGISTER(process_node);
```

**关键点**：
- 处理上游传递的 `objs` 数组
- 可以丢弃、修改或转发数据包
- 必须释放不再使用的 mbuf（避免内存泄漏）

### 6.4 Sink Node 详解

```c
static uint16_t
sink_node_process(struct rte_graph *graph, struct rte_node *node,
                  void **objs, uint16_t nb_objs)
{
    uint16_t i;

    // 处理最终数据包
    for (i = 0; i < nb_objs; i++) {
        struct rte_mbuf *mbuf = (struct rte_mbuf *)objs[i];

        // 在实际应用中，这里可以：
        // - 发送到网卡
        // - 写入文件
        // - 发送到其他进程

        sink_stats.pkts_processed++;

        // 释放 mbuf
        rte_pktmbuf_free(mbuf);
    }

    return nb_objs;
}

static struct rte_node_register sink_node = {
    .name = "sink_node",
    .process = sink_node_process,
    .nb_edges = 0,  // Sink Node 没有下一个节点
};

RTE_NODE_REGISTER(sink_node);
```

**关键点**：
- Sink Node 的 `nb_edges = 0`
- 必须释放所有接收到的 mbuf
- 通常负责最终的输出（发送、记录等）

### 6.5 创建和运行 Graph

```c
int main(int argc, char **argv)
{
    // 1. 定义 Graph 拓扑
    const char *patterns[] = {
        "source_node-process_node-sink_node",  // 唯一路径
    };

    // 2. 配置 Graph 参数
    struct rte_graph_param graph_conf = {
        .socket_id = rte_socket_id(),
        .nb_node_patterns = 1,
        .node_patterns = patterns,
    };

    // 3. 创建 Graph 实例
    graph = rte_graph_create("demo_graph", &graph_conf);
    if (graph == NULL)
        rte_panic("Failed to create graph\n");

    // 4. 启动 Graph worker 线程
    rte_eal_remote_launch(graph_main_loop, graph, worker_lcore);

    // 5. Graph worker 主循环
    while (!force_quit) {
        rte_graph_walk(graph);  // 驱动 Graph 执行
    }

    // 6. 清理
    rte_graph_destroy(rte_graph_from_obj(graph));
}
```

---

## 7. 高级特性

### 7.1 多边节点（Multi-Edge Node）

节点可以有多个输出边，根据条件转发到不同的下游节点。

```c
static struct rte_node_register classifier_node = {
    .name = "classifier_node",
    .process = classifier_process,
    .nb_edges = 3,
    .next_nodes = {
        [0] = "tcp_node",    // TCP 流量
        [1] = "udp_node",    // UDP 流量
        [2] = "other_node",  // 其他流量
    },
};

static uint16_t
classifier_process(struct rte_graph *graph, struct rte_node *node,
                   void **objs, uint16_t nb_objs)
{
    for (i = 0; i < nb_objs; i++) {
        struct rte_mbuf *m = (struct rte_mbuf *)objs[i];

        if (is_tcp(m)) {
            rte_node_enqueue(graph, node, 0, (void **)&m, 1);  // 边 0
        } else if (is_udp(m)) {
            rte_node_enqueue(graph, node, 1, (void **)&m, 1);  // 边 1
        } else {
            rte_node_enqueue(graph, node, 2, (void **)&m, 1);  // 边 2
        }
    }

    return nb_objs;
}
```

### 7.2 动态边（Dynamic Edge）

运行时动态添加边：

```c
// 运行时添加新的边
rte_node_edge_update(node_id, 0, new_edge_names, new_edge_count);
```

### 7.3 节点克隆（Node Clone）

为每个 worker 线程创建节点的独立副本：

```c
rte_node_t rte_node_clone(rte_node_t node_id, const char *name);
```

**应用场景**：
- 多线程并行处理
- 每个线程有独立的状态

### 7.4 内置节点库

DPDK 提供了丰富的内置节点：

| 节点类型 | 节点名称 | 功能 |
|---------|---------|------|
| **接收** | `ethdev_rx` | 网卡接收数据包 |
| **发送** | `ethdev_tx` | 网卡发送数据包 |
| **丢包** | `pkt_drop` | 丢弃数据包 |
| **IP 路由** | `ip4_lookup` | IPv4 路由查找 |
|  | `ip4_rewrite` | IPv4 包头重写 |
|  | `ip6_lookup` | IPv6 路由查找 |
| **协议** | `udp4_input` | UDP 输入处理 |
| **NULL** | `null` | 空节点（用于测试） |

**使用内置节点**：

```c
// 配置 ethdev_rx 节点
struct rte_node_ethdev_config ethdev_conf = {
    .port_id = 0,
    .queue_id = 0,
};
rte_node_eth_config(&ethdev_conf, 1, 1);

// 在 Graph 中使用
const char *patterns[] = {
    "ethdev_rx-ip4_lookup-ip4_rewrite-ethdev_tx",
};
```

---

## 8. 性能优化

### 8.1 批量处理优化

**原则**：尽量保持批量大小，减少单个包处理开销。

```c
// ❌ 不推荐：逐个转发
for (i = 0; i < nb_objs; i++) {
    rte_node_enqueue(graph, node, edge, &objs[i], 1);
}

// ✅ 推荐：批量转发
rte_node_enqueue(graph, node, edge, objs, nb_objs);
```

### 8.2 Cache Line 对齐

```c
struct my_node_ctx {
    uint64_t counter;
    // ...
} __rte_cache_aligned;  // 避免 false sharing
```

### 8.3 预取优化

```c
static uint16_t
optimized_node_process(struct rte_graph *graph, struct rte_node *node,
                       void **objs, uint16_t nb_objs)
{
    uint16_t i;

    // 预取下一个 mbuf
    for (i = 0; i < nb_objs - 1; i++) {
        rte_prefetch0(rte_pktmbuf_mtod(objs[i + 1], void *));

        // 处理当前 mbuf
        process_mbuf(objs[i]);
    }

    // 处理最后一个
    process_mbuf(objs[nb_objs - 1]);

    return nb_objs;
}
```

### 8.4 减少内存拷贝

```c
// ❌ 不推荐：拷贝数据包
memcpy(new_buf, old_buf, size);

// ✅ 推荐：直接修改 mbuf
modify_mbuf_inplace(mbuf);
```

### 8.5 Graph 拓扑优化

**原则**：
- 减少节点数量（合并简单节点）
- 减少分支数量（减少 cache miss）
- 热路径优先（常见路径短路径）

```c
// ❌ 过度细分
"rx-parse-validate-classify-route-modify-tx"

// ✅ 合理合并
"rx-parse_and_classify-route_and_modify-tx"
```

---

## 9. 实际应用场景

### 9.1 L3 Forwarding

```c
const char *patterns[] = {
    "ethdev_rx-ip4_lookup-ip4_rewrite-ethdev_tx",
    "ip4_lookup-pkt_drop",  // 路由失败丢包
};
```

### 9.2 防火墙（Firewall）

```c
const char *patterns[] = {
    "ethdev_rx-acl_classify-ethdev_tx",  // 允许的流量
    "acl_classify-pkt_drop",              // 拒绝的流量
};
```

### 9.3 负载均衡（Load Balancer）

```c
const char *patterns[] = {
    "ethdev_rx-lb_classifier-server1_tx",
    "lb_classifier-server2_tx",
    "lb_classifier-server3_tx",
};
```

### 9.4 DPI（深度包检测）

```c
const char *patterns[] = {
    "ethdev_rx-protocol_parse-pattern_match-action_node",
    "pattern_match-log_node",
    "pattern_match-drop_node",
};
```

### 9.5 VPN Gateway

```c
const char *patterns[] = {
    "ethdev_rx-ipsec_decrypt-ip4_lookup-ipsec_encrypt-ethdev_tx",
};
```

---

## 10. 常见问题

### 10.1 编译错误

**问题**：找不到 `rte_graph.h`

**解决**：
```bash
# 确保 DPDK 版本 >= 20.11
pkg-config --modversion libdpdk

# 检查头文件
pkg-config --cflags libdpdk | grep graph
```

### 10.2 运行时错误

**问题**：`Failed to create graph: no such node`

**原因**：节点未注册或名称拼写错误

**解决**：
```c
// 确保所有节点都已注册
RTE_NODE_REGISTER(my_node);

// 检查节点名称是否正确
const char *patterns[] = {
    "my_node-next_node",  // 确保名称与注册时一致
};
```

### 10.3 性能问题

**问题**：Graph 性能不如预期

**检查清单**：
- [ ] 批量大小是否足够（推荐 32-64）
- [ ] 是否使用了预取优化
- [ ] 节点是否过度细分
- [ ] 是否有不必要的内存拷贝
- [ ] NUMA 配置是否正确

### 10.4 内存泄漏

**问题**：运行一段时间后内存池耗尽

**原因**：节点未释放 mbuf

**解决**：
```c
// 确保所有分支都释放 mbuf
if (drop_packet) {
    rte_pktmbuf_free(mbuf);  // ✅ 丢包时释放
} else {
    rte_node_enqueue(...);   // ✅ 转发时由下游释放
}
```

---

## 总结

DPDK Graph Framework 是一个**强大而灵活**的包处理框架，适合构建复杂的网络应用：

✅ **优势**：
- 模块化、可重用、易维护
- 高性能（批量处理、Cache 友好）
- 灵活的拓扑结构
- 丰富的内置节点库

⚠️ **注意事项**：
- 需要 DPDK 20.11 或更高版本
- 学习曲线相对陡峭
- 简单应用可能过度设计

🎯 **适用场景**：
- 复杂的包处理流水线
- 需要高度可扩展的应用
- 多人协作的大型项目
- 需要复用现有节点的场景

📚 **下一步学习**：
1. 查看 DPDK 官方示例：`examples/l3fwd-graph`
2. 阅读内置节点源码：`lib/node/`
3. 尝试构建自己的复杂 Graph
4. 学习 Eventdev 与 Graph 的结合使用

---

## 参考资料

- [DPDK Graph Library Documentation](https://doc.dpdk.org/guides/prog_guide/graph_lib.html)
- [L3fwd-graph Sample Application](https://doc.dpdk.org/guides/sample_app_ug/l3_forward_graph.html)
- [DPDK Graph API Reference](https://doc.dpdk.org/api/rte__graph_8h.html)
- [DPDK Summit: Graph Framework Presentation](https://www.dpdk.org/resources/)
