# DPDK PCAP 录制与回放完全指南

> 高性能数据包捕获,Wireshark 兼容分析

---

## 开始之前: 为什么需要 PCAP 录制?

### 问题: 流量取证和分析

在网络流量分析和安全场景中,需要记录数据包用于:

```
关键场景:
┌─────────────────────────────────────────┐
│ 1. 安全事件取证                         │
│    - 攻击流量回溯分析                   │
│    - 入侵检测证据收集                   │
│                                         │
│ 2. 性能问题诊断                         │
│    - 异常流量分析                       │
│    - 应用层协议调试                     │
│                                         │
│ 3. 合规性要求                           │
│    - 审计日志保存                       │
│    - 法律证据链                         │
│                                         │
│ 4. 流量回放测试                         │
│    - 重现生产环境问题                   │
│    - 压力测试                           │
└─────────────────────────────────────────┘

挑战:
  ❌ 高速流量 (10Gbps+) 难以实时写盘
  ❌ 海量数据存储压力
  ❌ 写盘延迟影响收包性能
  ❌ 需要与 Wireshark 等工具兼容
```

### 解决方案: DPDK PCAP

**DPDK rte_pcapng** 库提供:
- ✅ 高性能 PCAP/PCAPNG 格式写入
- ✅ Wireshark 完全兼容
- ✅ 环形缓冲录制 (节省空间)
- ✅ 条件触发录制 (只录关键流量)
- ✅ 多线程并发写入

```
PCAP 录制架构:
┌────────┐
│  NIC   │
└───┬────┘
    │ 收包
    ▼
┌────────────┐
│ 过滤规则   │ (可选: 只录特定流量)
└─────┬──────┘
      │
      ▼
┌────────────┐
│ 环形缓冲   │ (内存缓冲,避免丢包)
└─────┬──────┘
      │
      ▼
┌────────────┐
│ 写入磁盘   │ → capture.pcapng
└────────────┘
      ↓
  Wireshark 分析
```

---

## 第一课: PCAP 格式基础

### 1.1 PCAP vs PCAPNG

```
PCAP (传统格式):
  • 简单,广泛支持
  • 单一接口
  • 无元数据
  • 文件大小限制 (2GB)

PCAPNG (新一代格式):
  • 支持多接口
  • 丰富的元数据
  • 支持大文件 (>2GB)
  • 更好的扩展性
  • Wireshark 推荐格式

推荐: 使用 PCAPNG
```

### 1.2 PCAPNG 文件结构

```
PCAPNG 文件格式:
┌──────────────────────────────────┐
│ Section Header Block (SHB)       │  必须
│  - 字节序标记                    │
│  - 版本号                        │
├──────────────────────────────────┤
│ Interface Description Block (IDB)│  必须
│  - 接口信息                      │
│  - 链路类型 (Ethernet)           │
│  - 抓包长度                      │
├──────────────────────────────────┤
│ Enhanced Packet Block (EPB)      │  重复
│  - 时间戳                        │
│  - 包长度                        │
│  - 包数据                        │
├──────────────────────────────────┤
│ Enhanced Packet Block (EPB)      │
│  ...                             │
├──────────────────────────────────┤
│ Statistics Block (可选)          │
│  - 抓包统计                      │
└──────────────────────────────────┘
```

---

## 第二课: rte_pcapng API

### 2.1 核心数据结构

```c
#include <rte_pcapng.h>

/* PCAPNG 上下文 */
rte_pcapng_t *pcapng;

/* 创建 PCAPNG 实例 */
pcapng = rte_pcapng_fdopen(
    fd,               /* 文件描述符 */
    NULL,             /* 操作系统描述 */
    NULL,             /* 硬件描述 */
    NULL,             /* 应用名称 */
    NULL              /* 注释 */
);

if (pcapng == NULL) {
    printf("Failed to create pcapng\n");
    return -1;
}
```

### 2.2 添加接口

```c
/* 添加网卡接口到 PCAPNG */
uint32_t port_id = 0;
char ifname[RTE_ETH_NAME_MAX_LEN];
char ifdescr[256];

rte_eth_dev_get_name_by_port(port_id, ifname);
snprintf(ifdescr, sizeof(ifdescr), "DPDK port %u", port_id);

ret = rte_pcapng_add_interface(
    pcapng,           /* PCAPNG 上下文 */
    port_id,          /* 端口 ID */
    ifname,           /* 接口名 */
    ifdescr,          /* 接口描述 */
    NULL              /* 过滤规则 (可选) */
);
```

### 2.3 写入数据包

