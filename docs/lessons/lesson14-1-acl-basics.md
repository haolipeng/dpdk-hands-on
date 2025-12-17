# Lesson 14-1: DPDK ACL（访问控制列表）入门

## 课程简介

ACL（Access Control List，访问控制列表）是 DPDK 中用于高性能数据包分类和过滤的核心组件。

本课程介绍 ACL 的基础概念、数据结构和工作原理。

**学习目标：**

- 理解 ACL 的应用场景和优势
- 掌握 IPv4 5 元组匹配的原理
- 熟悉 ACL 的数据结构设计



在学习之前，我们有什么疑问呢？

ACL使用的一般流程是什么呢？

结构体定义包括哪些枚举值可选呢？每个枚举值分别代表什么含义呢？

通过将官网代码进行剖析，

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

- 查找速度慢，规则越多就越慢
- 规则维护困难，手写if-else规则难以维护
- 不支持范围查询和通配符查询

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
192.168.1.10 → 10.0.0.1, Port 12345 → 80, Protocol TCP(6) 对应上述五元组的各个项。
```

### 2.2 ACL 规则结构

一条 ACL 规则由 **匹配字段**、**优先级和 动作** 组成：

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

用于 IP 地址的子网匹配，支持 CIDR 表示法。

#### 2.4.2 RANGE（范围匹配）

用于端口的范围匹配。

#### 2.4.3 BITMASK（位掩码匹配）

用于协议字段的精确匹配。

---

## 三、数据结构详解

### 3.1 ACL 配置结构

```c
struct rte_acl_config {
    uint32_t num_categories;            // category 数量（通常为 1）
    uint32_t num_fields;                // 字段数量（5 元组 = 5）
    struct rte_acl_field_def defs[...]; // 字段定义数组
};
```

因为我们是对网络协议的五元组进行识别，所以num_fields字段填写5，下面来看rte_acl_field_def结构体。



### 3.2 字段定义结构 `rte_acl_field_def`

**结构体字段说明**

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
| `size` | 字段大小 | 端口字段占2个字节，就是`sizeof(uint16_t)` |
| `field_index` | 字段编号 | 0, 1, 2, 3, 4 |
| `input_index` | **4 字节对齐分组** | 协议(0), 源IP(1), 目的IP(2), 端口(3) |
| `offset` | 内存偏移量 | `offsetof(struct ipv4_5tuple, proto)` |



**重点：type类型字段**

- **RTE_ACL_FIELD_TYPE_MASK** - 适用于ip地址，需要用网络掩码来匹配一个范围的字段，比如192.168.1.0/24
- **RTE_ACL_FIELD_TYPE_RANGE** - 用于像port这种需要在某个数值区间内匹配的字段，比如端口8000-9000
- **RTE_ACL_FIELD_TYPE_BITMASK** - 用于像协议标识符这种具有值和位掩码的字段，比如TCP协议标识



**重点：input_index 的作用**

DPDK ACL 要求字段按 **4 字节边界分组**：

```
5 元组内存布局：
┌─────────┬─────────┬─────────┬─────────┬─────────┐
│  proto  │ src_ip  │ dst_ip  │src_port │dst_port │
│ (1字节)  │(4字节)   │(4字节)  │(2字节)   │(2字节)  │
└─────────┴─────────┴─────────┴─────────┴─────────┘
input_index:
     0         1         2         3         3
  └─ 1B ─┘ └─ 4B ──┘ └─ 4B ──┘ └────── 4B ───────┘
                                  (2B + 2B = 4B)
