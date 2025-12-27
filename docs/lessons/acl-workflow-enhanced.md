# 四、ACL 示例编写流程

## 4.1 完整开发流程图

```
┌─────────────────────────────────────────┐
│  步骤 1: 初始化 EAL 环境                │
│  rte_eal_init(argc, argv)               │
└──────────────┬──────────────────────────┘
               ↓
┌─────────────────────────────────────────┐
│  步骤 2: 创建 ACL 上下文                │
│  rte_acl_create(&acl_param)             │
│  - 指定 NUMA socket                     │
│  - 设置最大规则数                       │
│  - 定义规则大小                         │
└──────────────┬──────────────────────────┘
               ↓
┌─────────────────────────────────────────┐
│  步骤 3: 配置字段定义                   │
│  setup_acl_config(&cfg)                 │
│  - 定义 5 元组字段                      │
���  - 设置字段类型 (MASK/RANGE/BITMASK)    │
│  - 配置 input_index 对齐                │
└──────────────┬──────────────────────────┘
               ↓
┌─────────────────────────────────────────┐
│  步骤 4: 添加规则                       │
│  rte_acl_add_rules(ctx, rules, num)     │
│  - 构造规则 (五元组 + 优先级)           │
│  - 设置动作 (userdata)                  │
│  - 批量添加多条规则                     │
└──────────────┬──────────────────────────┘
               ↓
┌─────────────────────────────────────────┐
│  步骤 5: 构建 ACL                       │
│  rte_acl_build(ctx, &cfg)               │
│  - 生成优化的 trie 结构                 │
│  - 一次性操作,耗时较长                  │
│  - 构建后不能再添加规则                 │
└──────────────┬──────────────────────────┘
               ↓
┌─────────────────────────────────────────┐
│  步骤 6: 分类数据包                     │
│  rte_acl_classify(ctx, data, results,   │
│                   num, categories)      │
│  - 准备数据包 (网络字节序)              │
│  - 批量分类 (32-64 个为佳)              │
│  - 获取匹配结果 (userdata)              │
└──────────────┬──────────────────────────┘
               ↓
┌─────────────────────────────────────────┐
│  步骤 7: 处理分类结果                   │
│  根据 results[] 做出转发/丢弃决策       │
└──────────────┬──────────────────────────┘
               ↓
┌─────────────────────────────────────────┐
│  步骤 8: 清理资源                       │
│  rte_acl_free(ctx)                      │
│  rte_eal_cleanup()                      │
└─────────────────────────────────────────┘
```

---

## 4.2 步骤详解

### 步骤 1: 初始化 EAL 环境

**目的**: 初始化 DPDK 运行环境，分配 hugepages，绑定 CPU 核心。

**代码示例**:

```c
int main(int argc, char **argv)
{
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_panic("无法初始化 EAL: %s\n", rte_strerror(rte_errno));
    }

    printf("EAL 初始化成功\n");
    // 继续后续步骤...
}
```

**关键点**:
- 必须在所有 DPDK 操作之前调用
- 常用参数: `-l 0` (使用核心 0)、`--no-pci` (无需网卡)

---

### 步骤 2: 创建 ACL 上下文

**目的**: 分配 ACL 上下文内存，初始化内部数据结构。

**代码示例**:

```c
#define MAX_ACL_RULES 10
#define NUM_FIELDS_IPV4 5

struct rte_acl_param acl_param = {
    .name = "ipv4_acl",                         // ACL 名称
    .socket_id = rte_socket_id(),               // 当前 NUMA socket
    .rule_size = RTE_ACL_RULE_SZ(NUM_FIELDS_IPV4), // 规则大小
    .max_rule_num = MAX_ACL_RULES,              // 最大规则数
};

struct rte_acl_ctx *acl_ctx = rte_acl_create(&acl_param);
if (acl_ctx == NULL) {
    rte_exit(EXIT_FAILURE, "错误: 无法创建 ACL 上下文\n");
}

printf("✓ 成功创建 ACL 上下文: %s\n", acl_param.name);
```

**参数说明**:

| 参数 | 说明 | 推荐值 |
|------|------|--------|
| `name` | 唯一名称 | "ipv4_acl" |
| `socket_id` | NUMA 节点 | `rte_socket_id()` |
| `rule_size` | 单条规则大小 | `RTE_ACL_RULE_SZ(5)` |
| `max_rule_num` | 最大规则数 | 根据需求 (如 10, 100) |

**关键点**:
- `socket_id`: NUMA 感知，分配在当前核心所在的内存节点
- `rule_size`: 使用宏 `RTE_ACL_RULE_SZ(N)` 自动计算
- `max_rule_num`: 预先分配空间，超过此数量添加规则会失败

