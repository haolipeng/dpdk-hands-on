# DPDK IP 分片与重组完全指南

> 完整处理分片数据包,实现端到端流分析

---

## 开始之前: 为什么需要 IP 分片重组?

### 问题: 分片数据包的挑战

在网络中,大数据包经常被分片传输:

```
场景示例:
┌─────────────────────────────────────────┐
│ 原始数据包: 3000 字节                   │
│ MTU = 1500 字节                         │
│                                         │
│ 被分片为:                               │
│   片段1: 1500 字节 (MF=1, Offset=0)    │
│   片段2: 1500 字节 (MF=0, Offset=1480) │
└─────────────────────────────────────────┘

问题:
  ❌ 只看到第一个片段的 L4 头部 (TCP/UDP)
  ❌ 无法获取完整的应用层数据
  ❌ 流表查询失败 (缺少端口信息)
  ❌ DPI 深度检测无法进行
  ❌ 可能误判为 DDoS 攻击
```

### 解决方案: IP 分片重组

**DPDK rte_ip_frag** 库提供:
- ✅ 自动识别分片数据包
- ✅ 缓存并重组分片
- ✅ 超时管理 (避免内存泄漏)
- ✅ 支持 IPv4 和 IPv6

```
重组工作流程:
┌────────┐
│ 片段1  │─┐
└────────┘ │
┌────────┐ │    ┌──────────────┐    ┌──────────┐
│ 片段2  │─┼───>│ 重组表       │───>│ 完整数据包│
└────────┘ │    │ (缓存)       │    └──────────┘
┌────────┐ │    └──────────────┘
│ 片段3  │─┘
└────────┘
    ↓
等待所有片段到达
```

---

## 第一课: IP 分片基础知识

### 1.1 IP 分片字段

```c
IPv4 头部中的关键字段:
┌─────────────────────────────────────────┐
│ Identification (16位)                   │
│   - 标识同一个原始数据包的所有片段      │
│                                         │
│ Flags (3位)                             │
│   - MF (More Fragments): 1=还有片段     │
│   - DF (Don't Fragment): 1=不能分片     │
│                                         │
│ Fragment Offset (13位)                  │
│   - 片段在原始数据中的偏移量            │
│   - 单位: 8字节                         │
└─────────────────────────────────────────┘

判断是否为分片:
  1. MF = 1  OR
  2. Fragment Offset > 0
```

### 1.2 分片示例

```
原始数据包: 5140 字节
MTU: 1500 字节

片段1:
  - Total Length: 1500
  - MF: 1 (还有更多片段)
  - Offset: 0 (0 * 8 = 0)
  - 包含: IP头 + TCP头 + 数据1

片段2:
  - Total Length: 1500
  - MF: 1
  - Offset: 185 (185 * 8 = 1480)
  - 包含: IP头 + 数据2

片段3:
  - Total Length: 1500
  - MF: 1
  - Offset: 370 (370 * 8 = 2960)
  - 包含: IP头 + 数据3

片段4:
  - Total Length: 740
  - MF: 0 (最后一个片段)
  - Offset: 555 (555 * 8 = 4440)
  - 包含: IP头 + 数据4
```

---

## 第二课: rte_ip_frag API

### 2.1 核心数据结构

```c
/* 分片表 - IPv4 */
struct rte_ip_frag_tbl;

/* 分片死亡行 - 用于超时管理 */
struct rte_ip_frag_death_row;

/* 分片键 - 标识同一个数据包的片段 */
struct ip_frag_key {
    uint64_t src_dst;      /* 源IP + 目的IP */
    uint32_t id;           /* Identification */
    uint32_t key_len;      /* 键长度 */
};
```

### 2.2 创建分片表

```c
#define MAX_FRAG_NUM 4        /* 每个数据包最多片段数 */
#define FRAG_TBL_BUCKET_ENTRIES 16  /* 每个桶的条目数 */

struct rte_ip_frag_tbl *frag_tbl;

/* 计算表大小 */
uint32_t max_flows = 1024;
uint32_t bucket_entries = FRAG_TBL_BUCKET_ENTRIES;
uint32_t bucket_num = (max_flows + bucket_entries - 1) / bucket_entries;

/* 创建分片表 */
frag_tbl = rte_ip_frag_table_create(
    bucket_num,             /* 桶数量 */
    bucket_entries,         /* 每桶条目数 */
    max_flows,              /* 最大流数量 */
    RTE_IPV4_MAX_PKT_SIZE,  /* 最大包大小 */
    rte_socket_id()         /* NUMA 节点 */
);

if (frag_tbl == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create fragment table\n");
```

### 2.3 IPv4 分片重组

