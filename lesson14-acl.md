# Lesson 14: DPDK ACL（访问控制列表）详解

## 课程简介

ACL（Access Control List，访问控制列表）是 DPDK 中用于高性能数据包分类和过滤的核心组件。本课程通过实践演示 ACL 的使用方法，帮助你快速掌握基于 5 元组的数据包过滤技术。

**学习目标：**
- 理解 ACL 的应用场景和优势
- 掌握 IPv4 5 元组匹配的原理
- 学会创建、配置和使用 ACL
- 理解优先级和通配符的使用
- 能够实现简单的防火墙功能

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
| **无法优化** | 每个数据包都要遍历所有规则 | CPU 占用高 |
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

## 四、核心 API 详解

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

| 参数 | 说明 | 推荐值 |
|------|------|--------|
| `name` | ACL 上下文名称 | "ipv4_acl" |
| `socket_id` | NUMA 节点 | `rte_socket_id()` |
| `rule_size` | 规则大小 | `RTE_ACL_RULE_SZ(5)` |
| `max_rule_num` | 最大规则数 | 根据需求（如 100） |

**返回值：**
- 成功：ACL 上下文指针
- 失败：NULL

**示例代码：**

```c
struct rte_acl_param acl_param = {
    .name = "ipv4_acl",
    .socket_id = rte_socket_id(),
    .rule_size = RTE_ACL_RULE_SZ(NUM_FIELDS_IPV4),  // 5 字段
    .max_rule_num = 100,
};

struct rte_acl_ctx *acl_ctx = rte_acl_create(&acl_param);
if (acl_ctx == NULL) {
    rte_exit(EXIT_FAILURE, "无法创建 ACL 上下文\n");
}
```

### 4.2 添加规则：`rte_acl_add_rules()`

**函数原型：**

```c
int rte_acl_add_rules(struct rte_acl_ctx *ctx,
                      const struct rte_acl_rule *rules,
                      uint32_t num);
```

**参数说明：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `ctx` | `struct rte_acl_ctx *` | ACL 上下文 |
| `rules` | `const struct rte_acl_rule *` | 规则数组 |
| `num` | `uint32_t` | 规则数量 |

**返回值：**
- 成功：0
- 失败：负数错误码

**示例代码：允许 HTTP 流量**

```c
struct acl_ipv4_rule rule;
memset(&rule, 0, sizeof(rule));

// 设置元数据
rule.data.category_mask = 1;      // Category 0
rule.data.priority = 100;         // 高优先级
rule.data.userdata = ACL_ALLOW;   // 动作：允许

// 字段 0：协议 = TCP (6)
rule.field[PROTO_FIELD_IPV4].value.u8 = IPPROTO_TCP;
rule.field[PROTO_FIELD_IPV4].mask_range.u8 = 0xFF;  // 精确匹配

// 字段 1：源 IP = 192.168.1.0/24（主机字节序！）
rule.field[SRC_FIELD_IPV4].value.u32 = RTE_IPV4(192, 168, 1, 0);
rule.field[SRC_FIELD_IPV4].mask_range.u32 = 24;  // CIDR /24

// 字段 2：目的 IP = 任意
rule.field[DST_FIELD_IPV4].value.u32 = 0;
rule.field[DST_FIELD_IPV4].mask_range.u32 = 0;  // 匹配所有

// 字段 3：源端口 = 任意
rule.field[SRCP_FIELD_IPV4].value.u16 = 0;
rule.field[SRCP_FIELD_IPV4].mask_range.u16 = 65535;  // 范围 0-65535

// 字段 4：目的端口 = 80
rule.field[DSTP_FIELD_IPV4].value.u16 = 80;
rule.field[DSTP_FIELD_IPV4].mask_range.u16 = 80;  // 范围 80-80（精确）

// 添加规则
int ret = rte_acl_add_rules(acl_ctx, (struct rte_acl_rule *)&rule, 1);
if (ret != 0) {
    rte_exit(EXIT_FAILURE, "添加规则失败: %s\n", strerror(-ret));
}
```

