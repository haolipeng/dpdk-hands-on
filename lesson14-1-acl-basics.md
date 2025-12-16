# Lesson 14-1: DPDK ACL（访问控制列表）入门

## 课程简介

ACL（Access Control List，访问控制列表）是 DPDK 中用于高性能数据包分类和过滤的核心组件。本课程介绍 ACL 的基础概念、数据结构和工作原理。

**学习目标：**
- 理解 ACL 的应用场景和优势
- 掌握 IPv4 5 元组匹配的原理
- 熟悉 ACL 的数据结构设计
- 了解优先级和通配符的使用
- 为实战编程打好理论基础

**前置知识：**
- 完成 Lesson 1-5（EAL 初始化、基本数据结构）
- 理解 TCP/IP 协议栈基础（IP 地址、端口、协议号）
- 了解网络数据包的基本结构

**应用场景：**
- 防火墙规则（允许/拒绝特定流量）
- QoS 分类（根据流量特征分配优先级）
- DDoS 防护（识别和过滤恶意流量）
- 流量统计（按规则分类统计流量）

---

## 一、为什么需要 ACL？

### 1.1 传统方式的问题

在数据包处理中，如果使用传统的线性查找或 if-else 链来匹配流量规则，会遇到严重的性能问题：

```c
// 传统方式：线性查找规则
for (int i = 0; i < num_rules; i++) {
    if (pkt->src_ip == rules[i].src_ip &&
        pkt->dst_ip == rules[i].dst_ip &&
        pkt->src_port == rules[i].src_port &&
        pkt->dst_port == rules[i].dst_port &&
        pkt->proto == rules[i].proto) {
        return rules[i].action;  // ❌ O(n) 复杂度
    }
}
```

**主要问题：**

| 问题 | 说明 | 影响 |
|------|------|------|
| **O(n) 查找时间** | 规则越多，查找越慢 | 无法扩展到大量规则 |
| **无法优化** | 每个数据包都要遍历��有规则 | CPU 占用高 |
| **维护困难** | 手写 if-else 链难以维护 | 代码复杂，易出错 |
| **不支持范围/通配符** | 需要为每个具体值写规则 | 规则数量爆炸 |

### 1.2 ACL 的优势

DPDK ACL 使用优化的 **trie（字典树）结构**，提供高性能的多字段匹配：

```
传统线性查找：                 DPDK ACL (trie 结构)：
┌─────────────┐              ┌──────────────────┐
│   规则 1    │              │    Trie Root     │
├─────────────┤              ├────┬────┬────────┤
│   规则 2    │              │ L1 │ L2 │  ...   │
├─────────────┤              ├────┴────┴────────┤
│   规则 3    │ → 遍历       │  快速多字段查找   │
├─────────────┤              │  O(1) 平均时间   │
│     ...     │              └──────────────────┘
├─────────────┤                    ↓
│   规则 N    │              匹配结果（userdata）
└─────────────┘
时间: O(n)                   时间: O(1) ~ O(log n)
```

**ACL 的核心优势：**

1. ✅ **多字段匹配** - 同时匹配源 IP、目的 IP、源端口、目的端口、协议（5 元组）
2. ✅ **高性能查找** - 使用 trie 结构，平均 O(1) 查找时间
3. ✅ **优先级支持** - 多条规则匹配时，自动选择优先级最高的规则
4. ✅ **通配符和范围** - 支持 IP 子网掩码（CIDR）和端口范围
5. ✅ **批量分类** - 一次分类多个数据包，提高吞吐量

### 1.3 典型应用场景对比

| 场景 | 传统方式 | DPDK ACL | 性能提升 |
|------|---------|----------|---------|
| **简单防火墙** | if-else 链 | 5 元组规则 | 10-100x |
| **QoS 分类** | 线性查找 | 多 category | 50-200x |
| **DDoS 防护** | Hash 表 + 遍历 | 优先级规则 | 20-100x |
| **流量统计** | 多次 Hash 查找 | 批量分类 | 10-50x |

---

## 二、核心概念

### 2.1 五元组（5-Tuple）

五元组是唯一标识一个网络流的 5 个关键字段：

```
网络数据包：
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ 源 IP    │ 目的 IP  │ 源端口   │ 目的端口 │  协议    │
└──────────┴──────────┴──────────┴──────────┴──────────┘
│                                                        │
└──────────────── 五元组（5-Tuple） ────────────────────┘

示例：
192.168.1.10 → 10.0.0.1, Port 12345 → 80, Protocol TCP(6)
```