```c
/* 重组 IPv4 分片 */
struct rte_mbuf *reassembled;
struct rte_ip_frag_death_row death_row;

/* 初始化死亡行 */
rte_ip_frag_death_row_init(&death_row, BURST_SIZE);

/* 尝试重组 */
reassembled = rte_ipv4_frag_reassemble_packet(
    frag_tbl,               /* 分片表 */
    &death_row,             /* 死亡行 */
    mbuf,                   /* 当前片段 */
    rte_rdtsc(),            /* 当前时间戳 */
    ip_hdr                  /* IP 头部指针 */
);

if (reassembled != NULL) {
    /* 重组成功 */
    printf("Fragment reassembled!\n");
    process_complete_packet(reassembled);
} else {
    /* 还在等待其他片段,或者是非分片包 */
    mbuf = NULL;  /* mbuf 已被分片表接管 */
}

/* 释放超时的片段 */
rte_ip_frag_free_death_row(&death_row, PREFETCH_OFFSET);
```

### 2.4 检查是否为分片

```c
static inline int
is_ipv4_fragment(const struct rte_ipv4_hdr *ip_hdr)
{
    uint16_t frag_off = rte_be_to_cpu_16(ip_hdr->fragment_offset);

    /* MF 标志位 或 偏移量 > 0 */
    return (frag_off & RTE_IPV4_HDR_MF_FLAG) != 0 ||
           (frag_off & RTE_IPV4_HDR_OFFSET_MASK) != 0;
}
```

---

## 第三课: 完整的重组流程

### 3.1 基本流程

```c
static struct rte_mbuf *
handle_ipv4_packet(struct rte_mbuf *m,
                   struct rte_ip_frag_tbl *frag_tbl,
                   struct rte_ip_frag_death_row *dr,
                   uint64_t cur_tsc)
{
    struct rte_ipv4_hdr *ip_hdr;

    ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
                                     sizeof(struct rte_ether_hdr));

    /* 检查是否为分片 */
    if (is_ipv4_fragment(ip_hdr)) {
        /* 尝试重组 */
        struct rte_mbuf *mo;

        mo = rte_ipv4_frag_reassemble_packet(frag_tbl, dr, m,
                                            cur_tsc, ip_hdr);

        if (mo == NULL) {
            /* 片段已缓存,等待其他片段 */
            return NULL;
        }

        /* 重组成功,更新 IP 头指针 */
        ip_hdr = rte_pktmbuf_mtod_offset(mo, struct rte_ipv4_hdr *,
                                         sizeof(struct rte_ether_hdr));
        return mo;
    }

    /* 非分片包,直接返回 */
    return m;
}
```

### 3.2 超时管理

```c
#define FRAG_TIMEOUT_MS 5000  /* 5秒超时 */

uint64_t cur_tsc = rte_rdtsc();
uint64_t hz = rte_get_timer_hz();
uint64_t timeout_cycles = (hz * FRAG_TIMEOUT_MS) / 1000;

/* 在处理循环中定期清理 */
static uint64_t last_cleanup = 0;

if (cur_tsc - last_cleanup > timeout_cycles) {
    /* 清理超时的片段 */
    rte_ip_frag_death_row_free(&death_row, PREFETCH_OFFSET);
    last_cleanup = cur_tsc;
}
```

---

## 第四课: 性能优化

### 4.1 批量处理

```c
/* 不要逐个处理,使用批量 */
struct rte_mbuf *bufs[BURST_SIZE];
struct rte_mbuf *output[BURST_SIZE];
uint16_t nb_rx, nb_out = 0;

nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

for (uint16_t i = 0; i < nb_rx; i++) {
    struct rte_mbuf *m = handle_ipv4_packet(
        bufs[i], frag_tbl, &death_row, cur_tsc);

    if (m != NULL) {
        output[nb_out++] = m;
    }
}

/* 批量处理输出 */
for (uint16_t i = 0; i < nb_out; i++) {
    process_packet(output[i]);
    rte_pktmbuf_free(output[i]);
}
```

### 4.2 分片表大小调优

```
表大小选择:
┌─────────────────┬──────────────┬─────────────┐
│   流量规模      │ 建议大小     │ 内存占用    │
├─────────────────┼──────────────┼─────────────┤
│ 小型 (< 1K流)   │ 1024         │ ~4 MB       │
│ 中型 (< 10K流)  │ 4096         │ ~16 MB      │
│ 大型 (< 100K流) │ 16384        │ ~64 MB      │
│ 超大型          │ 65536        │ ~256 MB     │
└─────────────────┴──────────────┴─────────────┘

公式:
  bucket_num = (max_flows + bucket_entries - 1) / bucket_entries
  memory ≈ bucket_num * bucket_entries * 1024 bytes
```

### 4.3 避免内存泄漏

```c
/* 定期清理死亡行 */
if (death_row.cnt > 0) {
    rte_ip_frag_free_death_row(&death_row, PREFETCH_OFFSET);
}

/* 程序退出时清理分片表 */
void cleanup_frag_table(struct rte_ip_frag_tbl *frag_tbl)
{
    if (frag_tbl != NULL) {
        rte_ip_frag_table_destroy(frag_tbl);
    }
}
```

---

## 第五课: 统计和监控

### 5.1 分片统计

```c
struct frag_stats {
    uint64_t total_fragments;     /* 总片段数 */
    uint64_t reassembled;         /* 重组成功数 */
    uint64_t timeouts;            /* 超时数 */
    uint64_t fragments_dropped;   /* 丢弃片段数 */
    uint64_t table_full;          /* 表满次数 */
};

/* 更新统计 */
static inline void update_frag_stats(
    struct frag_stats *stats,
    struct rte_mbuf *m,
    int is_fragment,
    int reassembled)
{
    if (is_fragment) {
        stats->total_fragments++;
    }

    if (reassembled) {
        stats->reassembled++;
    }
}
```