---

### 步骤 3: 配置字段定义

**目的**: 定义如何解析数据包的五元组字段。

**代码示例**:

```c
/* 字段索引枚举 */
enum {
    PROTO_FIELD_IPV4,   // 协议字段
    SRC_FIELD_IPV4,     // 源 IP 字段
    DST_FIELD_IPV4,     // 目的 IP 字段
    SRCP_FIELD_IPV4,    // 源端口字段
    DSTP_FIELD_IPV4     // 目的端口字段
};

static void
setup_acl_config(struct rte_acl_config *cfg)
{
    static struct rte_acl_field_def ipv4_defs[NUM_FIELDS_IPV4] = {
        /* 字段 0: 协议 (1 字节, BITMASK 类型) */
        {
            .type = RTE_ACL_FIELD_TYPE_BITMASK,
            .size = sizeof(uint8_t),
            .field_index = PROTO_FIELD_IPV4,
            .input_index = 0,
            .offset = offsetof(struct ipv4_5tuple, proto),
        },
        /* 字段 1: 源 IP (4 字节, MASK 类型，支持 CIDR) */
        {
            .type = RTE_ACL_FIELD_TYPE_MASK,
            .size = sizeof(uint32_t),
            .field_index = SRC_FIELD_IPV4,
            .input_index = 1,
            .offset = offsetof(struct ipv4_5tuple, ip_src),
        },
        /* 字段 2: 目的 IP (4 字节, MASK 类型) */
        {
            .type = RTE_ACL_FIELD_TYPE_MASK,
            .size = sizeof(uint32_t),
            .field_index = DST_FIELD_IPV4,
            .input_index = 2,
            .offset = offsetof(struct ipv4_5tuple, ip_dst),
        },
        /* 字段 3: 源端口 (2 字节, RANGE 类型) */
        {
            .type = RTE_ACL_FIELD_TYPE_RANGE,
            .size = sizeof(uint16_t),
            .field_index = SRCP_FIELD_IPV4,
            .input_index = 3,
            .offset = offsetof(struct ipv4_5tuple, port_src),
        },
        /* 字段 4: 目的端口 (2 字节, RANGE 类型) */
        {
            .type = RTE_ACL_FIELD_TYPE_RANGE,
            .size = sizeof(uint16_t),
            .field_index = DSTP_FIELD_IPV4,
            .input_index = 3,  // 与源端口共享 input_index
            .offset = offsetof(struct ipv4_5tuple, port_dst),
        },
    };

    memset(cfg, 0, sizeof(*cfg));
    cfg->num_categories = 1;                    // 单一分类器
    cfg->num_fields = NUM_FIELDS_IPV4;
    memcpy(cfg->defs, ipv4_defs, sizeof(ipv4_defs));
}
```

**字段类型说明**:

| 类型 | 说明 | 适用场景 | mask_range 含义 |
|------|------|---------|----------------|
| **BITMASK** | 位掩码匹配 | 协议字段 (1 字节) | 掩码值 (如 0xFF 精确匹配) |
| **MASK** | 子网掩码匹配 | IP 地址 (4 字节) | CIDR 位数 (0-32) |
| **RANGE** | 范围匹配 | 端口字段 (2 字节) | 范围上界 |

**input_index 对齐概念**:

ACL 引擎要求每个 `input_index` 对应 **4 字节数据**，实现硬件加速需要的内存对齐:

```
input_index=0: [proto=1字节][padding=3字节]
input_index=1: [ip_src=4字节]
input_index=2: [ip_dst=4字节]
input_index=3: [port_src=2字节][port_dst=2字节] ← 两个端口共享 4 字节
```

**关键点**:
- 源端口和目的端口共享 `input_index=3`
- `offset` 使用 `offsetof()` 宏自动计算
- 字段顺序必须与数据包结构体对应

---

### 步骤 4: 添加规则

**目的**: 定义防火墙规则，指定匹配条件和动作。

**辅助函数 - 构造规则**:

```c
static void
make_rule(struct acl_ipv4_rule *rule,
          uint32_t priority, uint32_t userdata,
          uint8_t proto, uint8_t proto_mask,
          uint32_t src_ip, uint32_t src_mask_len,
          uint32_t dst_ip, uint32_t dst_mask_len,
          uint16_t src_port_low, uint16_t src_port_high,
          uint16_t dst_port_low, uint16_t dst_port_high)
{
    memset(rule, 0, sizeof(*rule));

    /* 元数据 */
    rule->data.category_mask = 1;           // Category 0
    rule->data.priority = priority;         // 优先级 (数字越大越先匹配)
    rule->data.userdata = userdata;         // 用户数据 (可存储规则 ID 或动作)

    /* 字段 0: 协议 */
    rule->field[PROTO_FIELD_IPV4].value.u8 = proto;
    rule->field[PROTO_FIELD_IPV4].mask_range.u8 = proto_mask;

    /* 字段 1: 源 IP (主机字节序) */
    rule->field[SRC_FIELD_IPV4].value.u32 = src_ip;
    rule->field[SRC_FIELD_IPV4].mask_range.u32 = src_mask_len; // CIDR 位数

    /* 字段 2: 目的 IP */
    rule->field[DST_FIELD_IPV4].value.u32 = dst_ip;
    rule->field[DST_FIELD_IPV4].mask_range.u32 = dst_mask_len;

    /* 字段 3: 源端口范围 */
    rule->field[SRCP_FIELD_IPV4].value.u16 = src_port_low;
    rule->field[SRCP_FIELD_IPV4].mask_range.u16 = src_port_high;

    /* 字段 4: 目的端口范围 */
    rule->field[DSTP_FIELD_IPV4].value.u16 = dst_port_low;
    rule->field[DSTP_FIELD_IPV4].mask_range.u16 = dst_port_high;
}
```

**添加规则示例 (3 条规则)**:

```c
static void
add_acl_rules(struct rte_acl_ctx *ctx)
{
    struct acl_ipv4_rule rules[3];
    int ret;

    printf("[步骤 4] 添加防火墙规则...\n");

    /* 规则 1: 允许 HTTP (端口 80) - 优先级 100 */
    make_rule(&rules[0], 100, 1,        // priority=100, userdata=规则 ID 1
              IPPROTO_TCP, 0xFF,        // TCP 协议
              0, 0,                     // 源 IP: 任意
              0, 0,                     // 目的 IP: 任意
              0, 65535,                 // 源端口: 任意
              80, 80);                  // 目的端口: 80
    printf("  规则 1: 允许 HTTP (端口 80)           [优先级 100]\n");

    /* 规则 2: 拒绝 SSH (端口 22) - 优先级 90 */
    make_rule(&rules[1], 90, 2,
              IPPROTO_TCP, 0xFF,
              0, 0,
              0, 0,
              0, 65535,
              22, 22);                  // 目的端口: 22
    printf("  规则 2: 拒绝 SSH (端口 22)            [优先级 90]\n");

    /* 规则 3: 默认拒绝所有 - 优先级 10 */
    make_rule(&rules[2], 10, 3,
              0, 0,                     // 任意协议
              0, 0,
              0, 0,
              0, 65535,
              0, 65535);                // 任意端口
    printf("  规则 3: 拒绝 所有其他流量 (默认拒绝)  [优先级 10]\n");

    /* 批量添加规则 */
    ret = rte_acl_add_rules(ctx, (struct rte_acl_rule *)rules, 3);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "  错误: 添加规则失败: %s\n", strerror(-ret));
    }
    printf("  ✓ 成功添加 3 条规则\n\n");
}
```

**规则设计说明**:

| 规则 | 优先级 | 匹配条件 | userdata | 说明 |
|------|--------|---------|---------|------|
| 规则 1 | 100 | TCP 端口 80 | 1 | 最高优先级，允许 HTTP |
| 规则 2 | 90 | TCP 端口 22 | 2 | 拒绝 SSH |
| 规则 3 | 10 | 任意流量 | 3 | 最低优先级，默认拒绝 |

**关键点**:
- **优先级**: 数字越大越先匹配
- **字节序**: 规则字段使用主机字节序 (不需要 htonl/htons)
- **userdata**: 可以存储规则 ID (1, 2, 3) 或动作 (ALLOW/DENY)
- **通配符**: 值=0, mask/range=0 表示匹配所有

---

### 步骤 5: 构建 ACL

**目的**: 将规则编译成高效的 trie 查找结构。

**代码示例**:

```c
printf("[步骤 5] 构建 ACL...\n");

struct rte_acl_config cfg;
setup_acl_config(&cfg);  // 配置字段定义

int ret = rte_acl_build(acl_ctx, &cfg);
if (ret != 0) {
    rte_exit(EXIT_FAILURE, "  错误: 构建 ACL 失败: %s\n", strerror(-ret));
}

printf("  ✓ ACL 构建成功\n\n");
```

