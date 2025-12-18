# DPDK rte_flow 流控制完全指南

> 硬件加速的流量分类和精确匹配

---

## 开始之前: 为什么需要 Flow Director?

### 问题: RSS 的局限性

RSS 虽然能实现负载均衡,但有以下限制:

```
RSS 的问题:
┌─────────────────────────────────────────┐
│ 1. 只能负载均衡,不能精确匹配          │
│ 2. 无法区分优先级                      │
│ 3. 不能实现复杂的流分类                │
│ 4. 无法丢弃特定流量                    │
└─────────────────────────────────────────┘

例如:
  • 无法将 HTTP 流量单独处理
  • 无法阻止来自特定 IP 的流量
  • 无法为 VIP 客户提供专用队列
```

### 解决方案: rte_flow

**rte_flow** 是 DPDK 的通用流分类 API,支持:
- ✅ 精确匹配 (5-tuple, MAC 等)
- ✅ 优先级处理
- ✅ 复杂动作 (丢弃、标记、修改等)
- ✅ 硬件加速 (零 CPU 开销)

```
rte_flow 工作流程:
┌────────┐
│  NIC   │
└───┬────┘
    │ 收到数据包
    ▼
┌────────────────────────────┐
│   Flow 规则匹配 (硬件)     │
│                            │
│ 规则1: IP=1.2.3.4 → 队列5  │
│ 规则2: TCP端口=80 → 队列3  │
│ 规则3: IP=攻击者 → DROP    │
└────────┬───────────────────┘
         │
         ▼
    执行动作 (硬件)
    • 分发到指定队列
    • 打标记
    • 丢弃
    • 修改包
```

---

## 第一课: rte_flow 核心概念

### 1.1 Flow 规则的三要素

```
Flow 规则 = 属性 + 模式 + 动作

┌─────────────────────────────────────────┐
│           Flow 规则结构                 │
├─────────────────────────────────────────┤
│ 1. 属性 (Attribute)                     │
│    - 优先级                             │
│    - 方向 (ingress/egress)              │
│    - 传输模式                           │
├─────────────────────────────────────────┤
│ 2. 模式 (Pattern)                       │
│    - 匹配条件                           │
│    - 例: IP=1.2.3.4 AND PORT=80         │
├─────────────────────────────────────────┤
│ 3. 动作 (Action)                        │
│    - 匹配后做什么                       │
│    - 例: 发送到队列3                    │
└─────────────────────────────────────────┘
```

### 1.2 基本示例

```c
// 创建一个简单的流规则
// 匹配: 目的IP = 192.168.1.100
// 动作: 发送到队列 5

struct rte_flow_attr attr = {
    .ingress = 1,  // 接收方向
};

struct rte_flow_item pattern[] = {
    { .type = RTE_FLOW_ITEM_TYPE_ETH },
    { .type = RTE_FLOW_ITEM_TYPE_IPV4,
      .spec = &ipv4_spec,
      .mask = &ipv4_mask },
    { .type = RTE_FLOW_ITEM_TYPE_END },
};

struct rte_flow_action actions[] = {
    { .type = RTE_FLOW_ACTION_TYPE_QUEUE,
      .conf = &queue_action },
    { .type = RTE_FLOW_ACTION_TYPE_END },
};

struct rte_flow *flow = rte_flow_create(
    port_id, &attr, pattern, actions, &error);
```

---

## 第二课: Pattern (模式匹配)

### 2.1 常用的 Pattern 类型

```c
/* L2 层 */
RTE_FLOW_ITEM_TYPE_ETH       // 以太网帧
RTE_FLOW_ITEM_TYPE_VLAN      // VLAN 标签

/* L3 层 */
RTE_FLOW_ITEM_TYPE_IPV4      // IPv4
RTE_FLOW_ITEM_TYPE_IPV6      // IPv6

/* L4 层 */
RTE_FLOW_ITEM_TYPE_TCP       // TCP
RTE_FLOW_ITEM_TYPE_UDP       // UDP
RTE_FLOW_ITEM_TYPE_SCTP      // SCTP

/* 其他 */
RTE_FLOW_ITEM_TYPE_ANY       // 任意类型
RTE_FLOW_ITEM_TYPE_END       // 结束标记
```