### 5.2 打印分片表状态

```c
void print_frag_table_stats(struct rte_ip_frag_tbl *frag_tbl)
{
    struct rte_ip_frag_tbl_stat stat;

    rte_ip_frag_table_statistics_dump(frag_tbl, &stat);

    printf("\n=== Fragment Table Statistics ===\n");
    printf("Entries in use: %u\n", stat.entries);
    printf("Max entries:    %u\n", stat.max_entries);
    printf("Failed lookups: %"PRIu64"\n", stat.fail_nospace);
}
```

---

## 第六课: IPv6 分片重组

### 6.1 IPv6 分片扩展头

```
IPv6 分片扩展头格式:
┌────────────────────────────────────┐
│ Next Header (8位)                  │
│ Reserved (8位)                     │
│ Fragment Offset (13位) | MF (1位)  │
│ Identification (32位)              │
└────────────────────────────────────┘
```

### 6.2 IPv6 重组 API

```c
/* IPv6 分片重组 */
struct rte_mbuf *reassembled;

reassembled = rte_ipv6_frag_reassemble_packet(
    frag_tbl,               /* 分片表 */
    &death_row,             /* 死亡行 */
    mbuf,                   /* 当前片段 */
    rte_rdtsc(),            /* 当前时间戳 */
    ip6_hdr,                /* IPv6 头部 */
    frag_hdr                /* 分片扩展头 */
);
```

---

## 第七课: 常见问题

### 7.1 分片攻击防护

```c
/* 限制每个源 IP 的片段数 */
#define MAX_FRAGS_PER_SRC 100

struct src_frag_count {
    uint32_t src_ip;
    uint32_t count;
};

/* 检测分片洪水攻击 */
if (frag_count[src_ip] > MAX_FRAGS_PER_SRC) {
    printf("⚠ Possible fragment flood from %u.%u.%u.%u\n",
           (src_ip >> 24) & 0xFF,
           (src_ip >> 16) & 0xFF,
           (src_ip >> 8) & 0xFF,
           src_ip & 0xFF);
    rte_pktmbuf_free(mbuf);
    return NULL;
}
```

### 7.2 内存不足处理

```c
struct rte_mbuf *mo = rte_ipv4_frag_reassemble_packet(
    frag_tbl, dr, m, cur_tsc, ip_hdr);

if (mo == NULL && rte_errno == ENOMEM) {
    /* 内存不足,清理旧条目 */
    printf("Fragment table full, clearing old entries\n");
    rte_ip_frag_table_del_expired_entries(frag_tbl, dr, cur_tsc);
}
```

### 7.3 大包处理

```c
/* 确保 mbuf 支持大包 */
struct rte_mempool *mbuf_pool;

mbuf_pool = rte_pktmbuf_pool_create(
    "MBUF_POOL",
    NUM_MBUFS,
    MBUF_CACHE_SIZE,
    0,
    RTE_MBUF_DEFAULT_BUF_SIZE + 2048,  /* 支持更大的包 */
    rte_socket_id()
);
```

---

## 总结

### 核心要点

| 概念 | 说明 |
|------|------|
| **分片检测** | MF=1 或 Offset>0 |
| **分片表** | 缓存并管理片段 |
| **重组** | 等待所有片段到达 |
| **超时** | 5秒内未完成重组则丢弃 |
| **性能** | 批量处理 + 定期清理 |

### 使用场景

```
必须使用 IP 重组的场景:
  ✓ 完整流分析 (需要 L4 端口)
  ✓ DPI 深度包检测
  ✓ 应用层协议解析
  ✓ 流量录制 (PCAP)
  ✓ 入侵检测系统 (IDS)

可以不用的场景:
  ✗ 只统计流量 (包数/字节数)
  ✗ 基于 IP 的过滤
  ✗ 简单的包转发
```

### 性能影响

```
重组开销:
  • 内存: 每个流 ~1-4 KB
  • CPU: 额外 10-20% (取决于分片比例)
  • 延迟: 等待片段 (1-100 ms)

优化建议:
  1. 批量处理 (Burst)
  2. 合理的表大小
  3. 定期清理死亡行
  4. 限制分片流数��
```

### API 速查

```c
/* 创建分片表 */
rte_ip_frag_table_create()

/* IPv4 重组 */
rte_ipv4_frag_reassemble_packet()

/* IPv6 重组 */
rte_ipv6_frag_reassemble_packet()

/* 释放超时片段 */
rte_ip_frag_free_death_row()

/* 销毁分片表 */
rte_ip_frag_table_destroy()
```

---

## 参考资料

- DPDK Programmer's Guide - IP Fragmentation and Reassembly
- RFC 791 - Internet Protocol (IPv4)
- RFC 8200 - Internet Protocol, Version 6 (IPv6)
- IP Fragment Reassembly Sample Application