**关键点**:
- **必须在添加所有规则后调用**
- 构建过程会创建优化的 trie 结构，耗时较长
- 构建后不能再添加规则 (除非先 `rte_acl_reset()`)
- 这是一次性操作，之后可以反复分类

---

### 步骤 6: 分类数据包

**目的**: 对数据包进行规则匹配，返回匹配结果。

**创建测试数据包**:

```c
#define NUM_TEST_PACKETS 3

static void
create_test_packets(struct ipv4_5tuple *packets)
{
    /* 数据包 1: HTTP 请求 → 应匹配规则 1 (允许) */
    packets[0].proto = IPPROTO_TCP;
    packets[0].ip_src = htonl(RTE_IPV4(192, 168, 1, 10));   // 网络字节序!
    packets[0].ip_dst = htonl(RTE_IPV4(192, 168, 1, 100));
    packets[0].port_src = htons(12345);
    packets[0].port_dst = htons(80);

    /* 数据包 2: SSH 请求 → 应匹配规则 2 (拒绝) */
    packets[1].proto = IPPROTO_TCP;
    packets[1].ip_src = htonl(RTE_IPV4(10, 0, 0, 5));
    packets[1].ip_dst = htonl(RTE_IPV4(10, 0, 0, 1));
    packets[1].port_src = htons(54321);
    packets[1].port_dst = htons(22);

    /* 数据包 3: 其他端口 → 应匹配规则 3 (拒绝) */
    packets[2].proto = IPPROTO_TCP;
    packets[2].ip_src = htonl(RTE_IPV4(1, 2, 3, 4));
    packets[2].ip_dst = htonl(RTE_IPV4(5, 6, 7, 8));
    packets[2].port_src = htons(9999);
    packets[2].port_dst = htons(8080);
}
```

**批量分类**:

```c
static void
classify_and_print(struct rte_acl_ctx *ctx,
                   struct ipv4_5tuple *packets,
                   int num_packets)
{
    const uint8_t *data[NUM_TEST_PACKETS];
    uint32_t results[NUM_TEST_PACKETS] = {0};
    int i, ret;

    printf("[步骤 6] 分类测试数据包...\n");

    /* 准备数据指针数组 */
    for (i = 0; i < num_packets; i++) {
        data[i] = (const uint8_t *)&packets[i];
    }

    /* 批量分类 */
    ret = rte_acl_classify(ctx, data, results, num_packets, 1);
    if (ret != 0) {
        rte_exit(EXIT_FAILURE, "  错误: 分类失败: %s\n", strerror(-ret));
    }

    /* 打印分类结果 */
    for (i = 0; i < num_packets; i++) {
        char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
        const char *proto_str, *action_str;

        /* 转换 IP 地址为可读格式 */
        inet_ntop(AF_INET, &packets[i].ip_src, src_ip, sizeof(src_ip));
        inet_ntop(AF_INET, &packets[i].ip_dst, dst_ip, sizeof(dst_ip));

        proto_str = (packets[i].proto == IPPROTO_TCP) ? "TCP" : "UDP";

        /* 根据规则 ID 确定动作 (规则 1 允许, 规则 2 和 3 拒绝) */
        if (results[i] == 1) {
            action_str = "允许";
        } else {
            action_str = "拒绝";
        }

        printf("  包 %d: %s:%-5d -> %s:%-5d (%s)  => %s (规则 %u)\n",
               i + 1,
               src_ip, ntohs(packets[i].port_src),
               dst_ip, ntohs(packets[i].port_dst),
               proto_str,
               action_str,
               results[i]);  // results 存储的就是 userdata (规则 ID)
    }
    printf("\n");
}
```

**关键点**:
- **字节序**: 数据包使用网络字节序 (htonl/htons)，与规则的主机字节序相反
- **批量分类**: 一次处理多个数据包，性能更高 (推荐 32-64 个)
- **结果数组**: `results[i]` 包含匹配规则的 `userdata` 值
- **无匹配**: 如果无匹配规则，`results[i]` 为 0

---

### 步骤 7: 处理分类结果

**目的**: 根据分类结果做出转发或丢弃决策。

**代码示例**:

```c
for (int i = 0; i < num_packets; i++) {
    if (results[i] == 1) {
        // 规则 1: 允许 HTTP
        forward_packet(packets[i]);
        stats.allowed++;
    } else if (results[i] == 2 || results[i] == 3) {
        // 规则 2/3: 拒绝
        drop_packet(packets[i]);
        stats.denied++;
    } else {
        // 无匹配规则: 默认处理
        handle_unmatched(packets[i]);
    }
}
```

