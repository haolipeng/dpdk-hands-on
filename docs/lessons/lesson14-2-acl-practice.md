# Lesson 14-2: DPDK ACL 实战应用

## 课程简介

在 **[Lesson 14-1](lesson14-1-acl-basics.md)** 中，你已经学习了 ACL 的核心概念和数据结构。本课程将通过完整的代码示例，教你如何：

- 使用 ACL API 创建防火墙规则
- 编译和运行 ACL 演示程序
- 分析数据包匹配结果

**前置知识**: 完成 [Lesson 14-1: ACL入门与核心概念](lesson14-1-acl-basics.md)

---

## 一、核心 API 详解

### 1.1 创建 ACL 上下文：`rte_acl_create()`

**函数原型：**

```c
struct rte_acl_ctx * rte_acl_create(const struct rte_acl_param *param);
```

**参数说明：**

```c
struct rte_acl_param {
    const char *name;      // ACL 名称（要保证唯一性）
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

### 1.2 添加规则：`rte_acl_add_rules()`

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



采用柔性数组是最推荐的方式。

```
struct acl_ipv4_rule {
    struct rte_acl_rule_data data;
    struct rte_acl_field field[];  // 柔性数组成员
};
// sizeof(struct acl_ipv4_rule) = sizeof(struct rte_acl_rule_data)
```



**关键点：**
- 规则字段值使用 **主机字节序**（不需要 htonl/htons）
- `userdata` 可以存储任意 32 位值（如 规则ID值）

### 1.3 构建 ACL：`rte_acl_build()`

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
- 这是一个比较耗时的操作，但只需执行一次

### 1.4 分类数据包：`rte_acl_classify()`

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
- 失败：负数的错误码

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

## 二、完整示例代码

完整示例代码位于 [14-acl/acl_demo.c](14-acl/acl_demo.c)。

### 2.1 程序流程

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

### 2.2 字段定义配置（acl_demo.c:68-117）

字段定义是 ACL 的核心配置，决定了如何解析 5 元组：

```c
static void
setup_acl_config(struct rte_acl_config *cfg)
{
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

    memset(cfg, 0, sizeof(*cfg));
    cfg->num_categories = 1;
    cfg->num_fields = NUM_FIELDS_IPV4;
    memcpy(cfg->defs, ipv4_defs, sizeof(ipv4_defs));
}
```

### 2.3 规则定义（acl_demo.c:192-255）

演示 5 条规则，展示不同的匹配特性：

| 规则 | 优先级 | 匹配条件 | 动作 | 演示特性 |
|------|--------|---------|------|---------|
| 规则 1 | 100 | HTTP (80) from 192.168.1.0/24 | ALLOW | 子网匹配 |
| 规则 2 | 90 | SSH (22) from any | DENY | 端口精确匹配 |
| 规则 3 | 80 | DNS (UDP 53) from any | ALLOW | 协议区分 |
| 规则 4 | 50 | 端口 1024-65535 | ALLOW | 端口范围 |
| 规则 5 | 10 | 所有流量 | DENY | 默认拒绝 |

**构造规则的辅助函数：**

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
    rule->field[PROTO_FIELD_IPV4].value.u8 = proto;
    rule->field[PROTO_FIELD_IPV4].mask_range.u8 = proto_mask;

    rule->field[SRC_FIELD_IPV4].value.u32 = src_ip;
    rule->field[SRC_FIELD_IPV4].mask_range.u32 = src_mask_len;

    // ... (其他字段)
}
```

### 3.4 测试数据包（acl_demo.c:287-328）

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

### 5.5 批量分类（acl_demo.c:334-404）

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



### 6.3 结果分析

| 数据包 | 匹配规则 | 结果 | 原因 |
|--------|---------|------|------|
| 1 | 规则 1 | 允许 | 来自 192.168.1.0/24 的 HTTP 流量 |
| 2 | 规则 2 | 拒绝 | SSH 端口 22 被拒绝 |
| 3 | 规则 3 | 允许 | DNS 查询（UDP 端口 53） |
| 4 | 规则 4 | 允许 | 高端口范围 8080 ∈ [1024, 65535] |
| 5 | 规则 5 | 拒绝 | 默认拒绝（端口 445 < 1024） |

---

## 总结

通过本课程的实践，你已经掌握了：

### 核心技能

- ✅ **API 使用**：完整掌握 ACL 的 4 个核心 API
- ✅ **规则配置**：能够构造复杂的防火墙规则
- ✅ **数据包分类**：理解单个和批量分类流程
- ✅ **编译运行**：能独立编译、运行、调试 ACL 程序

### 关键要点

1. **开发流程**
   ```
   创建上下文 → 添加规则 → 构建 ACL → 分类数据包
   ```

2. **字节序注意**
   - **规则**：主机字节序（直接使用 `RTE_IPV4()`）
   - **数据包**：网络字节序（需要 `htonl()`/`htons()`）

3. **性能优化**
   - 使用批量分类（32-64 个数据包）
   - 高优先级规则放在前面
   - NUMA 感知分配

### 实战经验

- 完整示例代码演示了 5 条规则的防火墙
- 5 个测试场景覆盖了常见的匹配模式
- 统计信息帮助理解分类结果

### 下一步学习

在 **[Lesson 14-3: ACL性能优化与进阶](lesson14-3-acl-advanced.md)** 中，你将学习：

- ✅ 性能优化技巧（批量分类、NUMA感知、规则排序）
- ✅ 多分类器应用（入站/出站规则分离）
- ✅ 通配符和范围高级技巧
- ✅ 常见问题解决方案
- ✅ ACL vs Hash 表选择策略

**继续学习**: [lesson14-3-acl-advanced.md](lesson14-3-acl-advanced.md)

---

**参考资料：**

- [DPDK 官方文档：Packet Classification](https://doc.dpdk.org/guides/prog_guide/packet_classif_access_ctrl.html)
- [DPDK API 参考：rte_acl.h](https://doc.dpdk.org/api/rte__acl_8h.html)
- [示例代码：l3fwd-acl](https://doc.dpdk.org/guides/sample_app_ug/l3_forward_access_ctrl.html)