```

所以**源端口和目的端口共享 `input_index = 3`**，因为它们合起来是 4 字节。



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

使用**RTE_ACL_RULE_DEF**宏定义规则结构体：

```c
RTE_ACL_RULE_DEF(acl_ipv4_rule, NUM_FIELDS_IPV4);
```

**RTE_ACL_RULE_DEF**宏定义为：

```
#define	RTE_ACL_RULE_DEF(name, fld_num)	struct name {\
	struct rte_acl_rule_data data;               \
	struct rte_acl_field field[fld_num];         \
}
```

展开后相当于：

```
struct acl_ipv4_rule {
    struct rte_acl_rule_data data;  // 元数据
    struct rte_acl_field field[5];  // 5 个字段
};
```

acl_ipv4_rule是分为元数据和字段数据两部分的。



**元数据（`rte_acl_rule_data`）：**

```c
struct rte_acl_rule_data {
    uint32_t category_mask;  // 分类器掩码（位 0 = category 0）
    int32_t priority;        // 优先级（越大越高）
    uint32_t userdata;       // 用户自定义数据（规则id）
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
    } mask_range;   // 掩码或范围（取决于 type，RTE_ACL_FIELD_TYPE_MASK，RTE_ACL_FIELD_TYPE_RANGE，RTE_ACL_FIELD_TYPE_BITMASK）
};
```



## 四、ACL 示例编写流程

### 4.1 完整开发流程图

```
┌────────────────────────────────────┐
│  1. 初始化 EAL                     │
│     rte_eal_init()                 │
└─────────────┬──────────────────────┘
              ↓
┌────────────────────────────────────┐
│  2. 创建 ACL 上下文                │
│     rte_acl_create()               │
└─────────────┬──────────────────────┘
              ↓
┌────────────────────────────────────┐
│  3. 配置字段定义（使用标准宏）     │
│     - RTE_ACL_IPV4VLAN_PROTO (0)   │
│     - RTE_ACL_IPV4VLAN_SRC (2)     │
│     - RTE_ACL_IPV4VLAN_DST (3)     │
│     - RTE_ACL_IPV4VLAN_PORTS (4)   │
└─────────────┬──────────────────────┘
              ↓
┌────────────────────────────────────┐
│  4. 添加规则                       │
│     rte_acl_add_rules()            │
└─────────────┬──────────────────────┘
              ↓
┌────────────────────────────────────┐
│  5. 构建 ACL (一次性)              │
│     rte_acl_build()                │
└─────────────┬──────────────────────┘
              ↓
┌────────────────────────────────────┐
│  6. 批量分类数据包                 │
│     rte_acl_classify()             │
└─────────────┬──────────────────────┘
              ↓
┌────────────────────────────────────┐
│  7. 处理结果并清理                 │
│     rte_acl_free()                 │
└────────────────────────────────────┘
```

---

### 4.2 关键步骤说明

### 步骤 1-2: 初始化与创建上下文

```c
// 1. 初始化 EAL
int ret = rte_eal_init(argc, argv);

// 2. 创建 ACL 上下文
struct rte_acl_param acl_param = {
    .name = "ipv4_acl",
    .socket_id = rte_socket_id(),               // NUMA 感知
    .rule_size = RTE_ACL_RULE_SZ(NUM_FIELDS_IPV4),
    .max_rule_num = MAX_ACL_RULES,
};
struct rte_acl_ctx *acl_ctx = rte_acl_create(&acl_param);
```

### 步骤 3: 配置字段定义

定义如何解析五元组，使用 DPDK 标准宏：

```c
static struct rte_acl_field_def ipv4_defs[5] = {
    /* 字段 0: 协议 (BITMASK) */
    { .type = RTE_ACL_FIELD_TYPE_BITMASK, .size = 1,
      .field_index = 0, .input_index = RTE_ACL_IPV4VLAN_PROTO, ... },
    /* 字段 1: 源 IP (MASK, 支持 CIDR) */
    { .type = RTE_ACL_FIELD_TYPE_MASK, .size = 4,
      .field_index = 1, .input_index = RTE_ACL_IPV4VLAN_SRC, ... },
    /* 字段 2: 目的 IP (MASK) */
    { .type = RTE_ACL_FIELD_TYPE_MASK, .size = 4,
      .field_index = 2, .input_index = RTE_ACL_IPV4VLAN_DST, ... },
    /* 字段 3: 源端口 (RANGE) */
    { .type = RTE_ACL_FIELD_TYPE_RANGE, .size = 2,
      .field_index = 3, .input_index = RTE_ACL_IPV4VLAN_PORTS, ... },
    /* 字段 4: 目的端口 (RANGE) - 与源端口共享 input_index */
    { .type = RTE_ACL_FIELD_TYPE_RANGE, .size = 2,
      .field_index = 4, .input_index = RTE_ACL_IPV4VLAN_PORTS, ... },
};
```

**input_index 标准布局** (遵循 DPDK IPv4+VLAN 标准):
- `RTE_ACL_IPV4VLAN_PROTO (0)`: 协议字段 + 3字节padding
- `RTE_ACL_IPV4VLAN_VLAN (1)`: 预留给 VLAN (本示例未使用)
- `RTE_ACL_IPV4VLAN_SRC (2)`: 源IP (4字节)
- `RTE_ACL_IPV4VLAN_DST (3)`: 目的IP (4字节)
- `RTE_ACL_IPV4VLAN_PORTS (4)`: 源端口+目的端口 (2+2=4字节)

### 步骤 4: 添加规则

```c
// 规则 1: 允许 HTTP (端口 80) - 优先级 100
make_rule(&rules[0], 100, 1,          // priority, userdata
          IPPROTO_TCP, 0xFF,          // 协议: TCP
          0, 0,                       // 源 IP: 任意
          0, 0,                       // 目的 IP: 任意
          0, 65535,                   // 源端口: 任意
          80, 80);                    // 目的端口: 80