**字段说明：**

| 字段 | 大小 | 说明 | 示例 |
|------|------|------|------|
| **源 IP** | 4 字节 | 数据包的源 IP 地址 | 192.168.1.10 |
| **目的 IP** | 4 字节 | 数据包的目的 IP 地址 | 10.0.0.1 |
| **源端口** | 2 字节 | TCP/UDP 源端口 | 12345 |
| **目的端口** | 2 字节 | TCP/UDP 目的端口 | 80 (HTTP) |
| **协议** | 1 字节 | IP 协议号（TCP=6, UDP=17） | 6 (TCP) |

### 2.2 ACL 规则结构

一条 ACL 规则由 **匹配字段** 和 **动作** 组成：

```
规则示例：允许来自 192.168.1.0/24 的 HTTP 流量

┌─────────────────────────────────────────────────────┐
│                     匹配条件                        │
├─────────────┬─────────────┬─────────────────────────┤
│ 源 IP       │ 192.168.1.0/24  （子网掩码）         │
│ 目的 IP     │ 0.0.0.0/0       （任意地址）         │
│ 源端口      │ 0-65535         （任意端口）         │
│ 目的端口    │ 80              （HTTP）             │
│ 协议        │ 6               （TCP）              │
├─────────────┴─────────────┴─────────────────────────┤
│ 优先级      │ 100             （高优先级）         │
│ 动作        │ ALLOW           （允许通过）         │
└─────────────────────────────────────────────────────┘
```

**规则字段说明：**

- **匹配条件**：定义什么样的数据包会匹配这条规则
- **优先级**：数字越大，优先级越高（1-65535）
- **动作**：匹配后的处理（通常是 ALLOW 或 DENY，存储在 userdata 中）

### 2.3 优先级机制

当一个数据包匹配多条规则时，**优先级最高的规则获胜**：

```
数据包：192.168.1.10:12345 → 10.0.0.1:80 (TCP)

匹配的规则：
┌────────┬─────────────────────────────┬─────────┐
│ 优先级 │         规则内容             │  动作   │
├────────┼─────────────────────────────┼─────────┤
│  100   │ 允许 HTTP from 192.168.1.0/24 │ ALLOW   │ ← 优先级高，被选中
│   50   │ 允许高端口 (1024-65535)       │ ALLOW   │
│   10   │ 默认拒绝所有                 │ DENY    │
└────────┴─────────────────────────────┴─────────┘
         ↓
    最终结果：ALLOW（选择优先级 100 的规则）
```

**优先级冲突解决规则：**

1. 数字越大，优先级越高
2. 当多条规则匹配时，只返回优先级最高的规则
3. 优先级相同时，行为未定义（应避免）
4. 通常设置"默认拒绝"规则为最低优先级

### 2.4 匹配类型

ACL 支持三种字段匹配类型：

#### 2.4.1 MASK（掩码匹配）

用于 IP 地址的子网匹配，支持 CIDR 表示法：

```
匹配类型：MASK
字段：IP 地址（4 字节）

示例 1：匹配单个 IP
  192.168.1.10/32  → 只匹配 192.168.1.10

示例 2：匹配子网
  192.168.1.0/24   → 匹配 192.168.1.0 - 192.168.1.255

示例 3：匹配所有 IP
  0.0.0.0/0        → 匹配任意 IP 地址
```

**mask_range 字段**：存储 CIDR 的位数（0-32）

#### 2.4.2 RANGE（范围匹配）

用于端口的范围匹配：

```
匹配类型：RANGE
字段：端口（2 字节）

示例 1：匹配单个端口
  value = 80, mask_range = 80   → 只匹配端口 80

示例 2：匹配端口范围
  value = 1024, mask_range = 65535   → 匹配 1024-65535

示例 3：匹配任意端口
  value = 0, mask_range = 65535   → 匹配所有端口
```

**mask_range 字段**：存储范围的上界

#### 2.4.3 BITMASK（位掩码匹配）

用于协议字段的精确或多值匹配：

