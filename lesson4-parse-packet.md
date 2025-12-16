# Lesson 4: DPDK 数据包协议解析详解

## 课程简介

在上一课中，我们学习了如何捕获数据包。本课程将深入讲解如何解析网络数据包的各层协议，包括以太网层、IP 层和 TCP 层。

**学习目标：**
- 理解网络数据包的分层结构
- 掌握字节序转换（大端序 vs 小端序）
- 学会使用 DPDK API 解析各层协议
- 深入理解 mbuf 数据结构和操作宏

**前置知识：**
- 完成 Lesson 3（数据包捕获）
- 了解 TCP/IP 协议栈基础
- 理解网络协议字段含义

---

## 一、网络协议分层与数据包结构

### 1.1 数据包的层次结构

网络数据包采用分层封装的方式传输：

```
┌──────────────────────────────────────────┐
│     以太网帧 (Ethernet Frame)            │
├──────────────────────────────────────────┤
│  14 字节以太网头部                       │
│  ┌────────────────────────────────────┐  │
│  │ 目的 MAC (6) | 源 MAC (6) | 类型(2)│  │
│  └────────────────────────────────────┘  │
├──────────────────────────────────────────┤
│     IP 数据包 (IP Packet)                │
│  ┌────────────────────────────────────┐  │
│  │ 20+ 字节 IP 头部                   │  │
│  │ (版本、长度、TTL、协议类型等)      │  │
│  └────────────────────────────────────┘  │
├──────────────────────────────────────────┤
│     TCP/UDP 段 (TCP/UDP Segment)         │
│  ┌────────────────────────────────────┐  │
│  │ 20+ 字节 TCP 头部                  │  │
│  │ (源端口、目的端口、序号、标志等)   │  │
│  └────────────────────────────────────┘  │
├──────────────────────────────────────────┤
│          应用数据 (Payload)              │
│  ┌────────────────────────────────────┐  │
│  │ HTTP、DNS 等应用层数据             │  │
│  └────────────────────────────────────┘  │
└──────────────────────────────────────────┘
```

### 1.2 使用 Wireshark 观察数据包

在实际开发中，使用 Wireshark 等工具可以帮助我们理解数据包结构：

![数据包解析示例](./picture/image-20250913144806585.png)

**Wireshark 界面说明：**
- **左下方：** 显示每一层协议的字段名称和值（人类可读格式）
- **右侧：** 显示数据包的十六进制原始数据
- **中间：** 层次化的协议树结构

**示例分析：**
```
Ethernet II
├─ Destination: aa:bb:cc:dd:ee:ff
├─ Source: 11:22:33:44:55:66
└─ Type: IPv4 (0x0800)

Internet Protocol Version 4
├─ Version: 4
├─ Header Length: 20 bytes
├─ Total Length: 60
├─ Protocol: TCP (6)
├─ Source: 192.168.1.100
└─ Destination: 192.168.1.1

Transmission Control Protocol
├─ Source Port: 52345
├─ Destination Port: 80
├─ Sequence Number: 12345
└─ Flags: SYN
```

---

## 二、字节序：网络字节序 vs 主机字节序

### 2.1 什么是字节序？

字节序 (Byte Order) 决定了多字节数据在内存中的存储顺序。

**大端序 (Big Endian)：** 高位字节存储在低地址
```
数值: 0x12345678
内存: [0x12] [0x34] [0x56] [0x78]
      低地址 ───────────> 高地址
```

**小端序 (Little Endian)：** 低位字节存储在低地址
```
数值: 0x12345678
内存: [0x78] [0x56] [0x34] [0x12]
      低地址 ───────────> 高地址
```

### 2.2 网络协议中的字节序

| 类型 | 字节序 | 说明 | 应用 |
|------|--------|------|------|
| **网络字节序** | 大端序 (Big Endian) | 网络协议标准 | 所有网络协议 |
| **主机字节序** | 取决于 CPU | x86/x64 是小端序 | 程序内部处理 |

### 2.3 为什么需要转换？

以太网类型字段 `ether_type` 为例：

**DPDK 中的定义：**
```c
// 来源：rte_ether.h
struct rte_ether_hdr {
    struct rte_ether_addr dst_addr;  // 目的 MAC 地址
    struct rte_ether_addr src_addr;  // 源 MAC 地址
    rte_be16_t ether_type;           // ⭐ 注意：rte_be16_t 表示大端序 16 位
};
```