**实际应用场景**:
- 防火墙: 根据 userdata 决定转发/丢弃
- 流量整形: 根据 userdata 设置 QoS 优先级
- 负载均衡: 根据 userdata 选择转发队列

---

### 步骤 8: 清理资源

**目的**: 释放 ACL 上下文和 EAL 资源。

**代码示例**:

```c
printf("[步骤 8] 清理资源...\n");

/* 释放 ACL 上下文 */
if (acl_ctx != NULL) {
    rte_acl_free(acl_ctx);
    printf("  ✓ ACL 上下文已释放\n");
}

/* 清理 EAL */
rte_eal_cleanup();
printf("  ✓ EAL 已清理\n\n");

printf("=== 演示结束 ===\n");
return 0;
```

**关键点**:
- 按相反顺序释放资源
- 调用 `rte_eal_cleanup()` 清理 DPDK 环境
- 检查指针是否为 NULL 再释放

---

## 4.3 完整代码示例

完整的可运行代码位于: `14-acl-basic/acl_demo.c` (352 行)

**编译和运行**:

```bash
# 编译
cd /home/work/clionProject/dpdk-hands-on/build
cmake ..
make acl_demo

# 运行
sudo ../bin/acl_demo -l 0 --no-pci
```

**预期输出**:

```
=== DPDK ACL 演示: IPv4防火墙 (简化版) ===

[步骤 1] 创建 ACL 上下文...
  ✓ 成功创建 ACL 上下文: ipv4_acl

[步骤 2] 添加防火墙规则...
  规则 1: 允许 HTTP (端口 80)           [优先级 100]
  规则 2: 拒绝 SSH (端口 22)            [优先级 90]
  规则 3: 拒绝 所有其他流量 (默认拒绝)  [优先级 10]
  ✓ 成功添加 3 条规则

[步骤 3] 构建 ACL...
  ✓ ACL 构建成功

[步骤 4] 分类测试数据包...
  包 1: 192.168.1.10:12345 -> 192.168.1.100:80 (TCP)  => 允许 (规则 1)
  包 2: 10.0.0.5:54321 -> 10.0.0.1:22 (TCP)  => 拒绝 (规则 2)
  包 3: 1.2.3.4:9999 -> 5.6.7.8:8080 (TCP)  => 拒绝 (规则 3)

[清理]
  ✓ ACL 上下文已释放
  ✓ EAL 已清理

=== 演示结束 ===
```

---

## 4.4 关键要点总结

### 字节序注意事项

| 数据类型 | 字节序 | 转换函数 | 示例 |
|---------|--------|---------|------|
| **规则字段** | 主机字节序 | 无需转换 | `RTE_IPV4(192, 168, 1, 0)` |
| **数据包字段** | 网络字节序 | `htonl()`, `htons()` | `htonl(RTE_IPV4(192, 168, 1, 10))` |

**为什么不同?**
- **规则**: 用主机序方便定义和读取
- **数据包**: 用网络序因为来自真实网络数据

### 优先级规则

- **数字越大，优先级越高**
- 先匹配的规则会直接返回，不再继续匹配
- 建议优先级间隔设置大一些 (如 10, 20, 30)，方便后续插入新规则

### 性能优化建议

1. **批量分类**: 一次处理 32-64 个数据包
2. **NUMA 感知**: 创建 ACL 上下文时指定正确的 socket_id
3. **规则排序**: 高频匹配的规则放在前面 (高优先级)
4. **构建优化**: 只在初始化或规则变更时调用 `rte_acl_build()`

### 常见错误

| 错误 | 原因 | 解决方案 |
|------|------|---------|
| 分类结果总是 0 | 规则和数据包字节序不一致 | 检查 htonl/htons 使用 |
| 规则不匹配 | priority 设置错误 | 数字大的先匹配 |
| 构建失败 | 字段定义错误 | 检查 input_index 对齐 |
| 添加规则失败 | 超过 max_rule_num | 增大 max_rule_num |

---

## 4.5 下一步学习

完成本节后，你已经掌握了 ACL 的完整开发流程。接下来可以学习:

- **[Lesson 14-2: ACL 实战应用](lesson14-2-acl-practice.md)** - 详细的 API 使用和高级规则配置
- **[Lesson 14-3: ACL 性能优化](lesson14-3-acl-advanced.md)** - 性能调优和生产环境最佳实践

**练习建议**:
1. 修改规则，添加 UDP 端口 53 (DNS) 的允许规则
2. 增加测试数据包数量到 10 个
3. 尝试添加基于源 IP 子网的规则 (如 192.168.1.0/24)
4. 实现统计功能，记录允许/拒绝的数据包数量