```c
/* 写入单个数据包 */
ssize_t bytes_written;

bytes_written = rte_pcapng_write_packet(
    pcapng,           /* PCAPNG 上下文 */
    mbuf,             /* 数据包 mbuf */
    rte_pktmbuf_pkt_len(mbuf),  /* 包长度 */
    rte_rdtsc(),      /* 时间戳 (TSC) */
    0                 /* 接口索引 */
);

if (bytes_written < 0) {
    printf("Failed to write packet\n");
}
```

### 2.4 批量写入

```c
/* 批量写入多个数据包 */
struct rte_mbuf *bufs[BURST_SIZE];
uint16_t nb_pkts;

nb_pkts = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

for (uint16_t i = 0; i < nb_pkts; i++) {
    rte_pcapng_write_packet(pcapng, bufs[i],
                           rte_pktmbuf_pkt_len(bufs[i]),
                           rte_rdtsc(), 0);
}
```

### 2.5 关闭文件

```c
/* 写入统计信息并关闭 */
rte_pcapng_write_stats(
    pcapng,
    port_id,
    NULL,             /* 统计信息 (可选) */
    0,                /* 开始时间 */
    rte_rdtsc()       /* 结束时间 */
);

rte_pcapng_close(pcapng);
```

---

## 第三课: 录制策略

### 3.1 全量录制

```c
/* 录制所有数据包 */
while (!force_quit) {
    nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

    for (uint16_t i = 0; i < nb_rx; i++) {
        rte_pcapng_write_packet(pcapng, bufs[i],
                               rte_pktmbuf_pkt_len(bufs[i]),
                               rte_rdtsc(), 0);

        rte_pktmbuf_free(bufs[i]);
    }
}

优点: 完整记录
缺点: 存储压力大
```

### 3.2 条件录制

```c
/* 只录制特定条件的包 */
for (uint16_t i = 0; i < nb_rx; i++) {
    /* 条件: TCP SYN 包 */
    if (is_tcp_syn(bufs[i])) {
        rte_pcapng_write_packet(pcapng, bufs[i],
                               rte_pktmbuf_pkt_len(bufs[i]),
                               rte_rdtsc(), 0);
    }

    /* 条件: 来自特定 IP */
    if (is_from_vip_ip(bufs[i])) {
        rte_pcapng_write_packet(pcapng, bufs[i],
                               rte_pktmbuf_pkt_len(bufs[i]),
                               rte_rdtsc(), 0);
    }

    rte_pktmbuf_free(bufs[i]);
}

优点: 减少存储,聚焦关键流量
缺点: 需要定义过滤条件
```

### 3.3 环形缓冲录制

```c
/* 环形缓冲: 只保留最近 N 个包 */
#define RING_BUFFER_SIZE 10000

struct capture_ring {
    struct rte_mbuf *pkts[RING_BUFFER_SIZE];
    uint64_t timestamps[RING_BUFFER_SIZE];
    uint32_t head;
    uint32_t count;
};

void ring_buffer_add(struct capture_ring *ring, struct rte_mbuf *m)
{
    uint32_t idx = ring->head % RING_BUFFER_SIZE;

    /* 释放旧包 */
    if (ring->count >= RING_BUFFER_SIZE && ring->pkts[idx] != NULL) {
        rte_pktmbuf_free(ring->pkts[idx]);
    }

    /* 添加新包 */
    ring->pkts[idx] = rte_pktmbuf_clone(m, mbuf_pool);
    ring->timestamps[idx] = rte_rdtsc();
    ring->head++;

    if (ring->count < RING_BUFFER_SIZE)
        ring->count++;
}

/* 触发事件时,写入环形缓冲中的所有包 */
void flush_ring_buffer(struct capture_ring *ring, rte_pcapng_t *pcapng)
{
    uint32_t start = (ring->head - ring->count) % RING_BUFFER_SIZE;

    for (uint32_t i = 0; i < ring->count; i++) {
        uint32_t idx = (start + i) % RING_BUFFER_SIZE;

        rte_pcapng_write_packet(pcapng, ring->pkts[idx],
                               rte_pktmbuf_pkt_len(ring->pkts[idx]),
                               ring->timestamps[idx], 0);
    }
}

优点: 事件触发时可以看到前因后果
缺点: 占用额外内存
```

### 3.4 采样录制

```c
/* 采样录制: 每 N 个包录 1 个 */
#define SAMPLE_RATE 100  /* 1% 采样 */

uint64_t packet_count = 0;

for (uint16_t i = 0; i < nb_rx; i++) {
    packet_count++;

    if (packet_count % SAMPLE_RATE == 0) {
        rte_pcapng_write_packet(pcapng, bufs[i],
                               rte_pktmbuf_pkt_len(bufs[i]),
                               rte_rdtsc(), 0);
    }

    rte_pktmbuf_free(bufs[i]);
}

优点: 大幅减少存储,适合长期监控
缺点: 可能错过关键事件
```

---

## 第四课: 文件管理策略

### 4.1 文件轮转

