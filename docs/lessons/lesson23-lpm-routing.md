# Lesson 23: LPM (Longest Prefix Match) 路由表

## 概述

LPM (Longest Prefix Match，最长前缀匹配) 是网络路由查找的核心算法。DPDK 提供了高性能的 LPM 库，用于快速的 IP 路由查找。

### 什么是 LPM？

在 IP 路由中，路由表包含多个网络前缀(如 192.168.1.0/24)。当收到一个数据包时，需要找到与目标 IP 匹配的**最长**前缀，这就是最长前缀匹配。

**示例**：

```
路由表:
  10.0.0.0/8      -> Gateway A
  10.1.0.0/16     -> Gateway B
  10.1.2.0/24     -> Gateway C

数据包目标 IP: 10.1.2.100
  匹配 10.0.0.0/8 (8位)    ✓
  匹配 10.1.0.0/16 (16位)  ✓
  匹配 10.1.2.0/24 (24位)  ✓  <- 最长匹配！

结果: 转发到 Gateway C
```

## 为什么需要 LPM？

### 传统路由查找的问题

**方法1: 线性搜索**
```c
for (int i = 0; i < num_routes; i++) {
    if (match(ip, routes[i])) return routes[i];
}
// 时间复杂度: O(n) - 太慢！
```

**方法2: 哈希表**
```c
next_hop = hash_lookup(ip);  // 只能精确匹配
// 无法处理前缀匹配！
```

### DPDK LPM 的优势

✅ **O(1) 查找时间** - 使用 Trie 数据结构
✅ **支持 CIDR 前缀** - /8, /16, /24 等
✅ **NUMA 感知** - 可在指定 socket 创建
✅ **批量查找** - 一次查找多个 IP
✅ **IPv4 和 IPv6** - 两套独立的 API

## DPDK LPM API

### 核心数据结构

```c
struct rte_lpm;        // LPM 表（不透明类型）
struct rte_lpm_config; // LPM 配置
```

### 创建和配置

```c
// LPM 配置
struct rte_lpm_config config = {
    .max_rules = 1024,      // 最大路由条目数
    .number_tbl8s = 256,    // Trie 的内部表数量
    .flags = 0
};

// 创建 LPM 表
struct rte_lpm *lpm = rte_lpm_create(
    "my_lpm",           // 名称
    SOCKET_ID_ANY,      // NUMA socket
    &config             // 配置
);
```

### 添加路由

```c
// 添加路由: 192.168.1.0/24 -> next_hop = 10
uint32_t ip = IPv4(192, 168, 1, 0);
uint8_t depth = 24;          // 前缀长度
uint32_t next_hop = 10;      // 下一跳 ID

int ret = rte_lpm_add(lpm, ip, depth, next_hop);
```

### 路由查找

```c
// 单个查找
uint32_t ip = IPv4(192, 168, 1, 100);
uint32_t next_hop;

int ret = rte_lpm_lookup(lpm, ip, &next_hop);
if (ret == 0) {
    printf("Found: next_hop = %u\n", next_hop);
}
```

### 批量查找（高性能）

```c
#define BATCH_SIZE 32

const uint32_t ips[BATCH_SIZE] = { /* IP 地址数组 */ };
uint32_t next_hops[BATCH_SIZE];

int num_found = rte_lpm_lookup_bulk(
    lpm,
    ips,
    next_hops,
    BATCH_SIZE
);
```

### 删除路由

```c
uint32_t ip = IPv4(192, 168, 1, 0);
uint8_t depth = 24;

rte_lpm_delete(lpm, ip, depth);
```

### 清空和销毁

```c
// 删除所有路由
rte_lpm_delete_all(lpm);

// 释放 LPM 表
rte_lpm_free(lpm);
```

## LPM 内部原理

### DIR-24-8 算法

DPDK LPM 使用 **DIR-24-8 算法**，这是一种改进的 Trie 结构：

```
IPv4 地址 (32位) 分为:
  ┌─────────────┬──────────┐
  │  24 位      │  8 位    │
  └─────────────┴──────────┘
     DIR24         TBL8

查找过程:
  1. 用前 24 位索引 DIR24 表 (直接索引,O(1))
  2. 如果是叶子节点,返回 next_hop
  3. 如果是指针,用后 8 位索引 TBL8 表
  4. 最多 2 次内存访问!
```