**关键点：**
- 规则字段值使用 **主机字节序**（不需要 htonl/htons）
- MASK 类型：`mask_range` 是 CIDR 位数（0-32）
- RANGE 类型：`mask_range` 是范围上界
- `userdata` 可以存储任意 32 位值（如 ALLOW/DENY）

### 4.3 构建 ACL：`rte_acl_build()`

**函数原型：**

```c
int rte_acl_build(struct rte_acl_ctx *ctx,
                  const struct rte_acl_config *cfg);
```

**参数说明：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `ctx` | `struct rte_acl_ctx *` | ACL 上下文 |
| `cfg` | `const struct rte_acl_config *` | ACL 配置（字段定义） |

**返回值：**
- 成功：0
- 失败：负数错误码

**示例代码：**

```c
struct rte_acl_config cfg;
setup_acl_config(&cfg);  // 配置字段定义

int ret = rte_acl_build(acl_ctx, &cfg);
if (ret != 0) {
    rte_exit(EXIT_FAILURE, "构建 ACL 失败: %s\n", strerror(-ret));
}
```

**关键点：**
- **必须在添加所有规则后调用**
- 构建后会创建优化的 trie 结构
- 构建后不能再添加规则（除非先 `rte_acl_reset()`）
- 这是一个相对耗时的操作，但只需执行一次

### 4.4 分类数据包：`rte_acl_classify()`

**函数原型：**

```c
int rte_acl_classify(const struct rte_acl_ctx *ctx,
                     const uint8_t **data,
                     uint32_t *results,
                     uint32_t num,
                     uint32_t categories);
```

**参数说明：**

| 参数 | 类型 | 说明 |
|------|------|------|
| `ctx` | `const struct rte_acl_ctx *` | ACL 上下文 |
| `data` | `const uint8_t **` | 数据包指针数组 |
| `results` | `uint32_t *` | 结果数组（存储 userdata） |
| `num` | `uint32_t` | 数据包数量 |
| `categories` | `uint32_t` | category 数量（通常为 1） |

**返回值：**
- 成功：0
- 失败：负数错误码

**示例代码：分类单个数据包**

```c
// 准备测试数据包（网络字节序！）
struct ipv4_5tuple pkt;
pkt.proto = IPPROTO_TCP;
pkt.ip_src = htonl(RTE_IPV4(192, 168, 1, 10));  // 注意：网络字节序
pkt.ip_dst = htonl(RTE_IPV4(10, 0, 0, 1));
pkt.port_src = htons(12345);
pkt.port_dst = htons(80);

// 准备数据指针和结果数组
const uint8_t *data[1] = { (uint8_t *)&pkt };
uint32_t results[1] = { 0 };

// 分类
int ret = rte_acl_classify(acl_ctx, data, results, 1, 1);
if (ret != 0) {
    rte_exit(EXIT_FAILURE, "分类失败: %s\n", strerror(-ret));
}

// 检查结果
if (results[0] == ACL_ALLOW) {
    printf("数据包被允许\n");
} else if (results[0] == ACL_DENY) {
    printf("数据包被拒绝\n");
} else {
    printf("无匹配规则\n");
}
```

**批量分类示例：**

```c
#define BATCH_SIZE 32
struct ipv4_5tuple packets[BATCH_SIZE];
const uint8_t *data[BATCH_SIZE];
uint32_t results[BATCH_SIZE];

// 准备批量数据
for (int i = 0; i < BATCH_SIZE; i++) {
    data[i] = (uint8_t *)&packets[i];
}

// 批量分类（更高性能）
rte_acl_classify(acl_ctx, data, results, BATCH_SIZE, 1);

// 处理结果
for (int i = 0; i < BATCH_SIZE; i++) {
    printf("数据包 %d: %s\n", i,
           results[i] == ACL_ALLOW ? "允许" : "拒绝");
}
```

**关键点：**
- 数据包数据必须使用 **网络字节序**（与规则的主机字节序相反！）
- 使用 `htonl()` 和 `htons()` 转换字节序
- 批量分类比单个分类效率更高（推荐 32-64 个数据包）
- `results` 数组包含匹配规则的 `userdata` 值
- 如果无匹配规则，`results[i]` 为 0