```c
/* 按大小轮转 */
#define MAX_FILE_SIZE (1024 * 1024 * 1024)  /* 1GB */

uint64_t current_file_size = 0;
int file_index = 0;

void check_rotate_by_size(void)
{
    if (current_file_size >= MAX_FILE_SIZE) {
        /* 关闭当前文件 */
        rte_pcapng_close(pcapng);

        /* 打开新文件 */
        file_index++;
        char filename[256];
        snprintf(filename, sizeof(filename),
                "capture_%d.pcapng", file_index);

        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        pcapng = rte_pcapng_fdopen(fd, NULL, NULL, NULL, NULL);

        current_file_size = 0;
    }
}
```

```c
/* 按时间轮转 */
#define ROTATE_INTERVAL_SEC 3600  /* 每小时轮转 */

time_t last_rotate = time(NULL);

void check_rotate_by_time(void)
{
    time_t now = time(NULL);

    if (now - last_rotate >= ROTATE_INTERVAL_SEC) {
        rte_pcapng_close(pcapng);

        /* 使用时间戳命名 */
        char filename[256];
        struct tm *tm_info = localtime(&now);
        strftime(filename, sizeof(filename),
                "capture_%Y%m%d_%H%M%S.pcapng", tm_info);

        int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        pcapng = rte_pcapng_fdopen(fd, NULL, NULL, NULL, NULL);

        last_rotate = now;
    }
}
```

### 4.2 压缩存储

```c
/* 使用 gzip 压缩 */
#include <zlib.h>

gzFile gz_file;

gz_file = gzopen("capture.pcapng.gz", "wb");

/* 写入时压缩 */
ssize_t bytes = rte_pcapng_write_packet(...);
if (bytes > 0) {
    gzwrite(gz_file, buffer, bytes);
}

gzclose(gz_file);

节省空间: 压缩率通常 50-80%
```

### 4.3 自动清理

```c
/* 保留最近 N 天的文件 */
#define RETENTION_DAYS 7

void cleanup_old_files(void)
{
    time_t now = time(NULL);
    time_t cutoff = now - (RETENTION_DAYS * 86400);

    DIR *dir = opendir("./captures");
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".pcapng")) {
            struct stat st;
            stat(entry->d_name, &st);

            if (st.st_mtime < cutoff) {
                printf("Deleting old capture: %s\n", entry->d_name);
                unlink(entry->d_name);
            }
        }
    }

    closedir(dir);
}
```

---

## 第五课: 性能优化

### 5.1 异步 I/O

```c
/* 使用单独线程写入,避免阻塞收包 */
#include <pthread.h>

struct write_queue {
    struct rte_ring *ring;
    pthread_t thread;
    rte_pcapng_t *pcapng;
};

void* writer_thread(void *arg)
{
    struct write_queue *wq = arg;
    struct rte_mbuf *bufs[BURST_SIZE];
    unsigned nb_deq;

    while (!force_quit) {
        nb_deq = rte_ring_dequeue_burst(wq->ring, (void **)bufs,
                                       BURST_SIZE, NULL);

        for (unsigned i = 0; i < nb_deq; i++) {
            rte_pcapng_write_packet(wq->pcapng, bufs[i],
                                   rte_pktmbuf_pkt_len(bufs[i]),
                                   rte_rdtsc(), 0);
            rte_pktmbuf_free(bufs[i]);
        }
    }

    return NULL;
}

/* 收包线程 */
void capture_thread(void)
{
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;

    while (!force_quit) {
        nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

        /* 入队到写入队列 */
        if (nb_rx > 0) {
            rte_ring_enqueue_burst(write_queue->ring, (void **)bufs,
                                  nb_rx, NULL);
        }
    }
}
```

### 5.2 直接 I/O

```c
/* 使用 O_DIRECT 绕过页缓存 */
int fd = open("capture.pcapng",
             O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT,
             0644);

/* 注意: O_DIRECT 需要对齐的缓冲区 */
void *aligned_buf;
posix_memalign(&aligned_buf, 4096, BUFFER_SIZE);
```

### 5.3 批量写入

```c
/* 批量写入减少系统调用 */
#define WRITE_BATCH_SIZE 64

struct rte_mbuf *write_buffer[WRITE_BATCH_SIZE];
uint16_t buffer_count = 0;

void buffered_write(struct rte_mbuf *m)
{
    write_buffer[buffer_count++] = m;

    if (buffer_count >= WRITE_BATCH_SIZE) {
        flush_write_buffer();
    }
}

void flush_write_buffer(void)
{
    for (uint16_t i = 0; i < buffer_count; i++) {
        rte_pcapng_write_packet(pcapng, write_buffer[i],
                               rte_pktmbuf_pkt_len(write_buffer[i]),
                               rte_rdtsc(), 0);
        rte_pktmbuf_free(write_buffer[i]);
    }
    buffer_count = 0;
}
```