**示例数据结构**：

```c
// 简化版本（实际更复杂）
struct lpm_tbl24_entry {
    uint32_t next_hop : 24;  // 下一跳 ID
    uint32_t valid    : 1;   // 有效位
    uint32_t depth    : 6;   // 前缀长度
    uint32_t ext      : 1;   // 是否扩展到 TBL8
};

struct lpm_tbl8_entry {
    uint32_t next_hop : 24;
    uint32_t valid    : 1;
    uint32_t depth    : 6;
    uint32_t ext      : 1;
};
```

### 为什么这么快？

1. **最多 2 次内存访问** - DIR24 查找 + TBL8 查找（如果需要）
2. **Cache 友好** - DIR24 表连续，可预取
3. **无分支** - 查找路径固定
4. **SIMD 优化** - 批量查找使用向量指令

### 内存开销

```
DIR24 表: 2^24 entries × 4 bytes = 64 MB
TBL8 表:  number_tbl8s × 256 × 4 bytes
         (例如 256 个表 = 256 KB)

总计: ~64-65 MB (对于中等规模路由表)
```

## 实际应用场景

### 场景 1: L3 转发

```c
// 数据包处理循环
for (i = 0; i < nb_pkts; i++) {
    struct rte_mbuf *m = pkts[i];
    struct rte_ipv4_hdr *ip = get_ipv4_hdr(m);

    uint32_t next_hop;
    if (rte_lpm_lookup(lpm, rte_be_to_cpu_32(ip->dst_addr),
                       &next_hop) == 0) {
        // 找到路由,转发到对应端口
        tx_port = next_hop & 0xFF;
        send_packet(m, tx_port);
    } else {
        // 没有路由,丢弃
        rte_pktmbuf_free(m);
    }
}
```

### 场景 2: 负载均衡

```c
// 多路径 ECMP (Equal-Cost Multi-Path)
struct next_hop_entry {
    uint8_t num_paths;
    uint8_t ports[4];  // 最多 4 条路径
};

struct next_hop_entry next_hop_table[1024];

// 查找并选择路径
uint32_t next_hop_id;
if (rte_lpm_lookup(lpm, dst_ip, &next_hop_id) == 0) {
    struct next_hop_entry *nh = &next_hop_table[next_hop_id];

    // 使用流哈希选择路径（保持流一致性）
    uint32_t hash = flow_hash(ip->src_addr, ip->dst_addr, ...);
    uint8_t path = hash % nh->num_paths;
    uint8_t tx_port = nh->ports[path];

    send_packet(m, tx_port);
}
```

### 场景 3: ACL 预过滤

```c
// 先用 LPM 粗筛,再用 ACL 精确匹配
uint32_t next_hop;
if (rte_lpm_lookup(lpm, dst_ip, &next_hop) == 0) {
    if (next_hop == BLOCKED_SUBNET) {
        // 黑名单子网,直接丢弃
        rte_pktmbuf_free(m);
        return;
    }
}

// 继续 ACL 检查...
```

## 性能优化技巧

### 1. 批量查找

```c
// ❌ 不好: 逐个查找
for (i = 0; i < nb_pkts; i++) {
    rte_lpm_lookup(lpm, ips[i], &next_hops[i]);
}

// ✅ 好: 批量查找（使用 SIMD）
rte_lpm_lookup_bulk_func(lpm, ips, next_hops, nb_pkts);
```

**性能提升**: 2-3x

### 2. 预取下一跳信息

```c
#define PREFETCH_OFFSET 3

for (i = 0; i < nb_pkts; i++) {
    // 预取后面的包
    if (i + PREFETCH_OFFSET < nb_pkts) {
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i + PREFETCH_OFFSET], void *));
    }

    // 处理当前包
    uint32_t next_hop;
    rte_lpm_lookup(lpm, ips[i], &next_hop);

    // 预取 next_hop 表项
    rte_prefetch0(&next_hop_table[next_hop]);
}
```

### 3. NUMA 本地化

```c
// ✅ 在每个 NUMA 节点创建独立的 LPM 表
unsigned int socket_id = rte_socket_id();
struct rte_lpm *lpm = rte_lpm_create("lpm", socket_id, &config);

// ❌ 避免跨 NUMA 访问
```

### 4. 合理配置 TBL8 数量

