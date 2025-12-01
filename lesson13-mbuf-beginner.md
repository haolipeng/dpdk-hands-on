# DPDK Mbuf 零基础入门教程

> 用最简单的方式理解 DPDK 中最重要的数据结构 - Mbuf

---

## 开始之前：什么是 Mbuf？

### 用生活中的例子理解 Mbuf

想象你在快递公司工作，每天要处理成千上万个包裹。为了高效处理，快递公司会：

1. **准备标准化的包装盒**（这就是 Mbuf）
2. **预先备好一大批空盒子**（这就是 Mempool内存池）
3. **需要时直接拿，用完就还回去**（分配和释放）

在 DPDK 中：
- **Mbuf** = 包装盒（用来装网络数据包）
- **Mempool内存池** = 仓库（存放所有的空盒子）
- **数据包** = 快递物品（实际要传输的数据）



**DPDK Mbuf 的解决方案**：

```c
// 预先准备好一批 mbuf（程序启动时）
mbuf_pool = rte_pktmbuf_pool_create(...);

// 使用时直接拿，超快！
struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);

// 处理数据包...
// 归还到池子，也很快！
rte_pktmbuf_free(mbuf);  
```

---

## 第一课：Mbuf 的内部结构

### 1.1 Mbuf 长什么样？

这里参照了官网文档中mbuf的结构图示，https://doc.dpdk.org/guides/prog_guide/mbuf_lib.html

```
╔═══════════════════════════════════════════════════════════╗
║                    rte_mbuf 结构体                         ║
║  （元数据：记录数据在哪、有多长、是什么类型等信息）         ║
╠═══════════════════════════════════════════════════════════╣
║                                                           ║
║             Headroom（预留的前置空间）                     ║
║                  默认 128 字节                             ║
║                                                           ║
╠═══════════════════════════════════════════════════════════╣
║                                                           ║
║                   实际数据区                               ║
║            （这里存放真正的网络数据包）                     ║
║                                                           ║
╠═══════════════════════════════════════════════════════════╣
║                                                           ║
║              Tailroom（预留的后置空间）                    ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
```

### 1.2 为什么要有 Headroom 和 Tailroom？

答：**为了避免频繁的内存拷贝和申请释放操作**

```
// 原始数据包结构
[headroom] [data] [tailroom]
     ↑       ↑        ↑
   预留空间  实际数据   预留空间
```



数据包在发送时：

```
// 应用层数据
[  headroom  ] [payload data] [ tailroom ]
               ↑
// 添加TCP头              
[  headroom  ] [TCP][payload data] [ tailroom ]
               ↑
// 添加IP头
[  headroom  ] [IP][TCP][payload data] [ tailroom ]
               ↑
// 添加以太网头
[headroom] [ETH][IP][TCP][payload data] [ tailroom ]
```

当数据包在网络协议栈中传递时，经常需要：

- **添加协议头**（IP头、TCP头、以太网头等）
- **添加协议尾**（CRC校验、padding等）

**没有headroom/tailroom的情况下：**

```
// 传统做法：需要重新分配内存
original_data = malloc(data_size);
new_data = malloc(data_size + header_size);
memcpy(new_data + header_size, original_data, data_size);
add_header(new_data);
free(original_data);  // 性能杀手！
```



**有了headroom/tailroom：**

```
// DPDK做法：直接在预留空间操作
rte_pktmbuf_prepend(mbuf, header_size);  // 向前扩展
rte_pktmbuf_append(mbuf, trailer_size);  // 向后扩展
// 零拷贝！
```



### Headroom 的默认大小（128字节）够用吗？


答：**通常够用**。常见协议头大小：

- 以太网：14 字节
- IPv4：20 字节
- IPv6：40 字节
- UDP：8 字节
- TCP：20-60 字节
- VXLAN：8 字节

总共也就 100 字节左右，128 字节足够。

### 1.3 重要字段说明

| 字段名称 | 类型 | 含义 | 比喻 |
|---------|------|------|------|
| `buf_addr` | 指针 | 缓冲区起始地址 | 盒子的起始位置 |
| `data_off` | uint16_t | 数据开始的偏移 | 物品在盒子里的位置 |
| `data_len` | uint16_t | 数据的长度 | 物品的大小 |
| `buf_len` | uint16_t | 整个缓冲区长度 | 盒子的总大小 |
| `next` | 指针 | 下一个 mbuf | 如果一个盒子装不下，指向下一个盒子 |
| `pool` | 指针 | 所属的内存池 | 记住这个盒子是从哪个仓库拿的 |

---

## 第二课：Mbuf 基础操作

本课将第一个 Mbuf 程序与数据操作合并，通过一个完整的示例体验 Mbuf 的生命周期和数据操作。