---

## 五、完整示例代码

完整示例代码位于 [14-acl/acl_demo.c](14-acl/acl_demo.c)。

### 5.1 程序流程

```
┌─────────────────────┐
│  1. 初始化 EAL      │
└──────────┬──────────┘
           ↓
┌─────────────────────┐
│  2. 创建 ACL 上下文 │ → rte_acl_create()
└──────────┬──────────┘
           ↓
┌─────────────────────┐
│  3. 添加规则        │ → rte_acl_add_rules()
│     - 规则 1-5      │
└──────────┬──────────┘
           ↓
┌─────────────────────┐
│  4. 构建 ACL        │ → rte_acl_build()
│     (创建 trie)     │
└──────────┬──────────┘
           ↓
┌─────────────────────┐
│  5. 创建测试数据包  │
│     - 5 个场景      │
└──────────┬──────────┘
           ↓
┌─────────────────────┐
│  6. 分类数据包      │ → rte_acl_classify()
└──────────┬──────────┘
           ↓
┌─────────────────────┐
│  7. 打印结果        │
└──────────┬──────────┘
           ↓
┌─────────────────────┐
│  8. 清理资源        │ → rte_acl_free()
└─────────────────────┘
```

### 5.2 字段定义配置（acl_demo.c:58-109）

字段定义是 ACL 的核心配置，决定了如何解析 5 元组：

```c
static struct rte_acl_field_def ipv4_defs[NUM_FIELDS_IPV4] = {
    // 字段 0：协议（1 字节）
    {
        .type = RTE_ACL_FIELD_TYPE_BITMASK,
        .size = sizeof(uint8_t),
        .field_index = PROTO_FIELD_IPV4,
        .input_index = 0,
        .offset = offsetof(struct ipv4_5tuple, proto),
    },
    // 字段 1：源 IP（4 字节）
    {
        .type = RTE_ACL_FIELD_TYPE_MASK,
        .size = sizeof(uint32_t),
        .field_index = SRC_FIELD_IPV4,
        .input_index = 1,
        .offset = offsetof(struct ipv4_5tuple, ip_src),
    },
    // 字段 2：目的 IP（4 字节）
    {
        .type = RTE_ACL_FIELD_TYPE_MASK,
        .size = sizeof(uint32_t),
        .field_index = DST_FIELD_IPV4,
        .input_index = 2,
        .offset = offsetof(struct ipv4_5tuple, ip_dst),
    },
    // 字段 3：源端口（2 字节）
    {
        .type = RTE_ACL_FIELD_TYPE_RANGE,
        .size = sizeof(uint16_t),
        .field_index = SRCP_FIELD_IPV4,
        .input_index = 3,
        .offset = offsetof(struct ipv4_5tuple, port_src),
    },
    // 字段 4：目的端口（2 字节，与源端口共享 input_index）
    {
        .type = RTE_ACL_FIELD_TYPE_RANGE,
        .size = sizeof(uint16_t),
        .field_index = DSTP_FIELD_IPV4,
        .input_index = 3,  // ← 与源端口共享
        .offset = offsetof(struct ipv4_5tuple, port_dst),
    },
};
```

### 5.3 规则定义（acl_demo.c:177-230）

演示 5 条规则，展示不同的匹配特性：

| 规则 | 优先级 | 匹配条件 | 动作 | 演示特性 |
|------|--------|---------|------|---------|
| 规则 1 | 100 | HTTP (80) from 192.168.1.0/24 | ALLOW | 子网匹配 |
| 规则 2 | 90 | SSH (22) from any | DENY | 端口精确匹配 |
| 规则 3 | 80 | DNS (UDP 53) from any | ALLOW | 协议区分 |
| 规则 4 | 50 | 端口 1024-65535 | ALLOW | 端口范围 |
| 规则 5 | 10 | 所有流量 | DENY | 默认拒绝 |

### 5.4 测试数据包（acl_demo.c:256-301）

5 个测试场景：