```c
// 路由表规模与 TBL8 数量的关系:
// 小规模 (<1K routes):  number_tbl8s = 64
// 中等 (1K-10K):        number_tbl8s = 256
// 大规模 (10K-100K):    number_tbl8s = 1024
// 超大 (>100K):         number_tbl8s = 8192

struct rte_lpm_config config = {
    .max_rules = 10000,
    .number_tbl8s = 256,  // 根据实际路由数调整
};
```

### 5. 路由压缩

```c
// 合并连续子网为更大的前缀
// 例如:
//   192.168.0.0/24 -> GW1
//   192.168.1.0/24 -> GW1
//   192.168.2.0/24 -> GW1
//   192.168.3.0/24 -> GW1
// 可以合并为:
//   192.168.0.0/22 -> GW1
```

## 常见问题和注意事项

### 问题 1: 创建 LPM 表失败

```
错误: "Cannot allocate memory for LPM"

原因:
  1. max_rules 设置过大
  2. number_tbl8s 设置过大
  3. 内存不足

解决:
  - 降低 max_rules 和 number_tbl8s
  - 增加 hugepages
  - 使用 rte_lpm_free() 释放不用的表
```

### 问题 2: 路由查找失败

```c
// 常见错误: 忘记转换字节序
uint32_t ip = 0xC0A80164;  // 192.168.1.100 (小端)
rte_lpm_lookup(lpm, ip, &next_hop);  // ❌ 可能查不到

// 正确做法:
uint32_t ip_be = rte_cpu_to_be_32(0xC0A80164);
rte_lpm_lookup(lpm, ip_be, &next_hop);  // ✅
```

### 问题 3: 路由覆盖

```c
// 添加顺序会影响结果!
rte_lpm_add(lpm, IPv4(10,0,0,0), 8, 1);   // 10.0.0.0/8 -> 1
rte_lpm_add(lpm, IPv4(10,1,0,0), 16, 2);  // 10.1.0.0/16 -> 2

// 查找 10.1.2.3 -> 返回 2 (更长的前缀)
// 查找 10.2.3.4 -> 返回 1 (只匹配 /8)
```

### 问题 4: TBL8 耗尽

```
错误: "Failed to add route: No space left"

原因: TBL8 表用完（添加了太多长前缀路由）

解决:
  - 增加 number_tbl8s
  - 或者合并相邻前缀
```

## IPv6 LPM

DPDK 也支持 IPv6 路由查找:

```c
#include <rte_lpm6.h>

// 创建
struct rte_lpm6_config config = {
    .max_rules = 1024,
    .number_tbl8s = 256,
};
struct rte_lpm6 *lpm6 = rte_lpm6_create("lpm6", socket_id, &config);

// 添加路由
uint8_t ip6[16] = {0x20, 0x01, ...};  // 2001::
uint8_t depth = 64;
uint32_t next_hop = 10;
rte_lpm6_add(lpm6, ip6, depth, next_hop);

// 查找
uint32_t next_hop;
rte_lpm6_lookup(lpm6, ip6, &next_hop);

// 批量查找
rte_lpm6_lookup_bulk_func(lpm6, ip6_array, next_hops, num);
```

**IPv6 vs IPv4 差异**:
- IPv6 地址是 128 位（vs 32 位）
- 内存开销更大
- 查找稍慢（但仍然很快）

## 性能数据

### 单次查找性能

```
硬件: Intel Xeon E5-2680 v3 @ 2.5 GHz

单次查找延迟:
  命中 DIR24:        ~10 ns
  需要 TBL8:         ~15 ns

吞吐量（单核）:
  单次查找:          ~100 Mlookups/s
  批量查找(32):      ~200 Mlookups/s
  批量查找(64):      ~250 Mlookups/s
```

### 扩展性

```
多核扩展性（读操作）:
  1 核:   100 Mlookups/s
  2 核:   200 Mlookups/s
  4 核:   400 Mlookups/s
  8 核:   800 Mlookups/s

接近线性扩展（无锁操作）
```

### 内存占用

```
典型路由表:
  企业网络 (1K routes):     ~65 MB
  ISP 边缘 (10K routes):    ~66 MB
  BGP 全表 (100K routes):   ~70 MB
  完整路由 (1M routes):     ~150 MB
```

## 与其他技术对比

### LPM vs Hash