### 2.1 程序目标

1. 创建 Mbuf 池子（仓库）
2. 从池子里拿一个 Mbuf，查看其结构
3. 使用 **append** 添加数据（从尾部）
4. 使用 **prepend** 添加头部（从头部）
5. 使用 **adj/trim** 移除数据
6. 把 Mbuf 还回去

### 2.2 两种添加数据的方式

```
方式1：append（从后面追加）- 用于添加 payload
┌──────────┬────────────────────────┐
│ Headroom │      Tailroom          │
└──────────┴────────────────────────┘
            ↓ append("Hello")
┌──────────┬───────┬────────────────┐
│ Headroom │ Hello │   Tailroom     │
└──────────┴───────┴────────────────┘

方式2：prepend（从前面添加）- 用于添加协议头
┌──────────┬───────┬────────────────┐
│ Headroom │ Hello │   Tailroom     │
└──────────┴───────┴────────────────┘
     ↓ prepend(header)
┌────┬─────┬───────┬────────────────┐
│    │ Hdr │ Hello │   Tailroom     │
└────┴─────┴───────┴────────────────┘
```

### 2.3 完整代码示例

创建 `mbuf_basics.c`（合并了 mbuf_hello.c 和 mbuf_data_demo.c）：

```c
#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_errno.h>

/* 简单的协议头结构 */
struct simple_header {
    uint32_t magic;      /* 魔数标识 */
    uint16_t version;    /* 版本号 */
    uint16_t length;     /* 数据长度 */
} __attribute__((packed));

/* 打印 Mbuf 空间分布 */
static void print_mbuf_layout(struct rte_mbuf *m, const char *stage)
{
    printf("  [%s]\n", stage);
    printf("    Headroom: %4u | Data: %4u | Tailroom: %4u\n",
           rte_pktmbuf_headroom(m), m->data_len, rte_pktmbuf_tailroom(m));
}

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    struct rte_mbuf *mbuf;
    int ret;

    /* ===== Part 1: 初始化 DPDK 并创建内存池 ===== */
    ret = rte_eal_init(argc, argv);
    if (ret < 0) return -1;

    printf("\n========== DPDK Mbuf Basics Demo ==========\n\n");

    /* 创建 Mbuf 内存池 */
    printf("[Part 1] Create Mbuf Pool\n");
    mbuf_pool = rte_pktmbuf_pool_create(
        "BASICS_POOL", 8192, 256, 0,
        RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id()
    );
    if (mbuf_pool == NULL) {
        printf("ERROR: %s\n", rte_strerror(rte_errno));
        return -1;
    }
    printf("  [OK] Pool created: 8192 mbufs, %d bytes each\n\n",
           RTE_MBUF_DEFAULT_BUF_SIZE);

    /* 分配一个 Mbuf */
    mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (mbuf == NULL) return -1;

    /* 查看 Mbuf 结构 */
    printf("[Part 1] Inspect empty Mbuf\n");
    printf("  buf_len:  %u bytes (total buffer)\n", mbuf->buf_len);
    printf("  data_off: %u bytes (data offset)\n", mbuf->data_off);
    printf("  data_len: %u bytes (current data)\n", mbuf->data_len);
    print_mbuf_layout(mbuf, "Empty mbuf");
    printf("\n");

    /* ===== Part 2: 使用 append 添加数据 ===== */
    printf("[Part 2] Append data to tail\n");
    const char *message = "Hello DPDK!";
    size_t msg_len = strlen(message) + 1;

    char *data = (char *)rte_pktmbuf_append(mbuf, msg_len);
    strcpy(data, message);

    print_mbuf_layout(mbuf, "After append");
    printf("  Content: \"%s\"\n", rte_pktmbuf_mtod(mbuf, char *));
    printf("  Key: append() reduces tailroom, increases data_len\n\n");

    /* ===== Part 3: 使用 prepend 添加头部 ===== */
    printf("[Part 3] Prepend header to head\n");

    struct simple_header *hdr;
    hdr = (struct simple_header *)rte_pktmbuf_prepend(mbuf, sizeof(*hdr));
    hdr->magic = 0xDEADBEEF;
    hdr->version = 1;
    hdr->length = msg_len;

    print_mbuf_layout(mbuf, "After prepend");
    printf("  Header: magic=0x%X, ver=%u, len=%u\n",
           hdr->magic, hdr->version, hdr->length);

    char *payload = rte_pktmbuf_mtod_offset(mbuf, char *, sizeof(*hdr));
    printf("  Payload: \"%s\"\n", payload);
    printf("  Key: prepend() reduces headroom, increases data_len\n\n");

    /* ===== Part 4: 数据包结构可视化 ===== */
    printf("[Part 4] Final packet structure\n");
    printf("  +---------------------------+\n");
    printf("  | Headroom: %4u bytes      |\n", rte_pktmbuf_headroom(mbuf));
    printf("  +---------------------------+\n");
    printf("  | Header:   %4zu bytes      |\n", sizeof(struct simple_header));
    printf("  +---------------------------+\n");
    printf("  | Payload:  %4zu bytes      |\n", msg_len);
    printf("  +---------------------------+\n");
    printf("  | Tailroom: %4u bytes      |\n", rte_pktmbuf_tailroom(mbuf));
    printf("  +---------------------------+\n");
    printf("  Total data_len: %u bytes\n\n", mbuf->data_len);

    /* ===== Part 5: 使用 adj/trim 移除数据 ===== */
    printf("[Part 5] Remove data with adj/trim\n");

    /* adj: 从头部移除 */
    rte_pktmbuf_adj(mbuf, sizeof(struct simple_header));
    print_mbuf_layout(mbuf, "After adj (header removed)");
    printf("  Key: adj() increases headroom, decreases data_len\n");

    /* trim: 从尾部移除 */
    rte_pktmbuf_trim(mbuf, 1);
    print_mbuf_layout(mbuf, "After trim (1 byte removed)");
    printf("  Key: trim() increases tailroom, decreases data_len\n\n");

    /* ===== Part 6: 释放 Mbuf ===== */
    printf("[Part 6] Free Mbuf\n");
    rte_pktmbuf_free(mbuf);
    printf("  [OK] Mbuf returned to pool\n\n");

    rte_eal_cleanup();

    printf("========== API Summary ==========\n");
    printf("  rte_pktmbuf_append()      - Add data to tail\n");
    printf("  rte_pktmbuf_prepend()     - Add data to head\n");
    printf("  rte_pktmbuf_mtod()        - Get data pointer\n");
    printf("  rte_pktmbuf_mtod_offset() - Get pointer with offset\n");
    printf("  rte_pktmbuf_adj()         - Remove from head\n");
    printf("  rte_pktmbuf_trim()        - Remove from tail\n");
    printf("=================================\n\n");

    return 0;
}
```