### 2.2 匹配特定 IP 地址

```c
struct rte_flow_item_ipv4 ipv4_spec = {
    .hdr = {
        .dst_addr = rte_cpu_to_be_32(0xC0A80164), // 192.168.1.100
    },
};

struct rte_flow_item_ipv4 ipv4_mask = {
    .hdr = {
        .dst_addr = RTE_BE32(0xFFFFFFFF), // 完全匹配
    },
};

struct rte_flow_item pattern[] = {
    { .type = RTE_FLOW_ITEM_TYPE_ETH },
    {
        .type = RTE_FLOW_ITEM_TYPE_IPV4,
        .spec = &ipv4_spec,
        .mask = &ipv4_mask,
    },
    { .type = RTE_FLOW_ITEM_TYPE_END },
};
```

### 2.3 匹配 TCP 端口

```c
struct rte_flow_item_tcp tcp_spec = {
    .hdr = {
        .dst_port = rte_cpu_to_be_16(80), // 端口 80
    },
};

struct rte_flow_item_tcp tcp_mask = {
    .hdr = {
        .dst_port = RTE_BE16(0xFFFF),
    },
};

struct rte_flow_item pattern[] = {
    { .type = RTE_FLOW_ITEM_TYPE_ETH },
    { .type = RTE_FLOW_ITEM_TYPE_IPV4 },
    {
        .type = RTE_FLOW_ITEM_TYPE_TCP,
        .spec = &tcp_spec,
        .mask = &tcp_mask,
    },
    { .type = RTE_FLOW_ITEM_TYPE_END },
};
```

### 2.4 匹配子网 (使用 Mask)

```c
// 匹配 192.168.1.0/24 网段
struct rte_flow_item_ipv4 ipv4_spec = {
    .hdr = {
        .src_addr = rte_cpu_to_be_32(0xC0A80100), // 192.168.1.0
    },
};

struct rte_flow_item_ipv4 ipv4_mask = {
    .hdr = {
        .src_addr = RTE_BE32(0xFFFFFF00), // 前24位匹配
    },
};
```

---

## 第三课: Action (动作)

### 3.1 常用的 Action 类型

```c
/* 队列操作 */
RTE_FLOW_ACTION_TYPE_QUEUE       // 发送到指定队列
RTE_FLOW_ACTION_TYPE_RSS         // RSS 分发
RTE_FLOW_ACTION_TYPE_DROP        // 丢弃

/* 标记操作 */
RTE_FLOW_ACTION_TYPE_MARK        // 打标记
RTE_FLOW_ACTION_TYPE_FLAG        // 设置标志

/* 计数操作 */
RTE_FLOW_ACTION_TYPE_COUNT       // 计数

/* 修改操作 */
RTE_FLOW_ACTION_TYPE_SET_IPV4_SRC    // 修改源IP
RTE_FLOW_ACTION_TYPE_SET_IPV4_DST    // 修改目的IP
RTE_FLOW_ACTION_TYPE_SET_MAC_SRC     // 修改源MAC
RTE_FLOW_ACTION_TYPE_SET_MAC_DST     // 修改目的MAC

/* 其他 */
RTE_FLOW_ACTION_TYPE_PASSTHRU    // 继续正常处理
RTE_FLOW_ACTION_TYPE_END         // 结束标记
```

### 3.2 发送到指定队列

```c
struct rte_flow_action_queue queue = {
    .index = 5,  // 队列 5
};

struct rte_flow_action actions[] = {
    {
        .type = RTE_FLOW_ACTION_TYPE_QUEUE,
        .conf = &queue,
    },
    { .type = RTE_FLOW_ACTION_TYPE_END },
};
```

### 3.3 丢弃流量 (DDoS 防护)