// 批量添加
rte_acl_add_rules(ctx, (struct rte_acl_rule *)rules, 3);
```

**关键点**:
- 规则字段使用**主机字节序** (不需要 htonl/htons)
- `priority` 数字越大优先级越高
- `userdata` 可存储规则 ID 或动作

### 步骤 5: 构建 ACL

```c
struct rte_acl_config cfg;
setup_acl_config(&cfg);
rte_acl_build(acl_ctx, &cfg);  // 生成优化的 trie 结构
```

**注意**: 必须在添加所有规则后调用，构建后不能再添加规则。

### 步骤 6: 分类数据包

```c
// 准备数据包 (网络字节序!)
packets[0].proto = IPPROTO_TCP;
packets[0].ip_src = htonl(RTE_IPV4(192, 168, 1, 10));
packets[0].port_dst = htons(80);

// 批量分类
const uint8_t *data[NUM_PACKETS];
uint32_t results[NUM_PACKETS] = {0};
for (i = 0; i < NUM_PACKETS; i++)
    data[i] = (uint8_t *)&packets[i];

rte_acl_classify(ctx, data, results, NUM_PACKETS, 1);

// 检查结果
if (results[i] == 1) {
    printf("允许 (规则 %u)\n", results[i]);
} else {
    printf("拒绝 (规则 %u)\n", results[i]);
}
```

**关键点**:
- 数据包使用**网络字节序** (htonl/htons)，与规则的主机字节序相反
- `results[i]` 包含匹配规则的 `userdata` 值
- 批量分类性能更高 (推荐 32-64 个数据包)

---

### 4.3 字节序对照表

| 数据类型 | 字节序 | 转换函数 | 示例 |
|---------|--------|---------|------|
| **规则字段** | 主机字节序 | 无需转换 | `RTE_IPV4(192, 168, 1, 0)` |
| **数据包字段** | 网络字节序 | `htonl()`, `htons()` | `htonl(RTE_IPV4(192, 168, 1, 10))` |

**为什么不同?**
- 规则用主机序方便定义和读取
- 数据包用网络序因为来自真实网络数据
- ACL 引擎内部会自动处理字节序转换

---

### 4.4 完整示例位置

完整的可运行代码: [14-acl-basic/acl_demo.c](../../14-acl-basic/acl_demo.c)

**编译运行**:
```bash
cd /home/work/clionProject/dpdk-hands-on/build
cmake ..
make acl_demo
sudo ../bin/acl_demo -l 0 --no-pci
```

**更详细的步骤分解和代码说明**，请查看单独的流程文档: [acl-workflow-enhanced.md](acl-workflow-enhanced.md)



## 五、核心 API 概览

本节简要介绍 ACL 的核心 API。详细的使用方法和代码示例请参考 **Lesson 14-2: ACL实战应用**。

### 5.1 创建 ACL 上下文：`rte_acl_create()`

**功能**：创建 ACL 上下文，分配内存和初始化数据结构。



### 5.2 添加规则：`rte_acl_add_rules()`

**功能**：向 ACL 上下文添加一条或多条规则。规则字段使用 **主机字节序**。



### 5.3 构建 ACL：`rte_acl_build()`

**功能**：构建优化的 trie 结构。**必须在添加完所有规则后调用**，构建后不能再添加规则。



### 5.4 分类数据包：`rte_acl_classify()`

**功能**：对数据包进行分类，返回匹配规则的 `userdata`。数据包数据使用 **网络字节序**。
