# Lesson 24: DPDK Graph Framework 基础示例

## 简介

本示例展示了如何使用 DPDK Graph Framework 构建一个简单的包处理流水线。

## Graph 拓扑

```
┌─────────────┐     ┌──────────────┐     ┌───────────┐
│ source_node │────>│ process_node │────>│ sink_node │
│ (生成数据包) │     │  (过滤数据包)  │     │ (接收数据包)│
└─────────────┘     └──────────────┘     └───────────┘
```

**处理流程**：
1. **source_node**：生成带序列号的测试数据包（每次 32 个）
2. **process_node**：过滤数据包，只保留奇数序列号的包
3. **sink_node**：接收最终数据包并释放

## 编译

```bash
cd build
make graph_demo
```

## 运行

```bash
# 需要至少 2 个 CPU 核心
sudo ./bin/graph_demo -l 0-1 --no-pci

# 或者使用更多核心
sudo ./bin/graph_demo -l 0-3 --no-pci
```

## 输出示例

```
Mbuf pool created: 8192 mbufs
Graph created successfully: demo_graph

Graph topology:
  (graph ID: 0, name: "demo_graph")
  - source_node -> process_node
  - process_node -> sink_node
  - sink_node

Launching graph worker on lcore 1

Press Ctrl+C to stop...

============================================
          Graph Statistics
============================================
Source Node:
  Packets Generated : 102400

Process Node:
  Packets Processed : 51200
  Packets Dropped   : 51200

Sink Node:
  Packets Received  : 51200
============================================
```

## 关键概念

### 1. 节点注册

使用 `RTE_NODE_REGISTER()` 宏注册节点：

```c
static struct rte_node_register my_node = {
    .name = "my_node",
    .process = my_node_process,
    .nb_edges = 1,
    .next_nodes = {
        [0] = "next_node_name",
    },
};

RTE_NODE_REGISTER(my_node);
```

### 2. 节点处理函数

```c
static uint16_t
my_node_process(struct rte_graph *graph, struct rte_node *node,
                void **objs, uint16_t nb_objs)
{
    // 处理数据包
    for (i = 0; i < nb_objs; i++) {
        process_packet(objs[i]);
    }

    // 转发到下一个节点
    rte_node_enqueue(graph, node, 0, objs, nb_objs);

    return nb_objs;
}
```

### 3. 创建 Graph

```c
const char *patterns[] = {
    "node1-node2-node3",
};

struct rte_graph_param graph_conf = {
    .socket_id = rte_socket_id(),
    .nb_node_patterns = 1,
    .node_patterns = patterns,
};

rte_graph_t graph_id = rte_graph_create("my_graph", &graph_conf);
```

### 4. 驱动 Graph 执行

```c
struct rte_graph *graph = rte_graph_lookup("my_graph");

while (!quit) {
    rte_graph_walk(graph);
}
```

## 扩展练习

1. **添加新节点**：创建一个新的处理节点，例如修改数据包内容
2. **多路径**：添加分支，根据条件将数据包发送到不同的下游节点
3. **性能测试**：测试不同批量大小对性能的影响
4. **统计增强**：添加更详细的性能统计（吞吐量、延迟等）

## 参考文档

- [完整教程](../../docs/lessons/lesson24-graph-framework.md)
- [DPDK Graph Library 官方文档](https://doc.dpdk.org/guides/prog_guide/graph_lib.html)
- [L3fwd-graph 示例](https://doc.dpdk.org/guides/sample_app_ug/l3_forward_graph.html)

## 故障排除

### 问题：编译错误 "target specific option mismatch"

**解决**：确保 CMakeLists.txt 中包含了 `-mssse3 -march=native` 编译选项。

### 问题：运行时错误 "no such node"

**解决**：确保所有节点都已通过 `RTE_NODE_REGISTER()` 注册。

### 问题：性能不佳

**检查**：
- 批量大小是否足够（BURST_SIZE）
- NUMA 配置是否正确
- 是否有不必要的内存拷贝