```
匹配类型：BITMASK
字段：协议（1 字节）

示例 1：精确匹配 TCP
  value = 6, mask_range = 0xFF   → 只匹配 TCP (6)

示例 2：匹配 TCP 或 UDP
  value = 6 | 17, mask_range = ...   → 匹配 TCP 或 UDP

示例 3：匹配任意协议
  value = 0, mask_range = 0   → 匹配所有协议
```

### 2.5 分类类别（Categories）

ACL 支持 **多个独立的分类器**，称为 category：

```
单个 category（常用）：
┌─────────────┐
│  Category 0 │ → 所有规则在同一分类器
│  规则 1-N   │
└─────────────┘

多个 category（高级）：
┌─────────────┐  ┌─────────────┐
│  Category 0 │  │  Category 1 │
│  入站规则   │  │  出站规则   │
└─────────────┘  └─────────────┘
       ↓                ↓
   入站匹配          出站匹配
```

**使用场景：**
- **单 category**：大多数场景（防火墙、QoS）
- **多 category**：需要同时应用多组规则（入站+出站过滤）

---

## 三、数据结构详解

### 3.1 字段定义结构 `rte_acl_field_def`

定义 5 元组中每个字段的属性：

```c
struct rte_acl_field_def {
    uint8_t type;          // 字段类型：MASK/RANGE/BITMASK
    uint8_t size;          // 字段大小：1/2/4/8 字节
    uint8_t field_index;   // 字段索引（0-N）
    uint8_t input_index;   // 分组索引（4 字节对齐）
    uint32_t offset;       // 字段在结构体中的偏移量
};
```

**字段说明：**

| 字段 | 说明 | 示例 |
|------|------|------|
| `type` | 匹配类型 | `RTE_ACL_FIELD_TYPE_MASK` (IP) |
| `size` | 字段大小 | `sizeof(uint32_t)` (4 字节) |
| `field_index` | 字段编号 | 0, 1, 2, 3, 4 |
| `input_index` | **4 字节对齐分组** | 协议(0), 源IP(1), 目的IP(2), 端口(3) |
| `offset` | 内存偏移量 | `offsetof(struct ipv4_5tuple, proto)` |

**关键点：input_index 的作用**

DPDK ACL 要求字段按 **4 字节边界分组**：

```
5 元组内存布局：
┌─────────┬─────────┬─────────┬─────────┬─────────┐
│  proto  │ src_ip  │ dst_ip  │src_port │dst_port │
│ (1字节) │(4字节)  │(4字节)  │(2字节)  │(2字节)  │
└─────────┴─────────┴─────────┴─────────┴─────────┘
input_index:
    0         1         2         3         3
    └─ 1B ─┘ └─ 4B ──┘ └─ 4B ──┘ └──── 4B ─────┘
                                  (2B + 2B = 4B)
```

**源端口和目的端口共享 `input_index = 3`**，因为它们合起来是 4 字节。

### 3.2 IPv4 五元组结构体

```c
struct ipv4_5tuple {
    uint8_t  proto;      // 协议（1 字节）
    uint32_t ip_src;     // 源 IP（4 字节）
    uint32_t ip_dst;     // 目的 IP（4 字节）
    uint16_t port_src;   // 源端口（2 字节）
    uint16_t port_dst;   // 目的端口（2 字节）
} __rte_packed;          // 紧凑排列，无填充字节
```

**内存布局（13 字节）：**

```
偏移量:  0    1         5         9    11   13
        ┌────┬─────────┬─────────┬─────┬─────┐
        │proto│ ip_src │ ip_dst  │src_p│dst_p│
        └────┴─────────┴─────────┴─────┴─────┘
         1B     4B        4B       2B    2B
```

### 3.3 ACL 规则结构

使用宏定义规则结构体：

```c
RTE_ACL_RULE_DEF(acl_ipv4_rule, NUM_FIELDS_IPV4);

// 展开后相当于：
struct acl_ipv4_rule {
    struct rte_acl_rule_data data;  // 元数据
    struct rte_acl_field field[5];  // 5 个字段
};
```

**元数据（`rte_acl_rule_data`）：**

```c
struct rte_acl_rule_data {
    uint32_t category_mask;  // 分类器掩码（位 0 = category 0）
    int32_t priority;        // 优先级（越大越高）
    uint32_t userdata;       // 用户自定义数据（动作：ALLOW/DENY）
};
```

**字段数据（`rte_acl_field`）：**