```c
// 丢弃来自攻击者的流量
struct rte_flow_action actions[] = {
    { .type = RTE_FLOW_ACTION_TYPE_DROP },
    { .type = RTE_FLOW_ACTION_TYPE_END },
};
```

### 3.4 打标记

```c
struct rte_flow_action_mark mark = {
    .id = 123,  // 标记 ID
};

struct rte_flow_action actions[] = {
    {
        .type = RTE_FLOW_ACTION_TYPE_MARK,
        .conf = &mark,
    },
    { .type = RTE_FLOW_ACTION_TYPE_QUEUE,
      .conf = &queue },
    { .type = RTE_FLOW_ACTION_TYPE_END },
};

// 在收包时检查标记
struct rte_mbuf *mbuf;
if (mbuf->ol_flags & RTE_MBUF_F_RX_FDIR_ID) {
    uint32_t mark_id = mbuf->hash.fdir.hi;
    printf("Packet marked with ID: %u\n", mark_id);
}
```

### 3.5 计数

```c
struct rte_flow_action_count count = {
    .id = 1,
};

struct rte_flow_action actions[] = {
    {
        .type = RTE_FLOW_ACTION_TYPE_COUNT,
        .conf = &count,
    },
    { .type = RTE_FLOW_ACTION_TYPE_QUEUE,
      .conf = &queue },
    { .type = RTE_FLOW_ACTION_TYPE_END },
};

// 查询计数
struct rte_flow_query_count count_query = {0};
rte_flow_query(port_id, flow, &count_action, &count_query, &error);
printf("Packets: %"PRIu64", Bytes: %"PRIu64"\n",
       count_query.hits, count_query.bytes);
```

---

## 第四课: 实战场景

### 4.1 场景1: HTTP 流量专用处理

```c
/* 匹配所有 HTTP 流量 (TCP 端口 80) */
struct rte_flow_item_tcp tcp_spec = {
    .hdr.dst_port = rte_cpu_to_be_16(80),
};

struct rte_flow_item_tcp tcp_mask = {
    .hdr.dst_port = RTE_BE16(0xFFFF),
};

struct rte_flow_action_queue queue = {
    .index = 10,  // HTTP 专用队列
};

/* 创建规则 */
struct rte_flow *http_flow = create_flow(
    port_id, &tcp_spec, &tcp_mask, &queue);
```

### 4.2 场景2: VIP 客户专用通道

```c
/* VIP 客户 IP 列表 */
const char *vip_ips[] = {
    "10.0.0.100",
    "10.0.0.101",
    "10.0.0.102",
};

/* 为每个 VIP 创建高优先级流规则 */
for (int i = 0; i < num_vips; i++) {
    struct rte_flow_item_ipv4 ipv4_spec = {
        .hdr.src_addr = parse_ip(vip_ips[i]),
    };

    struct rte_flow_attr attr = {
        .priority = 0,  // 最高优先级
        .ingress = 1,
    };

    create_vip_flow(port_id, &attr, &ipv4_spec);
}
```

### 4.3 场景3: DDoS 攻击防护

```c
/* 检测到攻击源,动态添加丢弃规则 */
void block_attacker(uint32_t attacker_ip)
{
    struct rte_flow_item_ipv4 ipv4_spec = {
        .hdr.src_addr = rte_cpu_to_be_32(attacker_ip),
    };

    struct rte_flow_item_ipv4 ipv4_mask = {
        .hdr.src_addr = RTE_BE32(0xFFFFFFFF),
    };

    struct rte_flow_action actions[] = {
        { .type = RTE_FLOW_ACTION_TYPE_DROP },
        { .type = RTE_FLOW_ACTION_TYPE_END },
    };

    struct rte_flow *flow = rte_flow_create(
        port_id, &attr, pattern, actions, &error);

    printf("Blocked attacker: %u.%u.%u.%u\n",
           (attacker_ip >> 24) & 0xFF,
           (attacker_ip >> 16) & 0xFF,
           (attacker_ip >> 8) & 0xFF,
           attacker_ip & 0xFF);
}
```

### 4.4 场景4: 流量镜像