```c
// 数据包 1：HTTP from 192.168.1.10 → 应该允许（规则 1）
packets[0].proto = IPPROTO_TCP;
packets[0].ip_src = htonl(RTE_IPV4(192, 168, 1, 10));   // 网络字节序
packets[0].ip_dst = htonl(RTE_IPV4(10, 0, 0, 1));
packets[0].port_src = htons(12345);
packets[0].port_dst = htons(80);

// 数据包 2：SSH from 10.0.0.5 → 应该拒绝（规则 2）
packets[1].port_dst = htons(22);

// 数据包 3：DNS query → 应该允许（规则 3）
packets[2].proto = IPPROTO_UDP;
packets[2].port_dst = htons(53);

// 数据包 4：高端口连接 → 应该允许（规则 4）
packets[3].port_dst = htons(8080);

// 数据包 5：低端口未知服务 → 应该拒绝（规则 5）
packets[4].port_dst = htons(445);
```

### 5.5 关键代码片段

**构造规则的辅助函数（acl_demo.c:144-173）：**

```c
static void
make_rule(struct acl_ipv4_rule *rule, uint32_t priority, uint32_t userdata,
          uint8_t proto, uint8_t proto_mask,
          uint32_t src_ip, uint32_t src_mask_len,
          uint32_t dst_ip, uint32_t dst_mask_len,
          uint16_t src_port_low, uint16_t src_port_high,
          uint16_t dst_port_low, uint16_t dst_port_high)
{
    memset(rule, 0, sizeof(*rule));
    rule->data.category_mask = 1;
    rule->data.priority = priority;
    rule->data.userdata = userdata;

    // 填充 5 个字段...
}
```

**批量分类（acl_demo.c:306-368）：**

```c
const uint8_t *data[NUM_TEST_PACKETS];
uint32_t results[NUM_TEST_PACKETS] = {0};

// 准备数据指针
for (i = 0; i < num_packets; i++) {
    data[i] = (const uint8_t *)&packets[i];
}

// 批量分类
rte_acl_classify(ctx, data, results, num_packets, 1);

// 处理结果
for (i = 0; i < num_packets; i++) {
    if (results[i] == ACL_ALLOW) {
        printf("  数据包%d: ... → 允许\n", i + 1);
    } else {
        printf("  数据包%d: ... → 拒绝\n", i + 1);
    }
}
```

---

## 六、编译和运行

### 6.1 编译

```bash
# 进入构建目录
cd /home/work/clionProject/dpdk-hands-on/build

# 配置 CMake（如果尚未配置）
cmake ..

# 编译 ACL 演示程序
make acl_demo

# 检查可执行文件
ls -lh ../bin/acl_demo
```

**编译输出：**

```
[ 95%] Building C object 14-acl/CMakeFiles/acl_demo.dir/acl_demo.c.o
[100%] Linking C executable ../bin/acl_demo
[100%] Built target acl_demo
```

### 6.2 运行

ACL 演示程序不需要网卡，可以使用 `--no-pci` 参数运行：

```bash
# 运行（需要 sudo 权限）
sudo ../bin/acl_demo -l 0 --no-pci

# 或者指定更多核心（用于多线程扩展）
sudo ../bin/acl_demo -l 0-1 --no-pci
```

**预期输出：**

```
=== DPDK ACL 演示：IPv4防火墙 ===

[步骤 1] 创建ACL上下文...
  ✓ 成功创建ACL上下文: ipv4_acl

[步骤 2] 添加防火墙规则...
  规则1: 允许 HTTP (端口80) 来自 192.168.1.0/24 [优先级 100]
  规则2: 拒绝  SSH (端口22) 来自任意地址    [优先级 90]
  规则3: 允许  DNS (UDP端口53) 来自任意地址  [优先级 80]
  规则4: 允许  高端口范围 (1024-65535)      [优先级 50]
  规则5: 拒绝  所有其他流量（默认拒绝）      [优先级 10]
  ✓ 成功添加 5 条规则

[步骤 3] 构建ACL...
  ✓ ACL构建成功

[步骤 4] 分类测试数据包...
  数据包1: 192.168.1.10:12345 → 10.0.0.1:80    (TCP)  → 允许 (规则1)
  数据包2: 10.0.0.5:54321     → 10.0.0.1:22    (TCP)  → 拒绝  (规则2)
  数据包3: 8.8.8.8:33445      → 10.0.0.1:53    (UDP)  → 允许 (规则3)
  数据包4: 172.16.0.100:44556 → 10.0.0.1:8080  (TCP)  → 允许 (规则4)
  数据包5: 203.0.113.5:11223  → 10.0.0.1:445   (TCP)  → 拒绝  (规则5)

[统计信息]
  总数据包: 5
  允许: 3
  拒绝: 2

[清理]
  ✓ ACL上下文已释放
  ✓ EAL已清理

=== 演示结束 ===
```