```c
struct rte_acl_field {
    union {
        uint8_t  u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
    } value;        // 字段值（主机字节序）

    union {
        uint8_t  u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
    } mask_range;   // 掩码或范围（取决于 type）
};
```

### 3.4 ACL 配置结构

```c
struct rte_acl_config {
    uint32_t num_categories;            // category 数量（通常为 1）
    uint32_t num_fields;                // 字段数量（5 元组 = 5）
    struct rte_acl_field_def defs[...]; // 字段定义数组
};
```

---

## 四、核心 API 概览

本节简要介绍 ACL 的核心 API。详细的使用方法和代码示例请参考 **Lesson 14-2: ACL实战应用**。

### 4.1 创建 ACL 上下文：`rte_acl_create()`

**函数原型：**

```c
struct rte_acl_ctx *
rte_acl_create(const struct rte_acl_param *param);
```

**参数说明：**

```c
struct rte_acl_param {
    const char *name;      // ACL 名称（唯一）
    int socket_id;         // NUMA socket ID（rte_socket_id()）
    uint32_t rule_size;    // 单条规则大小（RTE_ACL_RULE_SZ(5)）
    uint32_t max_rule_num; // 最大规则数量
};
```

**功能**：创建 ACL 上下文，分配内存和初始化数据结构。

### 4.2 添加规则：`rte_acl_add_rules()`

**函数原型：**

```c
int rte_acl_add_rules(struct rte_acl_ctx *ctx,
                      const struct rte_acl_rule *rules,
                      uint32_t num);
```

**功能**：向 ACL 上下文添加一条或多条规则。规则字段使用 **主机字节序**。

### 4.3 构建 ACL：`rte_acl_build()`

**函数原型：**

```c
int rte_acl_build(struct rte_acl_ctx *ctx,
                  const struct rte_acl_config *cfg);
```

**功能**：构建优化的 trie 结构。**必须在添加完所有规则后调用**，构建后不能再添加规则。

### 4.4 分类数据包：`rte_acl_classify()`

**函数原型：**

```c
int rte_acl_classify(const struct rte_acl_ctx *ctx,
                     const uint8_t **data,
                     uint32_t *results,
                     uint32_t num,
                     uint32_t categories);
```

**功能**：对数据包进行分类，返回匹配规则的 `userdata`。数据包数据使用 **网络字节序**。

---

## 总结

本课程介绍了 DPDK ACL 的核心概念和数据结构：

### 关键知识点

1. **为什么需要 ACL**
   - 传统线性查找的性能问题（O(n) 复杂度）
   - ACL 使用 trie 结构实现高性能查找（O(1) 平均时间）
   - 性能提升 10-200 倍

2. **五元组匹配**
   - src_ip, dst_ip, src_port, dst_port, protocol
   - 唯一标识一个网络流

3. **三种匹配类型**
   - MASK（IP子网匹配，CIDR）
   - RANGE（端口范围匹配）
   - BITMASK（协议精确匹配）

4. **优先级机制**
   - 数字越大优先级越高
   - 多条规则匹配时自动选择最高优先级

5. **关键数据结构**
   - `rte_acl_field_def`：字段定义
   - `ipv4_5tuple`：五元组数据
   - `acl_ipv4_rule`：规则结构
   - `rte_acl_config`：ACL 配置

6. **input_index 重要概念**
   - 字段按 4 字节边界分组
   - 源端口和目的端口共享同一 input_index

### 字节序注意事项

- **规则字段**：使用 **主机字节序**（不需要 htonl/htons）
- **数据包数据**：使用 **网络字节序**（需要 htonl/htons 转换）

### 下一步学习

在 **Lesson 14-2: ACL实战应用** 中，你将学习：

- ✅ 完整的 API 使用方法（含详细代码示例）
- ✅ 如何构造 ACL 规则
- ✅ 运行完整的防火墙演示程序
- ✅ 编译、运行、调试 ACL 应用
- ✅ 分类数据包并分析结果

**继续学习**: [lesson14-2-acl-practice.md](lesson14-2-acl-practice.md)

---

**参考资料：**

- [DPDK 官方文档：Packet Classification and Access Control](https://doc.dpdk.org/guides/prog_guide/packet_classif_access_ctrl.html)
- [DPDK API 参考：rte_acl.h](https://doc.dpdk.org/api/rte__acl_8h.html)