### 2.4 编译和运行

```bash
# 编译
cd build && make mbuf_basics

# 运行
sudo ./bin/mbuf_basics -l 0 --no-pci
```

### 2.5 核心 API 说明

| API | 功能 | 使用场景 |
|-----|------|---------|
| `rte_pktmbuf_append()` | 在数据后面追加 | 构造数据包时先添加 payload |
| `rte_pktmbuf_prepend()` | 在数据前面添加 | 封装协议头（从内到外） |
| `rte_pktmbuf_mtod()` | 获取数据指针 | 读取数据 |
| `rte_pktmbuf_mtod_offset()` | 获取偏移后的指针 | 跳过头部读取数据 |
| `rte_pktmbuf_adj()` | 从头部移除指定长度的数据 | 解封装（去掉外层协议头） |
| `rte_pktmbuf_trim()` | 从尾部移除指定长度的数据 | 移除填充字节 |

### 2.6 关键概念总结

1. **append** 和 **prepend** 只是移动指针，不拷贝数据（零拷贝）
2. **append** 会减少 tailroom，增加 data_len
3. **prepend** 会减少 headroom，增加 data_len
4. **adj** 会增加 headroom，减少 data_len（移除头部）
5. **trim** 会增加 tailroom，减少 data_len（移除尾部）

---

## 第三课：实战案例 - 构造 UDP 数据包并保存为 pcap 文件

本课演示如何从零构建一个完整的 UDP 数据包，并使用 **libpcap** 将其保存为 pcap 文件，可用 Wireshark 或 tcpdump 查看。

### 3.1 数据包封装的层次

```
应用层  →  添加数据（append）
  ↓
传输层  →  添加 UDP 头（prepend）
  ↓
网络层  →  添加 IP 头（prepend）
  ↓
链路层  →  添加以太网头（prepend）
  ↓
保存    →  写入 pcap 文件（libpcap）
```

### 3.2 依赖安装

```bash
# Ubuntu/Debian
sudo apt-get install libpcap-dev

# CentOS/RHEL
sudo yum install libpcap-devel
```

### 3.3 核心代码：保存数据包到 pcap 文件

使用 libpcap 将 mbuf 数据写入 pcap 文件的关键函数：