```c
/* 将特定流量同时发送到多个队列 */
struct rte_flow_action actions[] = {
    {
        .type = RTE_FLOW_ACTION_TYPE_QUEUE,
        .conf = &(struct rte_flow_action_queue){.index = 5},
    },
    {
        .type = RTE_FLOW_ACTION_TYPE_QUEUE,
        .conf = &(struct rte_flow_action_queue){.index = 6},
    },
    { .type = RTE_FLOW_ACTION_TYPE_END },
};
```

---

## 第五课: 高级特性

### 5.1 优先级

```c
struct rte_flow_attr attr_high = {
    .priority = 0,  // 高优先级 (数字越小越高)
    .ingress = 1,
};

struct rte_flow_attr attr_low = {
    .priority = 10,  // 低优先级
    .ingress = 1,
};

/* 规则匹配顺序: 优先级高的先匹配 */
```

### 5.2 组合匹配 (5-tuple)

```c
/* 精确匹配一条 TCP 流 */
struct rte_flow_item_ipv4 ipv4_spec = {
    .hdr = {
        .src_addr = rte_cpu_to_be_32(0xC0A80101),
        .dst_addr = rte_cpu_to_be_32(0xC0A80102),
    },
};

struct rte_flow_item_tcp tcp_spec = {
    .hdr = {
        .src_port = rte_cpu_to_be_16(12345),
        .dst_port = rte_cpu_to_be_16(80),
    },
};

struct rte_flow_item pattern[] = {
    { .type = RTE_FLOW_ITEM_TYPE_ETH },
    {
        .type = RTE_FLOW_ITEM_TYPE_IPV4,
        .spec = &ipv4_spec,
        .mask = &ipv4_mask,
    },
    {
        .type = RTE_FLOW_ITEM_TYPE_TCP,
        .spec = &tcp_spec,
        .mask = &tcp_mask,
    },
    { .type = RTE_FLOW_ITEM_TYPE_END },
};
```

### 5.3 隔离模式

```c
/* 隔离模式: 只处理匹配的流量,其他全部丢弃 */
struct rte_flow_isolate_set isolate = {
    .set = 1,
};

ret = rte_flow_isolate(port_id, 1, &error);
if (ret != 0) {
    printf("Failed to enable isolation mode\n");
}
```

---

## 第六课: 规则管理

### 6.1 创建规则

```c
struct rte_flow *flow;
struct rte_flow_error error;

flow = rte_flow_create(port_id, &attr, pattern, actions, &error);
if (flow == NULL) {
    printf("Flow creation failed: %s\n", error.message);
    return -1;
}

/* 保存 flow 指针用于后续操作 */
```

### 6.2 销毁规则

```c
ret = rte_flow_destroy(port_id, flow, &error);
if (ret != 0) {
    printf("Flow destruction failed: %s\n", error.message);
}
```

### 6.3 刷新所有规则

```c
/* 删除端口上的所有流规则 */
ret = rte_flow_flush(port_id, &error);
if (ret != 0) {
    printf("Flow flush failed: %s\n", error.message);
}
```

### 6.4 查询规则统计

```c
struct rte_flow_action_count count_action = {
    .id = 1,
};

struct rte_flow_query_count count_query = {0};

ret = rte_flow_query(port_id, flow,
                     &(struct rte_flow_action){
                         .type = RTE_FLOW_ACTION_TYPE_COUNT,
                         .conf = &count_action,
                     },
                     &count_query, &error);

if (ret == 0) {
    printf("Hits: %"PRIu64", Bytes: %"PRIu64"\n",
           count_query.hits, count_query.bytes);
}
```

---

## 第七课: 最佳实践

### 7.1 验证规则有效性

```c
/* 创建前先验证 */
ret = rte_flow_validate(port_id, &attr, pattern, actions, &error);
if (ret != 0) {
    printf("Flow validation failed: %s\n", error.message);
    return -1;
}

/* 验证通过后再创建 */
flow = rte_flow_create(port_id, &attr, pattern, actions, &error);
```