| 特性 | LPM | Hash |
|------|-----|------|
| 查找类型 | 前缀匹配 | 精确匹配 |
| 时间复杂度 | O(1) | O(1) |
| 内存占用 | 固定 (~64MB) | 取决于条目数 |
| 用途 | IP 路由 | 流表、连接跟踪 |

### LPM vs ACL

| 特性 | LPM | ACL |
|------|-----|-----|
| 匹配字段 | 目标 IP | 多字段 (5-tuple+) |
| 规则类型 | 前缀 | 范围、掩码 |
| 性能 | 最快 | 快 |
| 灵活性 | 低 | 高 |

**选择建议**:
- ✅ 只需 IP 路由 → **LPM**
- ✅ 需要端口/协议过滤 → **ACL**
- ✅ 需要两者 → **LPM 预过滤 + ACL 精确匹配**

## 实战技巧

### 1. 动态路由更新

```c
// 路由变更不影响查找性能（无锁读）
void update_route(uint32_t ip, uint8_t depth, uint32_t new_next_hop)
{
    // 更新操作使用 RCU 机制,不阻塞读者
    rte_lpm_add(lpm, ip, depth, new_next_hop);
}

// 查找线程不需要加锁
uint32_t lookup_route(uint32_t ip)
{
    uint32_t next_hop;
    rte_lpm_lookup(lpm, ip, &next_hop);
    return next_hop;
}
```

### 2. 路由统计

```c
// 为每个 next_hop 维护统计信息
struct route_stats {
    uint64_t packets;
    uint64_t bytes;
} __rte_cache_aligned;

struct route_stats stats[MAX_NEXT_HOPS];

// 查找时更新统计
uint32_t next_hop;
if (rte_lpm_lookup(lpm, dst_ip, &next_hop) == 0) {
    stats[next_hop].packets++;
    stats[next_hop].bytes += pkt_len;
}
```

### 3. 多表查找

```c
// 场景: VRF (Virtual Routing and Forwarding)
struct rte_lpm *lpm_tables[NUM_VRFS];

uint32_t lookup_with_vrf(uint32_t vrf_id, uint32_t ip)
{
    struct rte_lpm *lpm = lpm_tables[vrf_id];
    uint32_t next_hop;

    if (rte_lpm_lookup(lpm, ip, &next_hop) == 0) {
        return next_hop;
    }

    return DEFAULT_NEXT_HOP;
}
```

### 4. 导入路由表

```c
// 从文件加载路由
void load_routes_from_file(struct rte_lpm *lpm, const char *filename)
{
    FILE *fp = fopen(filename, "r");
    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        char ip_str[32];
        int depth;
        uint32_t next_hop;

        // 格式: 192.168.1.0/24 10
        sscanf(line, "%[^/]/%d %u", ip_str, &depth, &next_hop);

        uint32_t ip = parse_ipv4(ip_str);
        rte_lpm_add(lpm, ip, depth, next_hop);
    }

    fclose(fp);
}
```

## 总结

### 核心要点

1. **LPM 是 IP 路由查找的最优算法** - O(1) 查找，支持前缀匹配
2. **DIR-24-8 数据结构** - 最多 2 次内存访问
3. **批量查找性能更好** - 使用 `rte_lpm_lookup_bulk()`
4. **内存开销固定** - ~64-70 MB 对于大多数应用
5. **无锁读操作** - 支持多核并发查找

### 适用场景

✅ L3 转发和路由
✅ 负载均衡（ECMP）
✅ ACL 预过滤
✅ 地理位置查询（IP -> 地区）
✅ VRF/多租户路由

### 性能特征

- **查找延迟**: 10-15 ns
- **吞吐量**: 100-250 Mlookups/s (单核)
- **扩展性**: 接近线性多核扩展
- **内存**: 64-150 MB (取决于路由数)

### 下一步

- Lesson 24: FIB (Forwarding Information Base) - 更灵活的路由查找
- Lesson 25: Flow Director - 硬件流分类
- Lesson 26: 完整的 L3 转发应用

你现在已经掌握了 DPDK 高性能路由查找的核心技术！

## 相关文档

- [DPDK Programmer's Guide - LPM Library](https://doc.dpdk.org/guides/prog_guide/lpm_lib.html)
- [DPDK API Reference - rte_lpm.h](https://doc.dpdk.org/api/rte__lpm_8h.html)
- RFC 1519 - Classless Inter-Domain Routing (CIDR)