**转换示例：**

假设收到一个 IPv4 数据包：
- IPv4 的 `ether_type` 标准值是 `0x0800`
- 网络中存储为：`[0x08] [0x00]` （大端序）
- 如果在 x86 CPU 上直接读取 16 位整数，会得到 `0x0008`（小端序解释）
- **错误！** 这会导致协议类型判断失败

**正确的转换方法：**

查看完整实现：[main.c:187](4-parse_packet/main.c#L187)

```c
// 读取 ether_type 并转换为主机字节序
uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

// 现在可以正确比较了
if (ether_type == RTE_ETHER_TYPE_IPV4) {  // 0x0800
    // 处理 IPv4 数据包
}
```

### 2.4 DPDK 字节序转换函数

| 函数 | 说明 | 示例 |
|------|------|------|
| `rte_be_to_cpu_16(x)` | 16 位大端序 → 主机字节序 | `ether_type`, `port` |
| `rte_be_to_cpu_32(x)` | 32 位大端序 → 主机字节序 | IP 地址, 序列号 |
| `rte_cpu_to_be_16(x)` | 主机字节序 → 16 位大端序 | 发送数据时使用 |
| `rte_cpu_to_be_32(x)` | 主机字节序 → 32 位大端序 | 发送数据时使用 |

**重要提示：** 所有从网络读取的多字节字段都需要进行字节序转换！

---

## 三、mbuf 结构深入理解

### 3.1 mbuf 内存布局

mbuf (message buffer) 是 DPDK 中存储数据包的核心数据结构。

```
mbuf 内存布局：
┌────────────────────────────────────────┐
│     mbuf 元数据 (struct rte_mbuf)      │ ← mbuf 指针指向这里
│  - buf_addr: 指向数据缓冲区            │
│  - data_off: 数据起始偏移 (通常128)    │
│  - pkt_len: 数据包总长度               │
│  - data_len: 当前段数据长度            │
│  - pool: 所属内存池                    │
│  - next: 下一个 mbuf (链表)            │
├────────────────────────────────────────┤
│      头部空间 (Headroom)               │
│      通常 128 字节                     │
│      用于添加协议头                    │
├────────────────────────────────────────┤ ← data_off 指向这里
│    ┌──────────────────────────────┐    │
│    │   以太网头 (14 字节)         │    │ ← rte_pktmbuf_mtod()
│    ├──────────────────────────────┤    │
│    │   IP 头 (20+ 字节)           │    │
│    ├──────────────────────────────┤    │
│    │   TCP 头 (20+ 字节)          │    │
│    ├──────────────────────────────┤    │
│    │   应用数据 (Payload)         │    │
│    └──────────────────────────────┘    │
├────────────────────────────────────────┤
│      尾部空间 (Tailroom)               │
│      用于添加 trailer                  │
└────────────────────────────────────────┘
```

### 3.2 关键宏：rte_pktmbuf_mtod

**含义：** "mbuf to data" - 获取 mbuf 中数据的起始指针

![rte_pktmbuf_mtod 示意图](./picture/image-20250913151335596.png)

**函数原型：**
```c
#define rte_pktmbuf_mtod(m, t) \
    ((t)((char *)(m)->buf_addr + (m)->data_off))
```

**使用示例：**

查看完整实现：[main.c:184](4-parse_packet/main.c#L184)

```c
// 获取以太网头部指针
struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);

// 等价于：
// struct rte_ether_hdr *eth_hdr =
//     (struct rte_ether_hdr *)((char *)pkt->buf_addr + pkt->data_off);
```

**工作原理：**
1. `pkt->buf_addr` - 数据缓冲区的起始地址
2. `pkt->data_off` - 数据的偏移量（通常是 128 字节）
3. 相加得到实际数据的起始地址
4. 转换为指定类型的指针

### 3.3 关键宏：rte_pktmbuf_mtod_offset

**含义：** 获取 mbuf 中带偏移的数据指针

![rte_pktmbuf_mtod_offset 示意图](./picture/image-20250913151359415.png)

**函数原型：**
```c
#define rte_pktmbuf_mtod_offset(m, t, o) \
    ((t)((char *)(m)->buf_addr + (m)->data_off + (o)))
```

**使用示例：**

查看完整实现：[main.c:199](4-parse_packet/main.c#L199)

```c
// 跳过以太网头，获取 IP 头部指针
struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(
    pkt,
    struct rte_ipv4_hdr *,
    sizeof(struct rte_ether_hdr)  // 偏移 14 字节
);

// 跳过以太网头和 IP 头，获取 TCP 头部指针
struct rte_tcp_hdr *tcp_hdr = rte_pktmbuf_mtod_offset(
    pkt,
    struct rte_tcp_hdr *,
    sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr)
);
```

**对比总结：**

| 宏 | 用途 | 偏移量 |
|----|------|--------|
| `rte_pktmbuf_mtod` | 获取数据起始位置 | 0（从以太网头开始） |
| `rte_pktmbuf_mtod_offset` | 获取指定偏移位置 | 自定义（跳过前面的协议头） |

---

## 四、解析以太网层（Layer 2）

### 4.1 以太网帧结构

```
以太网帧头部（14 字节）：
┌──────────────┬──────────────┬──────────────┐
│  目的 MAC    │   源 MAC     │  EtherType   │
│  (6 字节)    │  (6 字节)    │  (2 字节)    │
└──────────────┴──────────────┴──────────────┘
```

### 4.2 以太网头部结构定义

```c
// DPDK 定义 (rte_ether.h)
struct rte_ether_hdr {
    struct rte_ether_addr dst_addr;  // 目的 MAC 地址
    struct rte_ether_addr src_addr;  // 源 MAC 地址
    rte_be16_t ether_type;           // 以太网类型
} __rte_aligned(2);

// MAC 地址结构
struct rte_ether_addr {
    uint8_t addr_bytes[6];  // 6 字节 MAC 地址
} __rte_aligned(2);
```

### 4.3 常见的 EtherType 值

| EtherType | 值 | 说明 |
|-----------|-----|------|
| `RTE_ETHER_TYPE_IPV4` | 0x0800 | IPv4 协议 |
| `RTE_ETHER_TYPE_IPV6` | 0x86DD | IPv6 协议 |
| `RTE_ETHER_TYPE_ARP` | 0x0806 | ARP 协议 |
| `RTE_ETHER_TYPE_VLAN` | 0x8100 | VLAN 标签 |

### 4.4 解析以太网头部代码

查看完整实现：[main.c:183-193](4-parse_packet/main.c#L183-L193)

```c
static void process_packet(struct rte_mbuf *pkt)
{
    // 1. 获取以太网头部指针
    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);

    // 1.1 获取 ether_type（注意字节序转换）
    uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
    printf("ether_type: %04x\n", ether_type);

    // 1.2 获取并格式化源 MAC 地址
    char buf[RTE_ETHER_ADDR_FMT_SIZE];  // "XX:XX:XX:XX:XX:XX\0" = 18 字节
    rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &(eth_hdr->src_addr));
    printf("src_mac: %s\n", buf);

    // 同样可以获取目的 MAC 地址
    rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &(eth_hdr->dst_addr));
    printf("dst_mac: %s\n", buf);
}
```

**关键函数：**
- `rte_ether_format_addr()` - 将 MAC 地址格式化为字符串（如 "aa:bb:cc:dd:ee:ff"）

---

## 五、解析 IP 层（Layer 3）

### 5.1 IPv4 头部结构

```
IPv4 头部（最少 20 字节）：
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Version|  IHL  |Type of Service|          Total Length         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Identification        |Flags|      Fragment Offset    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Time to Live |    Protocol   |         Header Checksum       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Source Address                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Destination Address                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Options (if IHL > 5)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 5.2 IPv4 头部结构定义

```c
// DPDK 定义 (rte_ip.h)
struct rte_ipv4_hdr {
    uint8_t  version_ihl;      // 版本 (4位) + 头部长度 (4位)
    uint8_t  type_of_service;  // 服务类型 (TOS/DSCP)
    rte_be16_t total_length;   // 总长度（头部 + 数据）
    rte_be16_t packet_id;      // 标识符
    rte_be16_t fragment_offset;// 标志 (3位) + 片偏移 (13位)
    uint8_t  time_to_live;     // 生存时间 (TTL)
    uint8_t  next_proto_id;    // 下一层协议
    rte_be16_t hdr_checksum;   // 头部校验和
    rte_be32_t src_addr;       // 源 IP 地址
    rte_be32_t dst_addr;       // 目的 IP 地址
} __rte_packed;
```

### 5.3 IPv4 字段详解

| 字段 | 大小 | 说明 | 常见值 |
|------|------|------|--------|
| **Version** | 4 bits | IP 版本号 | 4 (IPv4) |
| **IHL** | 4 bits | 头部长度（单位：4 字节） | 5 (20 字节) |
| **TOS** | 8 bits | 服务类型 | 0x00 (普通) |
| **Total Length** | 16 bits | 总长度（包括头部和数据） | 60, 1500 等 |
| **Identification** | 16 bits | 数据包标识符 | 用于分片重组 |
| **Flags** | 3 bits | 控制标志 | DF=不分片, MF=更多分片 |
| **Fragment Offset** | 13 bits | 片偏移量 | 0 (未分片) |
| **TTL** | 8 bits | 生存时间 | 64, 128, 255 |
| **Protocol** | 8 bits | 上层协议 | 6=TCP, 17=UDP, 1=ICMP |
| **Header Checksum** | 16 bits | 头部校验和 | 用于检错 |
| **Source IP** | 32 bits | 源 IP 地址 | 如 192.168.1.100 |
| **Destination IP** | 32 bits | 目的 IP 地址 | 如 8.8.8.8 |

### 5.4 解析 IPv4 头部代码

查看完整实现：[main.c:196-254](4-parse_packet/main.c#L196-L254)

```c
// 判断是否为 IPv4 数据包
if(ether_type == RTE_ETHER_TYPE_IPV4)
{
    // 2. 获取 IPv4 头部（跳过以太网头）
    struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(
        pkt,
        struct rte_ipv4_hdr *,
        sizeof(struct rte_ether_hdr));

    // 2.1 获取源 IP 和目的 IP 地址
    uint32_t src_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);
    uint32_t dst_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);

    // 以点分十进制格式输出 IP 地址
    printf("IPv4: %d.%d.%d.%d -> %d.%d.%d.%d\n",
           (src_ip >> 24) & 0xFF,  // 最高字节（第一个数字）
           (src_ip >> 16) & 0xFF,  // 第三字节（第二个数字）
           (src_ip >> 8) & 0xFF,   // 第二字节（第三个数字）
           (src_ip >> 0) & 0xFF,   // 最低字节（第四个数字）
           (dst_ip >> 24) & 0xFF,
           (dst_ip >> 16) & 0xFF,
           (dst_ip >> 8) & 0xFF,
           (dst_ip >> 0) & 0xFF);

    // 2.2 解析版本号（高 4 位）
    uint8_t version = ipv4_hdr->version_ihl >> 4;
    printf("version: %d\n", version);  // 应该是 4

    // 2.3 解析头部长度（低 4 位，单位：4 字节）
    uint8_t ihl = ipv4_hdr->version_ihl & 0x0F;
    printf("ihl: %d (header length = %d bytes)\n", ihl, ihl * 4);

    // 2.4 解析其他字段
    // 服务类型
    uint8_t type_of_service = ipv4_hdr->type_of_service;
    printf("type_of_service: %d\n", type_of_service);

    // 总长度
    uint16_t total_length = rte_be_to_cpu_16(ipv4_hdr->total_length);
    printf("total_length: %d\n", total_length);

    // 数据包 ID
    uint16_t packet_id = rte_be_to_cpu_16(ipv4_hdr->packet_id);
    printf("packet_id: %d\n", packet_id);

    // 分片相关字段
    uint16_t fragment_offset_raw = rte_be_to_cpu_16(ipv4_hdr->fragment_offset);
    uint16_t flags = (fragment_offset_raw >> 13) & 0x7;    // 高 3 位
    uint16_t fragment_offset = fragment_offset_raw & 0x1FFF; // 低 13 位
    printf("flags: 0x%x, fragment_offset: %d\n", flags, fragment_offset);

    // TTL
    uint8_t ttl = ipv4_hdr->time_to_live;
    printf("ttl: %d\n", ttl);

    // 协议类型
    uint8_t protocol = ipv4_hdr->next_proto_id;
    printf("protocol: %d\n", protocol);

    // 校验和
    uint16_t checksum = rte_be_to_cpu_16(ipv4_hdr->hdr_checksum);
    printf("checksum: 0x%04x\n", checksum);
}
```

### 5.5 IP 地址转换技巧

**理解 IP 地址的表示：**
```
IP 地址：192.168.1.100
转换为 32 位整数：
  192 * 256³ + 168 * 256² + 1 * 256¹ + 100 * 256⁰
= 0xC0A80164

在网络字节序（大端）中存储：
  [0xC0] [0xA8] [0x01] [0x64]

读取并转换为主机字节序后：
  uint32_t ip = 0xC0A80164
  第一个数字：(ip >> 24) & 0xFF = 192
  第二个数字：(ip >> 16) & 0xFF = 168
  第三个数字：(ip >> 8)  & 0xFF = 1
  第四个数字：(ip >> 0)  & 0xFF = 100
```

### 5.6 常见协议类型

| 协议 | 值 | 宏定义 | 说明 |
|------|-----|--------|------|
| ICMP | 1 | `IPPROTO_ICMP` | Internet 控制消息协议 |
| TCP | 6 | `IPPROTO_TCP` | 传输控制协议 |
| UDP | 17 | `IPPROTO_UDP` | 用户数据报协议 |
| IPv6 | 41 | `IPPROTO_IPV6` | IPv6 封装 |
| GRE | 47 | `IPPROTO_GRE` | 通用路由封装 |

---

## 六、解析 TCP 层（Layer 4）

### 6.1 TCP 头部结构

```
TCP 头部（最少 20 字节）：
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Source Port          |       Destination Port        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Sequence Number                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Acknowledgment Number                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Data |       |C|E|U|A|P|R|S|F|                               |
| Offset| Rsrvd |W|C|R|C|S|S|Y|I|            Window             |
|       |       |R|E|G|K|H|T|N|N|                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Checksum            |         Urgent Pointer        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Options (if Data Offset > 5)               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 6.2 TCP 头部结构定义

```c
// DPDK 定义 (rte_tcp.h)
struct rte_tcp_hdr {
    rte_be16_t src_port;    // 源端口
    rte_be16_t dst_port;    // 目的端口
    rte_be32_t sent_seq;    // 序列号
    rte_be32_t recv_ack;    // 确认号
    uint8_t  data_off;      // 数据偏移 (4位) + 保留位 (4位)
    uint8_t  tcp_flags;     // TCP 标志位
    rte_be16_t rx_win;      // 接收窗口大小
    rte_be16_t cksum;       // 校验和
    rte_be16_t tcp_urp;     // 紧急指针
} __rte_packed;
```

### 6.3 TCP 标志位详解

| 标志位 | 宏定义 | 二进制位 | 说明 |
|--------|--------|----------|------|
| **FIN** | `RTE_TCP_FIN_FLAG` | 0x01 | 结束连接 |
| **SYN** | `RTE_TCP_SYN_FLAG` | 0x02 | 同步序列号（建立连接） |
| **RST** | `RTE_TCP_RST_FLAG` | 0x04 | 重置连接 |
| **PSH** | `RTE_TCP_PSH_FLAG` | 0x08 | 推送数据 |
| **ACK** | `RTE_TCP_ACK_FLAG` | 0x10 | 确认号有效 |
| **URG** | `RTE_TCP_URG_FLAG` | 0x20 | 紧急指针有效 |
| **ECE** | `RTE_TCP_ECE_FLAG` | 0x40 | ECN 回显 |
| **CWR** | `RTE_TCP_CWR_FLAG` | 0x80 | 拥塞窗口减少 |

### 6.4 解析 TCP 头部代码

查看完整实现：[main.c:255-279](4-parse_packet/main.c#L255-L279)

```c
// 判断是否为 TCP 协议
if(protocol == IPPROTO_TCP)
{
    printf("detect packet is tcp protocol!\n");

    // 3. 获取 TCP 头部
    // 注意：需要跳过以太网头 + IP 头
    uint8_t l2_len = sizeof(struct rte_ether_hdr);  // 14 字节
    uint8_t l3_len = sizeof(struct rte_ipv4_hdr);   // 20 字节

    struct rte_tcp_hdr *tcp_hdr = rte_pktmbuf_mtod_offset(
        pkt,
        struct rte_tcp_hdr *,
        l2_len + l3_len);

    // 3.1 解析端口号
    uint16_t src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
    uint16_t dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);

    // 3.2 解析序列号和确认号
    uint32_t seq = rte_be_to_cpu_32(tcp_hdr->sent_seq);
    uint32_t ack = rte_be_to_cpu_32(tcp_hdr->recv_ack);

    // 3.3 解析数据偏移（TCP 头部长度）
    uint8_t data_off = (tcp_hdr->data_off >> 4) & 0x0F;  // 高 4 位
    data_off *= 4;  // 转换为字节数（单位：4 字节）

    // 3.4 解析 TCP 标志位
    uint8_t tcp_flags = tcp_hdr->tcp_flags;

    // 3.5 解析其他字段
    uint16_t rx_win = rte_be_to_cpu_16(tcp_hdr->rx_win);    // 窗口大小
    uint16_t cksum = rte_be_to_cpu_16(tcp_hdr->cksum);      // 校验和
    uint16_t tcp_urp = rte_be_to_cpu_16(tcp_hdr->tcp_urp);  // 紧急指针

    // 输出所有解析的字段
    printf("src_port: %d, dst_port: %d, seq: %u, ack: %u, "
           "data_off: %d, tcp_flags: 0x%02x, rx_win: %d, "
           "cksum: 0x%04X, tcp_urp: %d\n",
           src_port, dst_port, seq, ack, data_off,
           tcp_flags, rx_win, cksum, tcp_urp);
}
```

### 6.5 判断 TCP 连接状态

```c
// 判断 TCP 标志位
if (tcp_flags & RTE_TCP_SYN_FLAG) {
    if (tcp_flags & RTE_TCP_ACK_FLAG) {
        printf("TCP SYN-ACK packet (connection response)\n");
    } else {
        printf("TCP SYN packet (connection request)\n");
    }
}

if (tcp_flags & RTE_TCP_FIN_FLAG) {
    printf("TCP FIN packet (connection close)\n");
}

if (tcp_flags & RTE_TCP_RST_FLAG) {
    printf("TCP RST packet (connection reset)\n");
}

if ((tcp_flags & RTE_TCP_PSH_FLAG) && (tcp_flags & RTE_TCP_ACK_FLAG)) {
    printf("TCP PSH-ACK packet (data transfer)\n");
}
```

### 6.6 常见端口号

| 端口号 | 协议 | 说明 |
|--------|------|------|
| 20, 21 | FTP | 文件传输协议 |
| 22 | SSH | 安全外壳协议 |
| 23 | Telnet | 远程登录 |
| 25 | SMTP | 邮件发送 |
| 53 | DNS | 域名解析 |
| 80 | HTTP | 网页浏览 |
| 443 | HTTPS | 安全网页浏览 |
| 3306 | MySQL | 数据库 |
| 6379 | Redis | 缓存数据库 |
| 8080 | HTTP | 备用 HTTP 端口 |

---

## 七、完整示例：数据包解析流程

查看完整实现：[main.c:181-285](4-parse_packet/main.c#L181-L285)

### 7.1 完整的 process_packet 函数

```c
static void process_packet(struct rte_mbuf *pkt)
{
    // === 第 1 层：以太网层 ===
    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
    uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

    char buf[RTE_ETHER_ADDR_FMT_SIZE];
    rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &(eth_hdr->src_addr));
    printf("src_mac: %s\n", buf);

    // === 第 2 层：网络层 (IPv4) ===
    if(ether_type == RTE_ETHER_TYPE_IPV4)
    {
        struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(
            pkt, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));

        uint32_t src_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);
        uint32_t dst_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
        printf("IPv4: %d.%d.%d.%d -> %d.%d.%d.%d\n",
               (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
               (src_ip >> 8) & 0xFF, (src_ip >> 0) & 0xFF,
               (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
               (dst_ip >> 8) & 0xFF, (dst_ip >> 0) & 0xFF);

        uint8_t protocol = ipv4_hdr->next_proto_id;

        // === 第 3 层：传输层 (TCP) ===
        if(protocol == IPPROTO_TCP)
        {
            struct rte_tcp_hdr *tcp_hdr = rte_pktmbuf_mtod_offset(
                pkt, struct rte_tcp_hdr *,
                sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

            uint16_t src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
            uint16_t dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);

            printf("TCP: %d -> %d\n", src_port, dst_port);
        }
    }

    // 更新统计
    total_packets++;
    total_bytes += pkt->pkt_len;
}
```

### 7.2 预期输出示例

```
Starting packet capture on 1 ports. [Ctrl+C to quit]

ether_type: 0800
src_mac: 52:54:00:12:34:56
IPv4: 192.168.1.100 -> 142.250.185.78
version: 4
ihl: 5 (header length = 20 bytes)
type_of_service: 0
total_length: 52
packet_id: 54321
flags: 0x2, fragment_offset: 0
ttl: 64
protocol: 6
checksum: 0xabcd
detect packet is tcp protocol!
src_port: 52345, dst_port: 443, seq: 123456789, ack: 987654321,
data_off: 20, tcp_flags: 0x10, rx_win: 65535, cksum: 0x1234, tcp_urp: 0
```

---

## 八、编译和运行

### 8.1 编译项目

```bash
# 在项目根目录
cd build
cmake ..
make

# 可执行文件生成在 bin 目录
ls -lh bin/parse_packet
```

### 8.2 运行程序

```bash
# 确保已绑定网卡到 DPDK
sudo dpdk-devbind.py --status

# 运行程序
sudo ./bin/parse_packet -l 0

# 生成测试流量（另一个终端）
ping 192.168.1.1  # 产生 ICMP 数据包
curl http://example.com  # 产生 TCP 数据包
```

---

## 九、进阶技巧

### 9.1 处理可变长度头部

IP 和 TCP 头部可能包含选项字段，导致长度可变：

```c
// 正确计算 IP 头部长度
uint8_t ihl = ipv4_hdr->version_ihl & 0x0F;
uint8_t ip_hdr_len = ihl * 4;  // 单位：字节

// 正确计算 TCP 头部长度
uint8_t data_off = (tcp_hdr->data_off >> 4) & 0x0F;
uint8_t tcp_hdr_len = data_off * 4;  // 单位：字节

// 使用实际长度计算下一层位置
struct rte_tcp_hdr *tcp_hdr = rte_pktmbuf_mtod_offset(
    pkt, struct rte_tcp_hdr *,
    sizeof(struct rte_ether_hdr) + ip_hdr_len);
```

### 9.2 解析 UDP 协议

```c
if(protocol == IPPROTO_UDP)
{
    struct rte_udp_hdr {
        rte_be16_t src_port;
        rte_be16_t dst_port;
        rte_be16_t dgram_len;  // 数据报长度
        rte_be16_t dgram_cksum;  // 校验和
    };

    struct rte_udp_hdr *udp_hdr = rte_pktmbuf_mtod_offset(
        pkt, struct rte_udp_hdr *,
        sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));

    uint16_t src_port = rte_be_to_cpu_16(udp_hdr->src_port);
    uint16_t dst_port = rte_be_to_cpu_16(udp_hdr->dst_port);
    printf("UDP: %d -> %d\n", src_port, dst_port);
}
```

### 9.3 处理 VLAN 标签

```c
// 检查是否有 VLAN 标签
if (ether_type == RTE_ETHER_TYPE_VLAN) {
    struct rte_vlan_hdr {
        rte_be16_t vlan_tci;  // VLAN Tag Control Information
        rte_be16_t eth_proto;  // 封装的以太网类型
    };

    struct rte_vlan_hdr *vlan_hdr = rte_pktmbuf_mtod_offset(
        pkt, struct rte_vlan_hdr *, sizeof(struct rte_ether_hdr));

    uint16_t vlan_id = rte_be_to_cpu_16(vlan_hdr->vlan_tci) & 0x0FFF;
    ether_type = rte_be_to_cpu_16(vlan_hdr->eth_proto);

    printf("VLAN ID: %d\n", vlan_id);
}
```

### 9.4 提取应用层数据

```c
// 计算应用层数据的起始位置
uint8_t l2_len = sizeof(struct rte_ether_hdr);
uint8_t l3_len = (ipv4_hdr->version_ihl & 0x0F) * 4;
uint8_t l4_len = ((tcp_hdr->data_off >> 4) & 0x0F) * 4;

// 获取应用层数据指针
char *payload = rte_pktmbuf_mtod_offset(pkt, char *, l2_len + l3_len + l4_len);

// 计算应用层数据长度
uint16_t payload_len = pkt->pkt_len - (l2_len + l3_len + l4_len);

printf("Payload length: %d bytes\n", payload_len);

// 打印前 16 字节数据（十六进制）
for (int i = 0; i < 16 && i < payload_len; i++) {
    printf("%02x ", (unsigned char)payload[i]);
}
printf("\n");
```

---

## 十、常见问题与调试技巧

### 问题 1：字节序转换遗漏

**症状：** 协议类型、端口号、IP 地址显示错误

**解决方案：**
- 所有从网络读取的多字节字段都需要转换
- 使用 `rte_be_to_cpu_16()` 和 `rte_be_to_cpu_32()`

### 问题 2：指针偏移计算错误

**症状：** 解析出的数据完全错乱

**解决方案：**
- 使用 `sizeof()` 计算结构体大小，不要硬编码
- 注意可变长度头部（IP 选项、TCP 选项）
- 使用 `rte_pktmbuf_mtod_offset()` 正确计算偏移

### 问题 3：如何调试解析逻辑？

**方法 1：** 使用 Wireshark 对比
```bash
# 使用 tcpdump 捕获相同流量
tcpdump -i eth0 -w capture.pcap

# 用 Wireshark 打开 capture.pcap
# 对比你的程序输出和 Wireshark 显示的字段
```

**方法 2：** 打印原始数据
```c
// 打印数据包的原始十六进制数据
void dump_packet(struct rte_mbuf *pkt, uint32_t len) {
    unsigned char *data = rte_pktmbuf_mtod(pkt, unsigned char *);
    for (uint32_t i = 0; i < len && i < pkt->pkt_len; i++) {
        if (i % 16 == 0) printf("\n%04x: ", i);
        printf("%02x ", data[i]);
    }
    printf("\n");
}
```

---

## 十一、性能优化建议

### 11.1 减少内存访问

```c
// ❌ 不好：多次访问同一字段
uint16_t port1 = rte_be_to_cpu_16(tcp_hdr->src_port);
uint16_t port2 = rte_be_to_cpu_16(tcp_hdr->src_port);

// ✅ 好：缓存值
uint16_t src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
// 使用 src_port 多次
```

### 11.2 使用 DPDK 内置函数

```c
// DPDK 提供了优化的协议解析函数
// 例如：检查 IP 校验和
int is_valid = rte_ipv4_cksum_verify(ipv4_hdr, NULL);
```

### 11.3 批量处理

```c
// 不要逐个处理数据包，使用批量接收
const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);
for (uint16_t i = 0; i < nb_rx; i++) {
    process_packet(bufs[i]);
}
```

---

## 十二、学习资源

### 官方文档
- [DPDK Mbuf Library](https://doc.dpdk.org/guides/prog_guide/mbuf_lib.html)
- [DPDK Packet Framework](https://doc.dpdk.org/guides/prog_guide/packet_framework.html)
- [rte_ether.h API](https://doc.dpdk.org/api/rte__ether_8h.html)
- [rte_ip.h API](https://doc.dpdk.org/api/rte__ip_8h.html)
- [rte_tcp.h API](https://doc.dpdk.org/api/rte__tcp_8h.html)

### 推荐工具
- **Wireshark** - 网络协议分析
- **tcpdump** - 命令行抓包工具
- **hping3** - 生成测试数据包

### 下一步学习
- **Lesson 5：** 数据包转发和修改
- **Lesson 6：** 流表和连接跟踪
- **Lesson 7：** 多进程架构

---

## 十三、总结

本课程详细讲解了 DPDK 数据包解析的核心技术：

### 关键知识点

1. ✅ **字节序转换**
   - 理解大端序和小端序的区别
   - 掌握 `rte_be_to_cpu_16/32()` 的使用

2. ✅ **mbuf 操作**
   - `rte_pktmbuf_mtod()` - 获取数据起始指针
   - `rte_pktmbuf_mtod_offset()` - 带偏移的指针获取

3. ✅ **协议解析**
   - 以太网层：MAC 地址、EtherType
   - IP 层：源/目的 IP、协议类型、TTL 等
   - TCP 层：端口号、序列号、标志位等

4. ✅ **实战技巧**
   - 处理可变长度头部
   - 解析 UDP、VLAN 等协议
   - 性能优化方法

### 实践建议

1. **对比学习：** 使用 Wireshark 验证你的解析结果
2. **逐层实现：** 先实现以太网层，再逐步添加 IP、TCP
3. **异常处理：** 考虑畸形数据包的处理
4. **性能测试：** 测试在高流量下的解析性能

继续加油，掌握网络数据包解析是成为 DPDK 高手的关键一步！🚀

---

**相关代码文件：**
- 完整源代码：[main.c](4-parse_packet/main.c)
- 构建配置：[CMakeLists.txt](4-parse_packet/CMakeLists.txt)