### 7.2 错误处理

```c
struct rte_flow_error error;

flow = rte_flow_create(port_id, &attr, pattern, actions, &error);
if (flow == NULL) {
    printf("Flow creation failed:\n");
    printf("  Type: %d\n", error.type);
    printf("  Cause: %p\n", error.cause);
    printf("  Message: %s\n", error.message ? error.message : "none");
    return -1;
}
```

### 7.3 规则管理器

```c
#define MAX_FLOWS 1024

struct flow_manager {
    struct rte_flow *flows[MAX_FLOWS];
    uint32_t num_flows;
    uint16_t port_id;
};

/* 添加规则 */
int flow_mgr_add(struct flow_manager *mgr, struct rte_flow *flow)
{
    if (mgr->num_flows >= MAX_FLOWS)
        return -1;

    mgr->flows[mgr->num_flows++] = flow;
    return 0;
}

/* 清理所有规则 */
void flow_mgr_cleanup(struct flow_manager *mgr)
{
    struct rte_flow_error error;

    for (uint32_t i = 0; i < mgr->num_flows; i++) {
        rte_flow_destroy(mgr->port_id, mgr->flows[i], &error);
    }
    mgr->num_flows = 0;
}
```

---

## 第八课: 性能考虑

### 8.1 硬件能力查询

```c
struct rte_eth_dev_info dev_info;

rte_eth_dev_info_get(port_id, &dev_info);

printf("Flow capabilities:\n");
printf("  Max flow rules: %u\n", dev_info.max_rx_queues);
// 注: 不同网卡支持的规则数量差异很大
```

### 8.2 规则数量限制

```
典型硬件限制:
┌─────────────────┬──────────────┐
│   网卡型号      │ 最大规则数   │
├─────────────────┼──────────────┤
│ Intel X710      │ 7168         │
│ Intel E810      │ 32768        │
│ Mellanox CX-5   │ 32768+       │
│ 虚拟网卡        │ 有限/不支持  │
└─────────────────┴──────────────┘
```

### 8.3 性能影响

```
✓ 好:
  • 规则数量 < 1000
  • 使用简单模式(IP/端口)
  • 硬件支持的动作

⚠ 注意:
  • 规则数量 > 10000 (查找变慢)
  • 复杂模式组合
  • 软件 fallback (无硬件支持)
```

---

## 总结

### 核心要点

| 概念 | 说明 |
|------|------|
| **Pattern** | 匹配条件 (IP, 端口, 协议等) |
| **Action** | 匹配后的动作 (队列, 丢弃, 标记等) |
| **Priority** | 规则优先级 (数字越小越高) |
| **Validation** | 创建前先验证 |

### rte_flow vs RSS

```
┌───────────────┬───────────┬──────────────┐
│               │   RSS     │   rte_flow   │
├───────────────┼───────────┼──────────────┤
│ 负载均衡      │ ✓         │ ✓            │
│ 精确匹配      │ ✗         │ ✓            │
│ 优先级        │ ✗         │ ✓            │
│ 丢弃流量      │ ✗         │ ✓            │
│ 动态规则      │ ✗         │ ✓            │
│ 硬件加速      │ ✓         │ ✓            │
└───────────────┴───────────┴──────────────┘

建议: RSS + rte_flow 组合使用
```

### 典型应用

1. **DDoS 防护**: 动态阻止攻击源
2. **QoS**: VIP 客户专用队列
3. **负载均衡**: 精确流量分发
4. **流量分析**: 特定流量隔离处理
5. **安全过滤**: 黑名单/白名单

### 下一步

学完本课程后,你应该掌握:
1. ✅ rte_flow 的基本概念
2. ✅ Pattern 和 Action 的使用
3. ✅ 规则的创建和管理
4. ✅ 常见应用场景
5. ✅ 性能优化技巧

**下一课预告**: 统计和监控 - 实时流量分析系统

---

## 参考资料

- DPDK Programmer's Guide - Generic Flow API
- rte_flow.h API Documentation
- Intel Flow Director Programming Guide