---

## 第六课: 实用技巧

### 6.1 Wireshark 分析

```bash
# 使用 Wireshark 打开
wireshark capture.pcapng

# 命令行分析
tshark -r capture.pcapng

# 统计信息
capinfos capture.pcapng

# 合并多个文件
mergecap -w merged.pcapng capture_*.pcapng

# 过滤导出
tshark -r capture.pcapng -Y "tcp.port==80" -w http.pcapng
```

### 6.2 tcpdump 兼容性

```bash
# DPDK PCAPNG 文件可以直接用 tcpdump 读取
tcpdump -r capture.pcapng

# 按条件过滤
tcpdump -r capture.pcapng 'tcp port 443'

# 显示详细信息
tcpdump -vvv -r capture.pcapng
```

### 6.3 Python 脚本分析

```python
from scapy.all import *

# 读取 PCAPNG 文件
pkts = rdpcap("capture.pcapng")

# 统计
print(f"Total packets: {len(pkts)}")

# 按协议分类
tcp_count = len([p for p in pkts if TCP in p])
udp_count = len([p for p in pkts if UDP in p])

print(f"TCP: {tcp_count}, UDP: {udp_count}")

# 提取特定流量
http_pkts = [p for p in pkts if TCP in p and p[TCP].dport == 80]
wrpcap("http_only.pcapng", http_pkts)
```

---

## 第七课: 常见问题

### 7.1 写入性能不足

```
问题: 写盘速度跟不上收包速度

解决方案:
  1. 使用 SSD 或 NVMe (IOPS > 100K)
  2. 使用 O_DIRECT 绕过页缓存
  3. 异步写入 (独立线程)
  4. 增大写入批量 (减少系统调用)
  5. 采样录制 (降低写入量)
```

### 7.2 磁盘空间不足

```
解决方案:
  1. 文件轮转 (按大小或时间)
  2. 自动清理旧文件
  3. 压缩存储 (gzip)
  4. 只录关键流量 (条件过滤)
  5. 环形缓冲 (内存中保留最近)
```

### 7.3 时间戳不准确

```c
/* 使用高精度时间戳 */
uint64_t ns_timestamp;

/* 方法1: 使用 TSC 转换 */
uint64_t tsc = rte_rdtsc();
ns_timestamp = tsc * 1000000000 / rte_get_timer_hz();

/* 方法2: 使用系统时钟 */
struct timespec ts;
clock_gettime(CLOCK_REALTIME, &ts);
ns_timestamp = ts.tv_sec * 1000000000 + ts.tv_nsec;

rte_pcapng_write_packet(pcapng, mbuf, len, ns_timestamp, 0);
```

---

## 总结

### 核心要点

| 概念 | 说明 |
|------|------|
| **PCAPNG** | 推荐格式,Wireshark 兼容 |
| **rte_pcapng** | DPDK 高性能录制库 |
| **异步写入** | 避免阻塞收包 |
| **文件轮转** | 管理磁盘空间 |
| **条件录制** | 减少存储压力 |

### 录制策略对比

```
┌──────────────┬──────────┬──────────┬─────────────┐
│ 策略         │ 存储需求 │ CPU 开销 │ 适用场景    │
├──────────────┼──────────┼──────────┼─────────────┤
│ 全量录制     │ 极高     │ 高       │ 短期调试    │
│ 条件录制     │ 低       │ 中       │ 安全监控    │
│ 采样录制     │ 极低     │ 低       │ 长期监控    │
│ 环形缓冲     │ 固定     │ 中       │ 事件触发    │
└──────────────┴──────────┴──────────┴─────────────┘
```

### 性能数据

```
10Gbps 网卡录制能力:
┌────────────────────┬──────────────┐
│ 写入方式           │ 最大速率     │
├────────────────────┼──────────────┤
│ 同步写入 (HDD)     │ ~100 Mbps    │
│ 同步写入 (SSD)     │ ~1 Gbps      │
│ 异步写入 (SSD)     │ ~5 Gbps      │
│ 异步写入 (NVMe)    │ ~10 Gbps     │
│ 采样 10% (任意)    │ 10 Gbps      │
└────────────────────┴──────────────┘
```

### API 速查

```c
/* 创建 */
rte_pcapng_fdopen()

/* 添加接口 */
rte_pcapng_add_interface()

/* 写入包 */
rte_pcapng_write_packet()

/* 写入统计 */
rte_pcapng_write_stats()

/* 关闭 */
rte_pcapng_close()
```

---

## 参考资料

- DPDK Programmer's Guide - Packet Capture
- PCAPNG Specification
- Wireshark User Guide
- tcpdump/libpcap Documentation