```c
#include <pcap/pcap.h>

/**
 * 将 mbuf 数据写入 pcap 文件
 */
static int save_packet_to_pcap(struct rte_mbuf *mbuf, const char *filename)
{
    pcap_t *pcap_handle;
    pcap_dumper_t *pcap_dumper;
    struct pcap_pkthdr pcap_hdr;

    /*
     * 创建一个 "dead" pcap 句柄，用于写入文件
     * DLT_EN10MB 表示以太网链路层类型
     * 65535 是快照长度（最大捕获长度）
     */
    pcap_handle = pcap_open_dead(DLT_EN10MB, 65535);
    if (pcap_handle == NULL) {
        return -1;
    }

    /* 打开 pcap 文件进行写入 */
    pcap_dumper = pcap_dump_open(pcap_handle, filename);
    if (pcap_dumper == NULL) {
        pcap_close(pcap_handle);
        return -1;
    }

    /* 填充 pcap 包头 */
    gettimeofday(&pcap_hdr.ts, NULL);    /* 当前时间戳 */
    pcap_hdr.caplen = mbuf->data_len;    /* 实际捕获的长度 */
    pcap_hdr.len = mbuf->data_len;       /* 数据包原始长度 */

    /* 获取 mbuf 数据指针并写入 pcap 文件 */
    uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);
    pcap_dump((u_char *)pcap_dumper, &pcap_hdr, pkt_data);

    /* 关闭文件和句柄 */
    pcap_dump_close(pcap_dumper);
    pcap_close(pcap_handle);

    return 0;
}
```

### 3.4 完整示例代码

完整代码见 `13-mbuf_usage/build_udp_packet.c`，主要流程：

```c
/* Step 1: 添加 Payload */
char *payload = (char *)rte_pktmbuf_append(mbuf, payload_len);
strcpy(payload, "Hello UDP!");

/* Step 2: 添加 UDP 头 */
struct rte_udp_hdr *udp = rte_pktmbuf_prepend(mbuf, sizeof(*udp));
udp->src_port = htons(12345);
udp->dst_port = htons(80);
udp->dgram_len = htons(sizeof(*udp) + payload_len);

/* Step 3: 添加 IPv4 头 */
struct rte_ipv4_hdr *ip = rte_pktmbuf_prepend(mbuf, sizeof(*ip));
ip->version_ihl = 0x45;
ip->total_length = htons(mbuf->data_len);
ip->next_proto_id = IPPROTO_UDP;
ip->src_addr = htonl(0xC0A80101);  /* 192.168.1.1 */
ip->dst_addr = htonl(0xC0A80102);  /* 192.168.1.2 */

/* Step 4: 添加以太网头 */
struct rte_ether_hdr *eth = rte_pktmbuf_prepend(mbuf, sizeof(*eth));
memcpy(eth->src_addr.addr_bytes, src_mac, 6);
memcpy(eth->dst_addr.addr_bytes, dst_mac, 6);
eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);

/* Step 5: 保存到 pcap 文件 */
save_packet_to_pcap(mbuf, "udp_packet.pcap");
```

### 3.5 编译和运行

```bash
# 编译
cd build && make build_udp_packet

# 运行
sudo ./bin/build_udp_packet -l 0 --no-pci

# 查看生成的 pcap 文件
tcpdump -r udp_packet.pcap -XX
wireshark udp_packet.pcap
tshark -r udp_packet.pcap -V
```

### 3.6 使用 Wireshark 查看

运行程序后会生成 `udp_packet.pcap` 文件，用 Wireshark 打开可以看到完整的协议解析：

```
Frame 1: 87 bytes on wire
Ethernet II, Src: 00:11:22:33:44:55, Dst: aa:bb:cc:dd:ee:ff
Internet Protocol Version 4, Src: 192.168.1.1, Dst: 192.168.1.2
User Datagram Protocol, Src Port: 12345, Dst Port: 80
Data (45 bytes): "Hello UDP! This is a DPDK mbuf demo packet."
```

### 3.7 关键知识点

1. **封装顺序**：从内到外（先 payload，再 UDP，再 IP，最后以太网）
2. **字节序转换**：网络字节序（大端）需要用 `htons()`、`htonl()` 转换
3. **长度计算**：每层协议都需要计算并填充长度字段
4. **Prepend 特性**：每次 prepend 都会把数据指针前移，自动利用 headroom
5. **libpcap 写入**：使用 `pcap_open_dead()` + `pcap_dump_open()` + `pcap_dump()` 写入 pcap 文件

---

## 总结

### 你现在应该掌握的知识

✅ 理解 Mbuf 是什么，为什么需要它
✅ 知道 Mbuf 的内部结构（元数据、headroom、data、tailroom）
✅ 会创建 Mbuf 内存池
✅ 会分配和释放 Mbuf
✅ 会使用 append 和 prepend 添加数据
✅ 理解为什么 headroom 很重要
✅ 能够构造简单的网络数据包

### 参考资源

- **项目配套代码**：参考本项目的 `13-mbuf_usage` 目录

---

**祝你学习愉快！Have fun with DPDK!** 🚀