### 6.3 结果分析

| 数据包 | 匹配规则 | 结果 | 原因 |
|--------|---------|------|------|
| 1 | 规则 1 | 允许 | 来自 192.168.1.0/24 的 HTTP 流量 |
| 2 | 规则 2 | 拒绝 | SSH 端口 22 被拒绝 |
| 3 | 规则 3 | 允许 | DNS 查询（UDP 端口 53） |
| 4 | 规则 4 | 允许 | 高端口范围 8080 ∈ [1024, 65535] |
| 5 | 规则 5 | 拒绝 | 默认拒绝（端口 445 < 1024） |

---

## 七、进阶话题

### 7.1 性能优化

#### 7.1.1 批量分类

单个数据包分类的开销较大，建议使用批量分类：

```c
#define BURST_SIZE 32

struct ipv4_5tuple packets[BURST_SIZE];
const uint8_t *data[BURST_SIZE];
uint32_t results[BURST_SIZE];

// 准备批量数据
for (int i = 0; i < BURST_SIZE; i++) {
    data[i] = (uint8_t *)&packets[i];
}

// 一次分类 32 个数据包（性能提升 10-20x）
rte_acl_classify(acl_ctx, data, results, BURST_SIZE, 1);
```

**性能对比：**

| 方式 | 吞吐量 | CPU 占用 |
|------|--------|---------|
| 单个分类 | ~1 Mpps | 100% |
| 批量分类（32） | ~15 Mpps | 100% |
| 提升倍数 | **15x** | - |

#### 7.1.2 规则排序优化

将最常匹配的规则设置为高优先级，减少平均查找时间：

```
❌ 差的排序：
  规则 1（优先级 100）：极少匹配的规则
  规则 2（优先级 90）： 常见规则
  规则 3（优先级 80）： 最常见规则

✅ 好的排序：
  规则 1（优先级 100）：最常见规则（HTTP）
  规则 2（优先级 90）： 常见规则（HTTPS）
  规则 3（优先级 80）： 不常见规则
```

#### 7.1.3 NUMA 感知

确保 ACL 上下文创建在数据处理的同一 NUMA 节点：

```c
struct rte_acl_param acl_param = {
    .name = "ipv4_acl",
    .socket_id = rte_socket_id(),  // ← 当前 lcore 的 NUMA 节点
    // ...
};
```

### 7.2 多分类器应用

使用多个 category 实现独立的规则集：

```c
// 配置两个 category
struct rte_acl_config cfg;
cfg.num_categories = 2;  // Category 0: 入站, Category 1: 出站

// 添加入站规则
rule1.data.category_mask = 0x1;  // Bit 0 = Category 0
rte_acl_add_rules(acl_ctx, &rule1, 1);

// 添加出站规则
rule2.data.category_mask = 0x2;  // Bit 1 = Category 1
rte_acl_add_rules(acl_ctx, &rule2, 1);

// 分类时指定 category 数量
rte_acl_classify(acl_ctx, data, results, num, 2);  // 2 categories
```

**结果数组布局：**

```
results[] = [
    results[0],  // Category 0 结果（入站）
    results[1],  // Category 1 结果（出站）
    results[2],  // 下一个数据包的 Category 0
    results[3],  // 下一个数据包的 Category 1
    // ...
];
```

### 7.3 通配符和范围技巧

#### 7.3.1 匹配任意 IP

```c
// 匹配所有源 IP
rule.field[SRC_FIELD_IPV4].value.u32 = 0;
rule.field[SRC_FIELD_IPV4].mask_range.u32 = 0;  // CIDR /0
```

#### 7.3.2 匹配特定子网

```c
// 匹配 10.0.0.0/8（10.0.0.0 - 10.255.255.255）
rule.field[SRC_FIELD_IPV4].value.u32 = RTE_IPV4(10, 0, 0, 0);
rule.field[SRC_FIELD_IPV4].mask_range.u32 = 8;

// 匹配 192.168.1.0/24（192.168.1.0 - 192.168.1.255）
rule.field[SRC_FIELD_IPV4].value.u32 = RTE_IPV4(192, 168, 1, 0);
rule.field[SRC_FIELD_IPV4].mask_range.u32 = 24;
```

#### 7.3.3 匹配端口范围

```c
// 匹配高端口（1024-65535）
rule.field[DSTP_FIELD_IPV4].value.u16 = 1024;
rule.field[DSTP_FIELD_IPV4].mask_range.u16 = 65535;

// 匹配特定范围（8000-9000）
rule.field[DSTP_FIELD_IPV4].value.u16 = 8000;
rule.field[DSTP_FIELD_IPV4].mask_range.u16 = 9000;
```

#### 7.3.4 匹配多个协议

使用位掩码匹配 TCP 或 UDP：

```c
// 匹配 TCP (6) 或 UDP (17)
rule.field[PROTO_FIELD_IPV4].value.u8 = 6 | 17;  // 不推荐
// 推荐：创建两条独立规则
```

### 7.4 动态更新规则

ACL 构建后不能直接添加规则，需要重置：

```c
// 重置 ACL（清空所有规则）
rte_acl_reset(acl_ctx);

// 添加新规则
rte_acl_add_rules(acl_ctx, new_rules, num_new);

// 重新构建
rte_acl_build(acl_ctx, &cfg);
```

**注意：** 重新构建会短暂阻塞分类操作，适合规则更新频率低的场景。

---

## 八、常见问题

### Q1: 构建 ACL 失败，提示 "Invalid argument"

**原因：** 忘记调用 `rte_acl_build()` 或字段定义错误。

**解决：**
```c
// 确保添加规则后调用 build
rte_acl_add_rules(acl_ctx, rules, num);
rte_acl_build(acl_ctx, &cfg);  // ← 必须调用
```

### Q2: 数据包无法匹配规则

**原因 1：** 字节序错误（规则用主机序，数据包用网络序）。

```c
// ❌ 错误：数据包用主机序
pkt.ip_src = RTE_IPV4(192, 168, 1, 10);

// ✅ 正确：数据包用网络序
pkt.ip_src = htonl(RTE_IPV4(192, 168, 1, 10));
```

**原因 2：** `input_index` 分组错误。

```c
// ❌ 错误：两个端口字段 input_index 不同
{.field_index = 3, .input_index = 3, ...},  // 源端口
{.field_index = 4, .input_index = 4, ...},  // 目的端口

// ✅ 正确：两个端口共享 input_index
{.field_index = 3, .input_index = 3, ...},  // 源端口
{.field_index = 4, .input_index = 3, ...},  // 目的端口（同 3）
```

### Q3: 性能不如预期

**原因：** 使用单个数据包分类，未使用批量分类。

**解决：**
```c
// ❌ 慢：逐个分类
for (int i = 0; i < n; i++) {
    rte_acl_classify(acl_ctx, &data[i], &results[i], 1, 1);
}

// ✅ 快：批量分类
rte_acl_classify(acl_ctx, data, results, n, 1);
```

### Q4: 规则数量有限制吗？

**限制：**
- 创建时指定 `max_rule_num`（如 100、1000）
- 实际限制取决于内存和性能需求
- 建议：< 1000 条规则性能最佳

### Q5: 如何调试匹配结果？

**方法：** 打印数据包和规则的详细信息。

```c
// 打印数据包
printf("数据包: proto=%u, src_ip=0x%08x, dst_ip=0x%08x, "
       "src_port=%u, dst_port=%u\n",
       pkt.proto, ntohl(pkt.ip_src), ntohl(pkt.ip_dst),
       ntohs(pkt.port_src), ntohs(pkt.port_dst));

// 打印规则
printf("规则: proto=%u/%u, src_ip=0x%08x/%u, dst_port=%u-%u\n",
       rule.field[0].value.u8, rule.field[0].mask_range.u8,
       rule.field[1].value.u32, rule.field[1].mask_range.u32,
       rule.field[4].value.u16, rule.field[4].mask_range.u16);
```

### Q6: ACL vs Hash 表，如何选择？

**对比：**

| 特性 | ACL | Hash 表 |
|------|-----|---------|
| **查找方式** | 多字段匹配 | 精确匹配 |
| **通配符** | 支持子网/范围 | 不支持 |
| **优先级** | 支持 | 不支持 |
| **性能** | O(1) ~ O(log n) | O(1) |
| **适用场景** | 防火墙、QoS | 流表、会话跟踪 |

**选择建议：**
- **需要通配符/范围匹配** → 使用 ACL
- **需要精确匹配** → 使用 Hash 表
- **需要优先级** → 使用 ACL
- **规则数量巨大（> 10000）** → 考虑 Hash 表

---

## 九、总结

### 关键知识点

1. **ACL 基本概念**
   - 5 元组：src_ip, dst_ip, src_port, dst_port, protocol
   - 用于高性能数据包分类和过滤
   - 使用 trie 结构实现 O(1) 平均查找

2. **核心流程**
   ```
   创建上下文 → 添加规则 → 构建 ACL → 分类数据包
   ```

3. **字段定义要点**
   - `input_index` 用于 4 字节对齐分组
   - 三种匹配类型：MASK（IP）、RANGE（端口）、BITMASK（协议）
   - 偏移量使用 `offsetof()` 计算

4. **字节序关键点**
   - 规则字段：**主机字节序**
   - 数据包数据：**网络字节序**（需 htonl/htons）

5. **性能优化**
   - 批量分类（32-64 个数据包）
   - 常用规则设置高优先级
   - NUMA 感知分配

### API 速查表

| API | 功能 | 调用时机 |
|-----|------|---------|
| `rte_acl_create()` | 创建 ACL 上下文 | 初始化阶段 |
| `rte_acl_add_rules()` | 添加规则 | 构建前 |
| `rte_acl_build()` | 构建 trie 结构 | 添加完规则后 |
| `rte_acl_classify()` | 分类数据包 | 运行时 |
| `rte_acl_reset()` | 重置规则 | 更新规则时 |
| `rte_acl_free()` | 释放上下文 | 清理阶段 |

### ACL vs 其他查找方法

| 方法 | 查找时间 | 通配符 | 优先级 | 适用场景 |
|------|---------|--------|--------|---------|
| **ACL** | O(1) ~ O(log n) | ✅ | ✅ | 防火墙、QoS、DDoS 防护 |
| **Hash** | O(1) | ❌ | ❌ | 流表、精确匹配 |
| **LPM** | O(log n) | ✅ | ❌ | IP 路由查找 |
| **线性** | O(n) | ✅ | ✅ | 规则少（< 10） |

### 下一步学习

- **Lesson 15**：DPDK LPM（最长前缀匹配）- IP 路由查找
- **高级主题**：结合 ACL + Hash 表实现复杂分类
- **性能调优**：使用 `perf` 分析 ACL 性能瓶颈

### 参考资料

- [DPDK 官方文档：Packet Classification and Access Control](https://doc.dpdk.org/guides/prog_guide/packet_classif_access_ctrl.html)
- [DPDK API 参考：rte_acl.h](https://doc.dpdk.org/api/rte__acl_8h.html)
- [示例代码：l3fwd-acl](https://doc.dpdk.org/guides/sample_app_ug/l3_forward_access_ctrl.html)

---

**实践建议：**

1. 先运行 `acl_demo`，理解基本流程
2. 修改规则和测试数据包，观察匹配结果
3. 尝试添加自己的规则（如允许 HTTPS、拒绝 FTP）
4. 测试批量分类的性能差异
5. 结合 Lesson 3-4（数据包捕获和解析），实现真实防火墙

**加油！** 🚀
