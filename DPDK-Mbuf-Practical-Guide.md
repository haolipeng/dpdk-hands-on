# DPDK Mbuf 实战学习手册

> 从零开始掌握DPDK最核心的数据结构 - Mbuf（Message Buffer）
> 以动手实践为主，每个阶段都有可运行的代码和验收标准

---

## 阶段1：Mbuf基础概念与第一个程序

**学习目标**：理解Mbuf是什么，掌握最基本的分配和释放操作

### 1.1 理解Mbuf结构（预计30分钟）

#### Mbuf是什么？

**Mbuf = Message Buffer**，是DPDK中用于存储数据包的核心数据结构。

```
┌─────────────────────────────────────────────────────────┐
│                    rte_mbuf (元数据)                    │
│  - 数据长度、偏移                                       │
│  - 指向下一个mbuf的指针（链式结构）                     │
│  - offload标志、VLAN信息等                              │
├─────────────────────────────────────────────────────────┤
│             Headroom (预留空间)                         │
│          默认 RTE_PKTMBUF_HEADROOM 字节                 │
├─────────────────────────────────────────────────────────┤
│                                                         │
│           Data (实际数据包内容)                         │
│                                                         │
├─────────────────────────────────────────────────────────┤
│             Tailroom (剩余空间)                         │
└─────────────────────────────────────────────────────────┘
```

**关键概念**：

| 术语         | 说明                                         |
| ------------ | -------------------------------------------- |
| **Headroom** | 数据区前的预留空间，用于添加协议头（如封装） |
| **Data**     | 实际数据内容                                 |
| **Tailroom** | 数据区后的剩余空间，用于追加数据             |
| **Mempool**  | Mbuf的内存池，所有mbuf从pool中分配和释放     |

#### 实操任务：绘制Mbuf结构图

创建学习笔记：
```bash
vim notes/01_mbuf_structure.md
```

**任务**：
1. 画出单个mbuf的内存布局
2. 标注关键字段：data_off, data_len, buf_len
3. 解释headroom的作用

**验收标准**：
- [ ] 能够画出mbuf的内存布局
- [ ] 理解headroom和tailroom的区别
- [ ] 知道mbuf元数据和数据区域的关系

---

### 1.2 编写第一个Mbuf程序（预计45分钟）

#### 程序目标

创建一个最简单的程序：
1. 初始化DPDK EAL
2. 创建mbuf内存池
3. 分配一个mbuf
4. 打印mbuf信息
5. 释放mbuf

#### 实操步骤

**步骤1：创建项目结构**

```bash
cd ~/dpdk-mbuf-learning
mkdir -p stage1
cd stage1
```

**步骤2：编写源代码**

```bash
vim mbuf_hello.c
```

**代码内容**：

```c
/* mbuf_hello.c - DPDK Mbuf入门程序 */
#include <stdio.h>
#include <stdint.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define MBUF_CACHE_SIZE 256
#define NUM_MBUFS 8192
#define MBUF_DATA_SIZE RTE_MBUF_DEFAULT_BUF_SIZE

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    struct rte_mbuf *mbuf;
    int ret;

    /* 步骤1：初始化EAL环境 */
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "EAL初始化失败\n");
        return -1;
    }

    printf("=== DPDK Mbuf入门程序 ===\n\n");

    /* 步骤2：创建mbuf内存池 */
    printf("1. 创建mbuf内存池...\n");
    mbuf_pool = rte_pktmbuf_pool_create(
        "MBUF_POOL",           /* 内存池名称 */
        NUM_MBUFS,             /* mbuf数量 */
        MBUF_CACHE_SIZE,       /* 缓存大小 */
        0,                     /* 私有数据大小 */
        MBUF_DATA_SIZE,        /* 数据区大小 */
        rte_socket_id()        /* NUMA节点 */
    );

    if (mbuf_pool == NULL) {
        fprintf(stderr, "无法创建mbuf内存池\n");
        return -1;
    }

    printf("   ✓ 内存池创建成功\n");
    printf("   - 名称: MBUF_POOL\n");
    printf("   - 容量: %d个mbuf\n", NUM_MBUFS);
    printf("   - 数据区大小: %d字节\n\n", MBUF_DATA_SIZE);

    /* 步骤3：从内存池分配一个mbuf */
    printf("2. 分配mbuf...\n");
    mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (mbuf == NULL) {
        fprintf(stderr, "无法分配mbuf\n");
        return -1;
    }

    printf("   ✓ mbuf分配成功\n\n");

    /* 步骤4：打印mbuf信息 */
    printf("3. Mbuf详细信息:\n");
    printf("   - 内存地址: %p\n", (void *)mbuf);
    printf("   - 数据偏移(data_off): %u\n", mbuf->data_off);
    printf("   - 数据长度(data_len): %u\n", mbuf->data_len);
    printf("   - 缓冲区长度(buf_len): %u\n", mbuf->buf_len);
    printf("   - 引用计数(refcnt): %u\n", rte_mbuf_refcnt_read(mbuf));
    printf("   - 所属内存池: %s\n", mbuf->pool->name);

    /* 计算可用空间 */
    uint16_t headroom = rte_pktmbuf_headroom(mbuf);
    uint16_t tailroom = rte_pktmbuf_tailroom(mbuf);

    printf("\n4. 空间分配:\n");
    printf("   - Headroom: %u字节\n", headroom);
    printf("   - Data区域: %u字节 (当前为空)\n", mbuf->data_len);
    printf("   - Tailroom: %u字节\n", tailroom);
    printf("   - 总可用空间: %u字节\n", headroom + tailroom);

    /* 步骤5：释放mbuf */
    printf("\n5. 释放mbuf...\n");
    rte_pktmbuf_free(mbuf);
    printf("   ✓ mbuf已释放回内存池\n");

    /* 清理并退出 */
    rte_eal_cleanup();
    printf("\n=== 程序结束 ===\n");

    return 0;
}
```

**步骤3：编写Makefile**

```bash
vim Makefile
```

**Makefile内容**：

```makefile
# DPDK路径（根据实际情况修改）
DPDK_PATH ?= /path/to/dpdk
PKG_CONFIG_PATH = $(DPDK_PATH)/build/meson-private

# 编译器和标志
CC = gcc
CFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags libdpdk)
LDFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs libdpdk)

# 目标程序
TARGET = mbuf_hello

all: $(TARGET)

$(TARGET): mbuf_hello.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)

run:
	sudo ./$(TARGET) --no-pci

.PHONY: all clean run
```

**步骤4：编译和运行**

```bash
# 编译
make

# 运行（不需要网卡）
make run
```

**验收标准**：

预期输出：
```
=== DPDK Mbuf入门程序 ===

1. 创建mbuf内存池...
   ✓ 内存池创建成功
   - 名称: MBUF_POOL
   - 容量: 8192个mbuf
   - 数据区大小: 2176字节

2. 分配mbuf...
   ✓ mbuf分配成功

3. Mbuf详细信息:
   - 内存地址: 0x7f1234567890
   - 数据偏移(data_off): 128
   - 数据长度(data_len): 0
   - 缓冲区长度(buf_len): 2176
   - 引用计数(refcnt): 1
   - 所属内存池: MBUF_POOL

4. 空间分配:
   - Headroom: 128字节
   - Data区域: 0字节 (当前为空)
   - Tailroom: 2048字节
   - 总可用空间: 2176字节

5. 释放mbuf...
   ✓ mbuf已释放回内存池

=== 程序结束 ===
```

**检查清单**：
- [ ] 程序编译成功无警告
- [ ] 能够成功创建mbuf内存池
- [ ] 能够分配和释放mbuf
- [ ] 理解输出中的每个字段含义
- [ ] Headroom默认为128字节（RTE_PKTMBUF_HEADROOM）

---

### 1.3 实验：理解Headroom的作用（预计30分钟）

#### 实验目标

通过实验理解为什么需要headroom以及如何使用它。

#### 实操步骤

**步骤1：创建实验程序**

```bash
vim mbuf_headroom_demo.c
```

**代码内容**：

```c
/* mbuf_headroom_demo.c - Headroom实验 */
#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <arpa/inet.h>

#define NUM_MBUFS 1024
#define MBUF_CACHE_SIZE 256

/* 简单的IP头结构（简化版） */
struct simple_ip_hdr {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t packet_id;
    uint16_t fragment_offset;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
} __attribute__((packed));

/* 简单的以太网头 */
struct simple_eth_hdr {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ether_type;
} __attribute__((packed));

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    struct rte_mbuf *mbuf;
    struct simple_eth_hdr *eth;
    struct simple_ip_hdr *ip;
    char *payload;
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        return -1;

    printf("=== Headroom实验：封装协议头 ===\n\n");

    /* 创建内存池 */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
                                        MBUF_CACHE_SIZE, 0,
                                        RTE_MBUF_DEFAULT_BUF_SIZE,
                                        rte_socket_id());
    if (mbuf_pool == NULL) {
        fprintf(stderr, "创建内存池失败\n");
        return -1;
    }

    /* 分配mbuf */
    mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (mbuf == NULL) {
        fprintf(stderr, "分配mbuf失败\n");
        return -1;
    }

    printf("1. 初始状态:\n");
    printf("   - Headroom: %u字节\n", rte_pktmbuf_headroom(mbuf));
    printf("   - Data长度: %u字节\n", mbuf->data_len);
    printf("   - Tailroom: %u字节\n\n", rte_pktmbuf_tailroom(mbuf));

    /* 步骤1：添加payload（应用层数据） */
    printf("2. 添加payload数据...\n");
    const char *msg = "Hello DPDK!";
    payload = rte_pktmbuf_append(mbuf, strlen(msg) + 1);
    if (payload == NULL) {
        fprintf(stderr, "追加数据失败\n");
        return -1;
    }
    strcpy(payload, msg);

    printf("   - Payload: \"%s\"\n", msg);
    printf("   - Data长度: %u字节\n", mbuf->data_len);
    printf("   - Tailroom: %u字节\n\n", rte_pktmbuf_tailroom(mbuf));

    /* 步骤2：前置IP头（使用headroom） */
    printf("3. 前置IP头（利用headroom）...\n");
    ip = (struct simple_ip_hdr *)rte_pktmbuf_prepend(mbuf, sizeof(*ip));
    if (ip == NULL) {
        fprintf(stderr, "前置IP头失败\n");
        return -1;
    }

    /* 填充IP头 */
    memset(ip, 0, sizeof(*ip));
    ip->version_ihl = 0x45;  /* IPv4, 20字节头 */
    ip->ttl = 64;
    ip->protocol = 17;  /* UDP */
    ip->src_addr = htonl(0xC0A80101);  /* 192.168.1.1 */
    ip->dst_addr = htonl(0xC0A80102);  /* 192.168.1.2 */
    ip->total_length = htons(mbuf->data_len);

    printf("   - IP头大小: %lu字节\n", sizeof(*ip));
    printf("   - 源IP: 192.168.1.1\n");
    printf("   - 目的IP: 192.168.1.2\n");
    printf("   - Data长度: %u字节\n", mbuf->data_len);
    printf("   - Headroom: %u字节\n\n", rte_pktmbuf_headroom(mbuf));

    /* 步骤3：前置以太网头 */
    printf("4. 前置以太网头...\n");
    eth = (struct simple_eth_hdr *)rte_pktmbuf_prepend(mbuf, sizeof(*eth));
    if (eth == NULL) {
        fprintf(stderr, "前置以太网头失败\n");
        return -1;
    }

    /* 填充以太网头 */
    memset(eth->dst_mac, 0xFF, 6);  /* 广播地址 */
    memset(eth->src_mac, 0xAA, 6);  /* 源MAC */
    eth->ether_type = htons(0x0800);  /* IPv4 */

    printf("   - 以太网头大小: %lu字节\n", sizeof(*eth));
    printf("   - 源MAC: AA:AA:AA:AA:AA:AA\n");
    printf("   - 目的MAC: FF:FF:FF:FF:FF:FF\n");
    printf("   - 总数据长度: %u字节\n", mbuf->data_len);
    printf("   - 剩余Headroom: %u字节\n\n", rte_pktmbuf_headroom(mbuf));

    /* 总结 */
    printf("5. 最终数据包结构:\n");
    printf("   ┌────────────────────────┐\n");
    printf("   │ 以太网头 (14字节)     │\n");
    printf("   ├────────────────────────┤\n");
    printf("   │ IP头 (20字节)         │\n");
    printf("   ├────────────────────────┤\n");
    printf("   │ Payload (12字节)      │\n");
    printf("   └────────────────────────┘\n");
    printf("   总长度: %u字节\n\n", mbuf->data_len);

    printf("✓ Headroom的作用：无需数据拷贝即可添加协议头！\n");

    /* 清理 */
    rte_pktmbuf_free(mbuf);
    rte_eal_cleanup();

    return 0;
}
```

**步骤2：编译和运行**

```bash
# 添加到Makefile
echo "
headroom_demo: mbuf_headroom_demo.c
	\$(CC) \$(CFLAGS) \$< -o \$@ \$(LDFLAGS)

run-headroom: headroom_demo
	sudo ./headroom_demo --no-pci
" >> Makefile

make headroom_demo
make run-headroom
```

**验收标准**：

- [ ] 理解headroom的作用：无需数据拷贝即可添加协议头
- [ ] 看到headroom随着prepend操作逐渐减少
- [ ] 理解数据包的封装过程（从payload到完整帧）

**预期输出**：
```
=== Headroom实验：封装协议头 ===

1. 初始状态:
   - Headroom: 128字节
   - Data长度: 0字节
   - Tailroom: 2048字节

2. 添加payload数据...
   - Payload: "Hello DPDK!"
   - Data长度: 12字节
   - Tailroom: 2036字节

3. 前置IP头（利用headroom）...
   - IP头大小: 20字节
   - 源IP: 192.168.1.1
   - 目的IP: 192.168.1.2
   - Data长度: 32字节
   - Headroom: 108字节

4. 前置以太网头...
   - 以太网头大小: 14字节
   - 源MAC: AA:AA:AA:AA:AA:AA
   - 目的MAC: FF:FF:FF:FF:FF:FF
   - 总数据长度: 46字节
   - 剩余Headroom: 94字节

5. 最终数据包结构:
   ┌────────────────────────┐
   │ 以太网头 (14字节)     │
   ├────────────────────────┤
   │ IP头 (20字节)         │
   ├────────────────────────┤
   │ Payload (12字节)      │
   └────────────────────────┘
   总长度: 46字节

✓ Headroom的作用：无需数据拷贝即可添加协议头！
```

---

### 1.4 阶段1成果验收

**成果文件清单**：
```
stage1/
├── mbuf_hello.c              # 基础程序
├── mbuf_headroom_demo.c      # Headroom实验
├── Makefile                  # 编译配置
├── mbuf_hello                # 可执行文件
└── headroom_demo             # 可执行文件

notes/
└── 01_mbuf_structure.md      # 学习笔记
```

**必须完成的任务**：
1. [ ] mbuf_hello程序运行成功，输出符合预期
2. [ ] 理解mbuf的基本结构（元数据、headroom、data、tailroom）
3. [ ] headroom_demo程序运行成功
4. [ ] 能够解释headroom的作用和使用场景

**必须回答的问题**：
1. **Mbuf包含哪几部分？**
   - 答：元数据区域（rte_mbuf结构）、headroom、data区域、tailroom

2. **为什么需要headroom？**
   - 答：避免数据拷贝，直接在数据前添加协议头（如封装操作）

3. **rte_pktmbuf_append()和rte_pktmbuf_prepend()的区别？**
   - 答：append在数据末尾追加（使用tailroom），prepend在数据开头前置（使用headroom）

4. **mbuf从哪里分配？释放后去哪里？**
   - 答：从mempool分配，释放后返回到原mempool

#### 

## 阶段3：单段Mbuf数据操作

**学习目标**：掌握mbuf的数据读写、修改操作，实现简单的数据包处理工具

### 3.1 数据读写基础（预计45分钟）

#### 实操任务：数据包构造器

创建一个工具，可以构造不同类型的数据包。

```bash
cd ~/dpdk-mbuf-learning
mkdir -p stage3
cd stage3
vim packet_builder.c
```

**代码内容**：

```c
/* packet_builder.c - 数据包构造工具 */
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#define NUM_MBUFS 1024

/* 辅助函数：打印mbuf内容（十六进制） */
static void dump_mbuf_data(struct rte_mbuf *m, const char *title)
{
    uint8_t *data = rte_pktmbuf_mtod(m, uint8_t *);
    uint16_t len = m->data_len;
    int i;

    printf("\n%s (长度: %u字节)\n", title, len);
    printf("─────────────────────────────────────────────────\n");

    for (i = 0; i < len; i++) {
        printf("%02x ", data[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
        else if ((i + 1) % 8 == 0)
            printf(" ");
    }
    if (len % 16 != 0)
        printf("\n");
    printf("─────────────────────────────────────────────────\n");
}

/* 构造以太网头 */
static int build_eth_header(struct rte_mbuf *m,
                           const uint8_t *dst_mac,
                           const uint8_t *src_mac,
                           uint16_t ether_type)
{
    struct rte_ether_hdr *eth;

    /* 使用prepend在数据前添加以太网头 */
    eth = (struct rte_ether_hdr *)rte_pktmbuf_prepend(m, sizeof(*eth));
    if (eth == NULL)
        return -1;

    /* 填充字段 */
    rte_ether_addr_copy((const struct rte_ether_addr *)dst_mac,
                        &eth->dst_addr);
    rte_ether_addr_copy((const struct rte_ether_addr *)src_mac,
                        &eth->src_addr);
    eth->ether_type = rte_cpu_to_be_16(ether_type);

    return 0;
}

/* 构造IPv4头 */
static int build_ipv4_header(struct rte_mbuf *m,
                            uint32_t src_ip,
                            uint32_t dst_ip,
                            uint16_t total_len,
                            uint8_t protocol)
{
    struct rte_ipv4_hdr *ip;

    ip = (struct rte_ipv4_hdr *)rte_pktmbuf_prepend(m, sizeof(*ip));
    if (ip == NULL)
        return -1;

    /* 填充IPv4头 */
    ip->version_ihl = 0x45;  /* IPv4, 20字节头 */
    ip->type_of_service = 0;
    ip->total_length = rte_cpu_to_be_16(total_len);
    ip->packet_id = 0;
    ip->fragment_offset = 0;
    ip->time_to_live = 64;
    ip->next_proto_id = protocol;
    ip->hdr_checksum = 0;  /* 暂时不计算校验和 */
    ip->src_addr = rte_cpu_to_be_32(src_ip);
    ip->dst_addr = rte_cpu_to_be_32(dst_ip);

    return 0;
}

/* 构造UDP头 */
static int build_udp_header(struct rte_mbuf *m,
                           uint16_t src_port,
                           uint16_t dst_port,
                           uint16_t payload_len)
{
    struct rte_udp_hdr *udp;
    uint16_t udp_len = sizeof(*udp) + payload_len;

    udp = (struct rte_udp_hdr *)rte_pktmbuf_prepend(m, sizeof(*udp));
    if (udp == NULL)
        return -1;

    udp->src_port = rte_cpu_to_be_16(src_port);
    udp->dst_port = rte_cpu_to_be_16(dst_port);
    udp->dgram_len = rte_cpu_to_be_16(udp_len);
    udp->dgram_cksum = 0;  /* 暂时不计算校验和 */

    return 0;
}

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    struct rte_mbuf *pkt;
    char *payload;
    const char *msg = "Hello DPDK Packet!";
    uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t dst_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        return -1;

    printf("=== DPDK数据包构造器 ===\n");

    /* 创建内存池 */
    mbuf_pool = rte_pktmbuf_pool_create("PKT_POOL", NUM_MBUFS, 256, 0,
                                         RTE_MBUF_DEFAULT_DATAROOM,
                                         rte_socket_id());
    if (mbuf_pool == NULL) {
        fprintf(stderr, "创建内存池失败\n");
        return -1;
    }

    /* 分配mbuf */
    pkt = rte_pktmbuf_alloc(mbuf_pool);
    if (pkt == NULL) {
        fprintf(stderr, "分配mbuf失败\n");
        return -1;
    }

    printf("\n【步骤1】添加Payload\n");
    payload = rte_pktmbuf_append(pkt, strlen(msg) + 1);
    if (payload == NULL) {
        fprintf(stderr, "追加payload失败\n");
        return -1;
    }
    strcpy(payload, msg);
    printf("  Payload: \"%s\"\n", msg);
    printf("  Payload长度: %lu字节\n", strlen(msg) + 1);

    /* 构造UDP头 */
    printf("\n【步骤2】添加UDP头\n");
    if (build_udp_header(pkt, 12345, 80, strlen(msg) + 1) < 0) {
        fprintf(stderr, "构造UDP头失败\n");
        return -1;
    }
    printf("  源端口: 12345\n");
    printf("  目的端口: 80\n");
    printf("  UDP长度: %lu字节\n", sizeof(struct rte_udp_hdr) + strlen(msg) + 1);

    /* 构造IP头 */
    printf("\n【步骤3】添加IPv4头\n");
    uint16_t ip_len = sizeof(struct rte_ipv4_hdr) +
                      sizeof(struct rte_udp_hdr) +
                      strlen(msg) + 1;
    if (build_ipv4_header(pkt, 0xC0A80101, 0xC0A80102, ip_len, 17) < 0) {
        fprintf(stderr, "构造IP头失败\n");
        return -1;
    }
    printf("  源IP: 192.168.1.1\n");
    printf("  目的IP: 192.168.1.2\n");
    printf("  协议: UDP (17)\n");
    printf("  IP总长度: %u字节\n", ip_len);

    /* 构造以太网头 */
    printf("\n【步骤4】添加以太网头\n");
    if (build_eth_header(pkt, dst_mac, src_mac, RTE_ETHER_TYPE_IPV4) < 0) {
        fprintf(stderr, "构造以太网头失败\n");
        return -1;
    }
    printf("  源MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           src_mac[0], src_mac[1], src_mac[2],
           src_mac[3], src_mac[4], src_mac[5]);
    printf("  目的MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           dst_mac[0], dst_mac[1], dst_mac[2],
           dst_mac[3], dst_mac[4], dst_mac[5]);
    printf("  EtherType: 0x0800 (IPv4)\n");

    /* 打印最终数据包 */
    printf("\n【最终数据包】\n");
    printf("  总长度: %u字节\n", pkt->data_len);
    printf("  结构:\n");
    printf("    ┌─ 以太网头 (14字节)\n");
    printf("    ├─ IPv4头 (20字节)\n");
    printf("    ├─ UDP头 (8字节)\n");
    printf("    └─ Payload (%lu字节)\n", strlen(msg) + 1);

    /* 十六进制转储 */
    dump_mbuf_data(pkt, "完整数据包（十六进制）");

    /* 清理 */
    rte_pktmbuf_free(pkt);
    rte_eal_cleanup();

    printf("\n=== 程序结束 ===\n");
    return 0;
}
```

**编译运行**：

```bash
# Makefile
cat > Makefile << 'EOF'
DPDK_PATH ?= /path/to/dpdk
PKG_CONFIG_PATH = $(DPDK_PATH)/build/meson-private

CC = gcc
CFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags libdpdk)
LDFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs libdpdk)

all: packet_builder

packet_builder: packet_builder.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f packet_builder

run: packet_builder
	sudo ./packet_builder --no-pci

.PHONY: all clean run
EOF

make
make run
```

**验收标准**：
- [ ] 程序成功构造完整的UDP/IP/Ethernet数据包
- [ ] 能够看到数据包的十六进制转储
- [ ] 理解prepend和append的使用顺序（从内到外封装）

---

### 3.2 数据包解析器（预计45分钟）

#### 实操任务：实现数据包解析工具

与构造器相反，实现一个解析器：

```bash
vim packet_parser.c
```

**代码内容**：

```c
/* packet_parser.c - 数据包解析工具 */
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_tcp.h>

#define NUM_MBUFS 1024

/* 解析以太网头 */
static int parse_eth_header(struct rte_mbuf *m)
{
    struct rte_ether_hdr *eth;
    uint16_t ether_type;

    if (m->data_len < sizeof(*eth)) {
        printf("  ✗ 数据包太短，无法包含以太网头\n");
        return -1;
    }

    eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    ether_type = rte_be_to_cpu_16(eth->ether_type);

    printf("\n【以太网头】\n");
    printf("  源MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           eth->src_addr.addr_bytes[0], eth->src_addr.addr_bytes[1],
           eth->src_addr.addr_bytes[2], eth->src_addr.addr_bytes[3],
           eth->src_addr.addr_bytes[4], eth->src_addr.addr_bytes[5]);
    printf("  目的MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           eth->dst_addr.addr_bytes[0], eth->dst_addr.addr_bytes[1],
           eth->dst_addr.addr_bytes[2], eth->dst_addr.addr_bytes[3],
           eth->dst_addr.addr_bytes[4], eth->dst_addr.addr_bytes[5]);
    printf("  EtherType: 0x%04x", ether_type);

    switch (ether_type) {
    case RTE_ETHER_TYPE_IPV4:
        printf(" (IPv4)\n");
        break;
    case RTE_ETHER_TYPE_IPV6:
        printf(" (IPv6)\n");
        break;
    case RTE_ETHER_TYPE_ARP:
        printf(" (ARP)\n");
        break;
    default:
        printf(" (未知)\n");
    }

    return ether_type;
}

/* 解析IPv4头 */
static int parse_ipv4_header(struct rte_mbuf *m, uint16_t offset)
{
    struct rte_ipv4_hdr *ip;
    uint32_t src_ip, dst_ip;
    char src_ip_str[INET_ADDRSTRLEN];
    char dst_ip_str[INET_ADDRSTRLEN];

    if (m->data_len < offset + sizeof(*ip)) {
        printf("  ✗ 数据包太短，无法包含IP头\n");
        return -1;
    }

    ip = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *, offset);

    src_ip = rte_be_to_cpu_32(ip->src_addr);
    dst_ip = rte_be_to_cpu_32(ip->dst_addr);

    /* 转换为点分十进制 */
    struct in_addr addr;
    addr.s_addr = htonl(src_ip);
    inet_ntop(AF_INET, &addr, src_ip_str, sizeof(src_ip_str));
    addr.s_addr = htonl(dst_ip);
    inet_ntop(AF_INET, &addr, dst_ip_str, sizeof(dst_ip_str));

    printf("\n【IPv4头】\n");
    printf("  版本: %u\n", (ip->version_ihl >> 4));
    printf("  头长度: %u字节\n", (ip->version_ihl & 0x0F) * 4);
    printf("  总长度: %u字节\n", rte_be_to_cpu_16(ip->total_length));
    printf("  TTL: %u\n", ip->time_to_live);
    printf("  协议: %u", ip->next_proto_id);

    switch (ip->next_proto_id) {
    case 1:
        printf(" (ICMP)\n");
        break;
    case 6:
        printf(" (TCP)\n");
        break;
    case 17:
        printf(" (UDP)\n");
        break;
    default:
        printf(" (未知)\n");
    }

    printf("  源IP: %s\n", src_ip_str);
    printf("  目的IP: %s\n", dst_ip_str);

    return ip->next_proto_id;
}

/* 解析UDP头 */
static void parse_udp_header(struct rte_mbuf *m, uint16_t offset)
{
    struct rte_udp_hdr *udp;

    if (m->data_len < offset + sizeof(*udp)) {
        printf("  ✗ 数据包太短，无法包含UDP头\n");
        return;
    }

    udp = rte_pktmbuf_mtod_offset(m, struct rte_udp_hdr *, offset);

    printf("\n【UDP头】\n");
    printf("  源端口: %u\n", rte_be_to_cpu_16(udp->src_port));
    printf("  目的端口: %u\n", rte_be_to_cpu_16(udp->dst_port));
    printf("  UDP长度: %u字节\n", rte_be_to_cpu_16(udp->dgram_len));

    /* 尝试打印payload */
    uint16_t payload_offset = offset + sizeof(*udp);
    uint16_t payload_len = m->data_len - payload_offset;

    if (payload_len > 0) {
        char *payload = rte_pktmbuf_mtod_offset(m, char *, payload_offset);
        printf("\n【Payload】\n");
        printf("  长度: %u字节\n", payload_len);
        printf("  内容: ");

        /* 尝试打印为字符串（如果可打印） */
        int printable = 1;
        for (int i = 0; i < payload_len && i < 50; i++) {
            if (payload[i] < 32 || payload[i] > 126) {
                printable = 0;
                break;
            }
        }

        if (printable) {
            printf("\"%.*s\"\n", payload_len, payload);
        } else {
            printf("(二进制数据)\n");
        }
    }
}

/* 主解析函数 */
static void parse_packet(struct rte_mbuf *m)
{
    uint16_t ether_type;
    int proto;

    printf("\n=== 开始解析数据包 ===\n");
    printf("总长度: %u字节\n", m->data_len);

    /* 解析以太网头 */
    ether_type = parse_eth_header(m);
    if (ether_type < 0)
        return;

    /* 如果是IPv4，继续解析 */
    if (ether_type == RTE_ETHER_TYPE_IPV4) {
        proto = parse_ipv4_header(m, sizeof(struct rte_ether_hdr));
        if (proto < 0)
            return;

        /* 如果是UDP，继续解析 */
        if (proto == 17) {
            parse_udp_header(m, sizeof(struct rte_ether_hdr) +
                                sizeof(struct rte_ipv4_hdr));
        }
    }

    printf("\n=== 解析完成 ===\n");
}

/* 辅助函数：构造测试数据包（复用之前的代码） */
static struct rte_mbuf *create_test_packet(struct rte_mempool *pool)
{
    struct rte_mbuf *m;
    struct rte_ether_hdr *eth;
    struct rte_ipv4_hdr *ip;
    struct rte_udp_hdr *udp;
    char *payload;
    const char *msg = "Test UDP Packet";
    uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t dst_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    m = rte_pktmbuf_alloc(pool);
    if (m == NULL)
        return NULL;

    /* Payload */
    payload = rte_pktmbuf_append(m, strlen(msg) + 1);
    strcpy(payload, msg);

    /* UDP头 */
    udp = (struct rte_udp_hdr *)rte_pktmbuf_prepend(m, sizeof(*udp));
    udp->src_port = rte_cpu_to_be_16(12345);
    udp->dst_port = rte_cpu_to_be_16(80);
    udp->dgram_len = rte_cpu_to_be_16(sizeof(*udp) + strlen(msg) + 1);
    udp->dgram_cksum = 0;

    /* IP头 */
    ip = (struct rte_ipv4_hdr *)rte_pktmbuf_prepend(m, sizeof(*ip));
    ip->version_ihl = 0x45;
    ip->type_of_service = 0;
    ip->total_length = rte_cpu_to_be_16(sizeof(*ip) + sizeof(*udp) + strlen(msg) + 1);
    ip->packet_id = 0;
    ip->fragment_offset = 0;
    ip->time_to_live = 64;
    ip->next_proto_id = 17;  /* UDP */
    ip->hdr_checksum = 0;
    ip->src_addr = rte_cpu_to_be_32(0xC0A80101);  /* 192.168.1.1 */
    ip->dst_addr = rte_cpu_to_be_32(0xC0A80102);  /* 192.168.1.2 */

    /* 以太网头 */
    eth = (struct rte_ether_hdr *)rte_pktmbuf_prepend(m, sizeof(*eth));
    rte_ether_addr_copy((const struct rte_ether_addr *)dst_mac, &eth->dst_addr);
    rte_ether_addr_copy((const struct rte_ether_addr *)src_mac, &eth->src_addr);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    return m;
}

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    struct rte_mbuf *pkt;
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        return -1;

    printf("=== DPDK数据包解析器 ===\n");

    /* 创建内存池 */
    mbuf_pool = rte_pktmbuf_pool_create("PKT_POOL", NUM_MBUFS, 256, 0,
                                         RTE_MBUF_DEFAULT_DATAROOM,
                                         rte_socket_id());
    if (mbuf_pool == NULL) {
        fprintf(stderr, "创建内存池失败\n");
        return -1;
    }

    /* 创建测试数据包 */
    printf("\n【创建测试数据包】\n");
    pkt = create_test_packet(mbuf_pool);
    if (pkt == NULL) {
        fprintf(stderr, "创建测试数据包失败\n");
        return -1;
    }
    printf("✓ 测试数据包创建成功\n");

    /* 解析数据包 */
    parse_packet(pkt);

    /* 清理 */
    rte_pktmbuf_free(pkt);
    rte_eal_cleanup();

    return 0;
}
```

**编译运行**：

```bash
cat >> Makefile << 'EOF'

packet_parser: packet_parser.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run-parser: packet_parser
	sudo ./packet_parser --no-pci
EOF

make packet_parser
make run-parser
```

**验收标准**：
- [ ] 程序成功解析以太网/IP/UDP头
- [ ] 能够正确提取和显示各层协议信息
- [ ] 理解rte_pktmbuf_mtod_offset()的作用

---

### 3.3 阶段3成果验收

**成果文件清单**：
```
stage3/
├── packet_builder.c         # 数据包构造器
├── packet_parser.c          # 数据包解析器
├── Makefile
├── packet_builder           # 可执行文件
└── packet_parser            # 可执行文件
```

**必须回答的问题**：
1. **rte_pktmbuf_mtod()宏的作用是什么？**
   - 答：获取mbuf数据区的指针（mbuf to data）

2. **为什么封装时从内到外（payload→UDP→IP→Eth）？**
   - 答：因为使用prepend在数据前添加头，需要逆序构造

3. **rte_pktmbuf_mtod_offset()与rte_pktmbuf_mtod()的区别？**
   - 答：前者可以指定偏移量，用于跳过某些头部直接访问后续数据

---

## 阶段4：多段Mbuf处理（Multi-segment）

**学习目标**：理解和处理链式mbuf，实现Jumbo帧（超大帧）的处理

### 4.1 理解多段Mbuf（预计30分钟）

#### 为什么需要多段Mbuf？

当数据包大小超过单个mbuf能容纳的范围时（如Jumbo帧 > 9000字节），需要使用多个mbuf链接起来。

```
单段Mbuf (标准MTU 1500字节):
┌──────────────┐
│  rte_mbuf    │
│  data: 1500B │
└──────────────┘

多段Mbuf (Jumbo帧 9000字节):
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  rte_mbuf    │────>│  rte_mbuf    │────>│  rte_mbuf    │
│  data: 2048B │     │  data: 2048B │     │  data: 4904B │
│  next ───────┤     │  next ───────┤     │  next = NULL │
└──────────────┘     └──────────────┘     └──────────────┘
     (第1段)              (第2段)              (第3段)
```

**关键概念**：

| 字段         | 说明                                     |
| ------------ | ---------------------------------------- |
| **next**     | 指向下一个mbuf的指针                     |
| **nb_segs**  | 链中的段数（只有第一个mbuf有效）         |
| **pkt_len**  | 整个数据包的总长度（只有第一个mbuf有效） |
| **data_len** | 当前段的数据长度（每个mbuf都有）         |

#### 实操任务：创建学习笔记

```bash
cd ~/dpdk-mbuf-learning
mkdir -p notes
vim notes/04_multi_segment.md
```

**任务**：画出三段mbuf的链式结构，标注next指针和各段长度

**验收标准**：
- [ ] 理解next指针的作用
- [ ] 理解pkt_len与data_len的区别
- [ ] 知道nb_segs只在第一个mbuf有效

---

### 4.2 实现Jumbo帧生成器（预计60分钟）

#### 实操步骤

```bash
cd ~/dpdk-mbuf-learning
mkdir -p stage4
cd stage4
vim jumbo_frame.c
```

**代码内容**：

```c
/* jumbo_frame.c - Jumbo帧处理 */
#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define NUM_MBUFS 1024
#define MBUF_CACHE_SIZE 256

/* 创建指定大小的数据包（可能是多段） */
static struct rte_mbuf *create_packet(struct rte_mempool *pool, uint32_t size)
{
    struct rte_mbuf *pkt, *seg, *last_seg;
    uint32_t remaining = size;
    uint32_t seg_size;
    uint32_t seg_count = 0;
    char *data;

    /* 分配第一个mbuf */
    pkt = rte_pktmbuf_alloc(pool);
    if (pkt == NULL) {
        fprintf(stderr, "无法分配第一个mbuf\n");
        return NULL;
    }

    last_seg = pkt;
    seg_count++;

    /* 填充数据到第一个段 */
    seg_size = RTE_MIN(remaining, rte_pktmbuf_tailroom(pkt));
    data = rte_pktmbuf_append(pkt, seg_size);
    if (data == NULL) {
        rte_pktmbuf_free(pkt);
        return NULL;
    }
    memset(data, 'A', seg_size);
    remaining -= seg_size;

    /* 如果需要，分配额外的段 */
    while (remaining > 0) {
        seg = rte_pktmbuf_alloc(pool);
        if (seg == NULL) {
            fprintf(stderr, "分配段失败\n");
            rte_pktmbuf_free(pkt);
            return NULL;
        }

        seg_count++;

        /* 填充数据 */
        seg_size = RTE_MIN(remaining, rte_pktmbuf_tailroom(seg));
        data = rte_pktmbuf_append(seg, seg_size);
        if (data == NULL) {
            rte_pktmbuf_free(seg);
            rte_pktmbuf_free(pkt);
            return NULL;
        }
        memset(data, 'A' + (seg_count - 1), seg_size);
        remaining -= seg_size;

        /* 链接到前一个段 */
        last_seg->next = seg;
        last_seg = seg;

        /* 更新第一个mbuf的元数据 */
        pkt->nb_segs++;
        pkt->pkt_len += seg_size;
    }

    return pkt;
}

/* 打印mbuf链信息 */
static void print_mbuf_chain(struct rte_mbuf *m)
{
    struct rte_mbuf *seg;
    int seg_num = 0;

    printf("\n=== Mbuf链信息 ===\n");
    printf("第一个mbuf地址: %p\n", (void *)m);
    printf("总段数(nb_segs): %u\n", m->nb_segs);
    printf("总长度(pkt_len): %u字节\n", m->pkt_len);

    printf("\n各段详情:\n");
    printf("─────────────────────────────────────────────────\n");

    for (seg = m; seg != NULL; seg = seg->next) {
        printf("段 #%d:\n", seg_num);
        printf("  地址: %p\n", (void *)seg);
        printf("  data_len: %u字节\n", seg->data_len);
        printf("  data_off: %u\n", seg->data_off);
        printf("  next: %p\n", (void *)seg->next);

        /* 打印数据片段（前10字节） */
        uint8_t *data = rte_pktmbuf_mtod(seg, uint8_t *);
        printf("  数据预览: ");
        for (int i = 0; i < RTE_MIN(10, seg->data_len); i++) {
            printf("%c", data[i]);
        }
        printf("...\n");

        printf("─────────────────────────────────────────────────\n");
        seg_num++;
    }
}

/* 遍历并验证mbuf链 */
static int validate_mbuf_chain(struct rte_mbuf *m, uint32_t expected_len)
{
    struct rte_mbuf *seg;
    uint32_t total_len = 0;
    int seg_count = 0;

    for (seg = m; seg != NULL; seg = seg->next) {
        total_len += seg->data_len;
        seg_count++;
    }

    printf("\n=== 验证结果 ===\n");
    printf("预期总长度: %u字节\n", expected_len);
    printf("实际总长度: %u字节\n", total_len);
    printf("pkt_len字段: %u字节\n", m->pkt_len);
    printf("实际段数: %d\n", seg_count);
    printf("nb_segs字段: %u\n", m->nb_segs);

    if (total_len != expected_len) {
        printf("✗ 长度不匹配！\n");
        return -1;
    }

    if (total_len != m->pkt_len) {
        printf("✗ pkt_len不正确！\n");
        return -1;
    }

    if (seg_count != m->nb_segs) {
        printf("✗ nb_segs不正确！\n");
        return -1;
    }

    printf("✓ 所有验证通过！\n");
    return 0;
}

/* 实现mbuf链的合并（将所有段的数据合并到一个新的mbuf） */
static struct rte_mbuf *coalesce_mbuf(struct rte_mempool *pool,
                                     struct rte_mbuf *m)
{
    struct rte_mbuf *new_m, *seg;
    char *dst;
    uint32_t total_len = m->pkt_len;

    printf("\n=== 合并Mbuf链 ===\n");
    printf("原链段数: %u\n", m->nb_segs);
    printf("总长度: %u字节\n", total_len);

    /* 分配新mbuf（需要足够大的data_room） */
    new_m = rte_pktmbuf_alloc(pool);
    if (new_m == NULL) {
        fprintf(stderr, "分配新mbuf失败\n");
        return NULL;
    }

    /* 检查新mbuf空间是否足够 */
    if (rte_pktmbuf_tailroom(new_m) < total_len) {
        fprintf(stderr, "新mbuf空间不足\n");
        rte_pktmbuf_free(new_m);
        return NULL;
    }

    /* 复制所有段的数据 */
    dst = rte_pktmbuf_append(new_m, total_len);
    if (dst == NULL) {
        rte_pktmbuf_free(new_m);
        return NULL;
    }

    for (seg = m; seg != NULL; seg = seg->next) {
        char *src = rte_pktmbuf_mtod(seg, char *);
        rte_memcpy(dst, src, seg->data_len);
        dst += seg->data_len;
    }

    printf("✓ 合并完成，新mbuf段数: %u\n", new_m->nb_segs);

    return new_m;
}

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    struct rte_mbuf *jumbo_pkt, *coalesced_pkt;
    uint32_t jumbo_size = 9000;  /* 9KB Jumbo帧 */
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        return -1;

    printf("=== DPDK Jumbo帧处理 ===\n");

    /* 创建内存池 */
    mbuf_pool = rte_pktmbuf_pool_create("JUMBO_POOL", NUM_MBUFS,
                                         MBUF_CACHE_SIZE, 0,
                                         RTE_MBUF_DEFAULT_DATAROOM,
                                         rte_socket_id());
    if (mbuf_pool == NULL) {
        fprintf(stderr, "创建内存池失败\n");
        return -1;
    }

    /* 测试1：创建Jumbo帧 */
    printf("\n【测试1】创建%u字节的Jumbo帧\n", jumbo_size);
    jumbo_pkt = create_packet(mbuf_pool, jumbo_size);
    if (jumbo_pkt == NULL) {
        fprintf(stderr, "创建Jumbo帧失败\n");
        return -1;
    }

    print_mbuf_chain(jumbo_pkt);
    validate_mbuf_chain(jumbo_pkt, jumbo_size);

    /* 测试2：合并mbuf链 */
    printf("\n【测试2】合并mbuf链为单段\n");
    coalesced_pkt = coalesce_mbuf(mbuf_pool, jumbo_pkt);
    if (coalesced_pkt == NULL) {
        fprintf(stderr, "合并失败\n");
        rte_pktmbuf_free(jumbo_pkt);
        return -1;
    }

    print_mbuf_chain(coalesced_pkt);
    validate_mbuf_chain(coalesced_pkt, jumbo_size);

    /* 测试3：创建不同大小的数据包 */
    printf("\n【测试3】不同大小数据包的段数\n");
    printf("─────────────────────────────────────────────────\n");

    uint32_t test_sizes[] = {64, 1500, 2048, 4096, 9000, 16000};
    int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (int i = 0; i < num_tests; i++) {
        struct rte_mbuf *test_pkt = create_packet(mbuf_pool, test_sizes[i]);
        if (test_pkt != NULL) {
            printf("大小: %5u字节 → %u段\n",
                   test_sizes[i], test_pkt->nb_segs);
            rte_pktmbuf_free(test_pkt);
        }
    }

    printf("─────────────────────────────────────────────────\n");

    /* 清理 */
    rte_pktmbuf_free(jumbo_pkt);
    rte_pktmbuf_free(coalesced_pkt);

    rte_eal_cleanup();
    printf("\n=== 程序结束 ===\n");

    return 0;
}
```

**编译和运行**：

```bash
# Makefile
cat > Makefile << 'EOF'
DPDK_PATH ?= /home/work/dpdk-stable-24.11.1
PKG_CONFIG_PATH = $(DPDK_PATH)/build/meson-private

CC = gcc
CFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags libdpdk)
LDFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs libdpdk)

all: jumbo_frame

jumbo_frame: jumbo_frame.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f jumbo_frame

run: jumbo_frame
	sudo ./jumbo_frame --no-pci

.PHONY: all clean run
EOF

make
make run
```

**验收标准**：
- [ ] 程序成功创建9000字节的Jumbo帧
- [ ] 能够看到mbuf链的各段信息
- [ ] 理解nb_segs和pkt_len字段的作用
- [ ] 成功实现mbuf链的合并操作

**预期输出示例**：
```
=== DPDK Jumbo帧处理 ===

【测试1】创建9000字节的Jumbo帧

=== Mbuf链信息 ===
第一个mbuf地址: 0x7f1234567890
总段数(nb_segs): 5
总长度(pkt_len): 9000字节

各段详情:
─────────────────────────────────────────────────
段 #0:
  地址: 0x7f1234567890
  data_len: 2048字节
  data_off: 128
  next: 0x7f1234567A00
  数据预览: AAAAAAAAAA...
─────────────────────────────────────────────────
...

=== 验证结果 ===
预期总长度: 9000字节
实际总长度: 9000字节
pkt_len字段: 9000字节
实际段数: 5
nb_segs字段: 5
✓ 所有验证通过！
```

---

### 4.3 实现mbuf链遍历工具（预计30分钟）

#### 实操任务：实现通用的mbuf链操作函数

```bash
vim mbuf_chain_utils.c
```

**代码内容**：

```c
/* mbuf_chain_utils.c - Mbuf链操作工具集 */
#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define NUM_MBUFS 1024

/* 工具函数1：计算mbuf链的总长度（手动遍历） */
static uint32_t mbuf_chain_length(struct rte_mbuf *m)
{
    struct rte_mbuf *seg;
    uint32_t total = 0;

    for (seg = m; seg != NULL; seg = seg->next) {
        total += seg->data_len;
    }

    return total;
}

/* 工具函数2：读取mbuf链中指定偏移的数据 */
static int mbuf_read_at_offset(struct rte_mbuf *m, uint32_t offset,
                               void *buf, uint32_t len)
{
    struct rte_mbuf *seg;
    uint32_t seg_offset = 0;
    uint32_t copied = 0;

    if (offset + len > m->pkt_len) {
        fprintf(stderr, "读取越界\n");
        return -1;
    }

    /* 找到包含起始偏移的段 */
    for (seg = m; seg != NULL; seg = seg->next) {
        if (offset < seg_offset + seg->data_len) {
            /* 当前段包含起始位置 */
            uint32_t seg_start = offset - seg_offset;
            uint32_t seg_copy = RTE_MIN(seg->data_len - seg_start,
                                        len - copied);
            char *data = rte_pktmbuf_mtod_offset(seg, char *, seg_start);

            memcpy((char *)buf + copied, data, seg_copy);
            copied += seg_copy;

            if (copied >= len)
                break;
        }
        seg_offset += seg->data_len;
    }

    return copied;
}

/* 工具函数3：写入数据到mbuf链的指定偏移 */
static int mbuf_write_at_offset(struct rte_mbuf *m, uint32_t offset,
                                const void *buf, uint32_t len)
{
    struct rte_mbuf *seg;
    uint32_t seg_offset = 0;
    uint32_t written = 0;

    if (offset + len > m->pkt_len) {
        fprintf(stderr, "写入越界\n");
        return -1;
    }

    for (seg = m; seg != NULL; seg = seg->next) {
        if (offset < seg_offset + seg->data_len) {
            uint32_t seg_start = offset - seg_offset;
            uint32_t seg_write = RTE_MIN(seg->data_len - seg_start,
                                         len - written);
            char *data = rte_pktmbuf_mtod_offset(seg, char *, seg_start);

            memcpy(data, (const char *)buf + written, seg_write);
            written += seg_write;

            if (written >= len)
                break;
        }
        seg_offset += seg->data_len;
    }

    return written;
}

/* 工具函数4：打印mbuf链的统计信息 */
static void print_mbuf_stats(struct rte_mbuf *m, const char *title)
{
    struct rte_mbuf *seg;
    int seg_count = 0;
    uint32_t total_len = 0;

    printf("\n=== %s ===\n", title);

    for (seg = m; seg != NULL; seg = seg->next) {
        seg_count++;
        total_len += seg->data_len;
    }

    printf("段数: %d (nb_segs=%u)\n", seg_count, m->nb_segs);
    printf("总长度: %u字节 (pkt_len=%u)\n", total_len, m->pkt_len);
    printf("第一段data_len: %u字节\n", m->data_len);
    printf("Headroom: %u字节\n", rte_pktmbuf_headroom(m));
    printf("Tailroom: %u字节 (最后一段)\n",
           rte_pktmbuf_tailroom(rte_pktmbuf_lastseg(m)));
}

/* 创建测试mbuf链 */
static struct rte_mbuf *create_test_chain(struct rte_mempool *pool,
                                         uint32_t size)
{
    struct rte_mbuf *m, *seg, *last;
    uint32_t remaining = size;
    char fill_char = 'A';

    m = rte_pktmbuf_alloc(pool);
    if (m == NULL)
        return NULL;

    last = m;

    while (remaining > 0) {
        uint32_t chunk = RTE_MIN(remaining, rte_pktmbuf_tailroom(last));
        char *data = rte_pktmbuf_append(last, chunk);
        if (data == NULL) {
            rte_pktmbuf_free(m);
            return NULL;
        }

        memset(data, fill_char++, chunk);
        remaining -= chunk;

        if (remaining > 0) {
            seg = rte_pktmbuf_alloc(pool);
            if (seg == NULL) {
                rte_pktmbuf_free(m);
                return NULL;
            }
            last->next = seg;
            last = seg;
            m->nb_segs++;
            m->pkt_len += 0;  /* 会在append时更新 */
        }
    }

    return m;
}

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    struct rte_mbuf *chain;
    char read_buf[100];
    const char write_data[] = "MODIFIED";
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        return -1;

    printf("=== Mbuf链操作工具集测试 ===\n");

    mbuf_pool = rte_pktmbuf_pool_create("UTILS_POOL", NUM_MBUFS, 256, 0,
                                         RTE_MBUF_DEFAULT_DATAROOM,
                                         rte_socket_id());
    if (mbuf_pool == NULL) {
        fprintf(stderr, "创建内存池失败\n");
        return -1;
    }

    /* 创建5000字节的测试链 */
    printf("\n【测试1】创建5000字节的mbuf链\n");
    chain = create_test_chain(mbuf_pool, 5000);
    if (chain == NULL) {
        fprintf(stderr, "创建测试链失败\n");
        return -1;
    }

    print_mbuf_stats(chain, "初始状态");

    /* 测试2：读取数据 */
    printf("\n【测试2】从偏移100读取20字节\n");
    memset(read_buf, 0, sizeof(read_buf));
    ret = mbuf_read_at_offset(chain, 100, read_buf, 20);
    if (ret > 0) {
        printf("读取成功: %d字节\n", ret);
        printf("内容: ");
        for (int i = 0; i < ret; i++) {
            printf("%c", read_buf[i]);
        }
        printf("\n");
    }

    /* 测试3：写入数据 */
    printf("\n【测试3】向偏移200写入数据\n");
    ret = mbuf_write_at_offset(chain, 200, write_data, strlen(write_data));
    if (ret > 0) {
        printf("写入成功: %d字节\n", ret);
    }

    /* 验证写入 */
    memset(read_buf, 0, sizeof(read_buf));
    mbuf_read_at_offset(chain, 200, read_buf, strlen(write_data));
    printf("读回内容: %s\n", read_buf);

    /* 测试4：跨段读取 */
    printf("\n【测试4】跨段读取（偏移2040，跨第1和第2段）\n");
    memset(read_buf, 0, sizeof(read_buf));
    ret = mbuf_read_at_offset(chain, 2040, read_buf, 20);
    if (ret > 0) {
        printf("读取成功: %d字节\n", ret);
        printf("内容: ");
        for (int i = 0; i < ret; i++) {
            printf("%c", read_buf[i]);
        }
        printf("\n");
    }

    /* 测试5：计算长度 */
    printf("\n【测试5】手动计算链长度\n");
    uint32_t manual_len = mbuf_chain_length(chain);
    printf("手动遍历计算: %u字节\n", manual_len);
    printf("pkt_len字段: %u字节\n", chain->pkt_len);
    printf("匹配: %s\n", manual_len == chain->pkt_len ? "✓" : "✗");

    /* 清理 */
    rte_pktmbuf_free(chain);
    rte_eal_cleanup();

    printf("\n=== 测试完成 ===\n");
    return 0;
}
```

**编译运行**：

```bash
cat >> Makefile << 'EOF'

mbuf_chain_utils: mbuf_chain_utils.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run-utils: mbuf_chain_utils
	sudo ./mbuf_chain_utils --no-pci
EOF

make mbuf_chain_utils
make run-utils
```

**验收标准**：
- [ ] 能够跨段读取数据
- [ ] 能够跨段写入数据
- [ ] 理解如何遍历mbuf链
- [ ] 手动计算的长度与pkt_len匹配

---

### 4.4 阶段4成果验收

**成果文件清单**：
```
stage4/
├── jumbo_frame.c            # Jumbo帧生成和合并
├── mbuf_chain_utils.c       # Mbuf链操作工具集
├── Makefile
├── jumbo_frame              # 可执行文件
└── mbuf_chain_utils         # 可执行文件

notes/
└── 04_multi_segment.md      # 学习笔记
```

**必须回答的问题**：
1. **什么时候需要多段mbuf？**
   - 答：当数据包大小超过单个mbuf的容量时（如Jumbo帧 > 2KB）

2. **nb_segs和pkt_len字段在哪个mbuf中有效？**
   - 答：只在链的第一个mbuf中有效，代表整个链的信息

3. **如何释放多段mbuf？**
   - 答：调用rte_pktmbuf_free()释放第一个mbuf，DPDK会自动释放整条链

4. **如何遍历mbuf链？**
   - 答：使用for循环，通过next指针遍历：`for (seg = m; seg != NULL; seg = seg->next)`

---

## 阶段6：零拷贝与Mbuf克隆

**学习目标**：理解间接mbuf和克隆操作，实现零拷贝数据包处理

### 6.1 理解间接Mbuf（预计30分钟）

#### 什么是间接Mbuf？

**直接Mbuf**：拥有自己的数据缓冲区
**间接Mbuf**：数据指针指向另一个mbuf的数据缓冲区（零拷贝）

```
直接Mbuf:
┌──────────────┐
│  rte_mbuf    │
│  buf_addr ─┐ │
└────────────┼─┘
             │
             ▼
         ┌────────┐
         │  Data  │
         └────────┘

间接Mbuf (指向直接Mbuf):
┌──────────────┐       ┌──────────────┐
│ Indirect mbuf│       │  Direct mbuf │
│  buf_addr ─┼─┼──────>│  buf_addr ─┐ │
│  attached ─┼─┘       └────────────┼─┘
└─────────────┘                     │
                                    ▼
                                ┌────────┐
                                │  Data  │  (共享数据)
                                └────────┘
```

**使用场景**：
- 多播：同一个数据包发送给多个目标
- 数据包复制：保留原始数据包的副本

#### 实操任务：创建学习笔记

```bash
cd ~/dpdk-mbuf-learning
mkdir -p stage6 notes
vim notes/06_indirect_mbuf.md
```

**任务**：绘制间接mbuf的结构图，解释引用计数的作用

**验收标准**：
- [ ] 理解直接mbuf和间接mbuf的区别
- [ ] 知道间接mbuf的使用场景
- [ ] 理解引用计数（refcnt）的作用

---

### 6.2 实现Mbuf克隆（预计60分钟）

#### 实操步骤

```bash
cd ~/dpdk-mbuf-learning/stage6
vim mbuf_clone.c
```

**代码内容**：

```c
/* mbuf_clone.c - Mbuf克隆和零拷贝 */
#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

#define NUM_MBUFS 1024
#define NUM_CLONES 4

/* 打印mbuf的引用计数信息 */
static void print_refcnt_info(struct rte_mbuf *m, const char *title)
{
    printf("\n--- %s ---\n", title);
    printf("mbuf地址: %p\n", (void *)m);
    printf("buf_addr: %p\n", m->buf_addr);
    printf("refcnt: %u\n", rte_mbuf_refcnt_read(m));
    printf("是否直接mbuf: %s\n",
           RTE_MBUF_DIRECT(m) ? "是" : "否 (间接)");

    if (RTE_MBUF_HAS_EXTBUF(m)) {
        printf("包含外部缓冲\n");
    }

    if (!RTE_MBUF_DIRECT(m)) {
        struct rte_mbuf *direct = rte_mbuf_from_indirect(m);
        printf("指向的直接mbuf: %p (refcnt=%u)\n",
               (void *)direct, rte_mbuf_refcnt_read(direct));
    }
}

/* 创建测试数据包 */
static struct rte_mbuf *create_test_packet(struct rte_mempool *pool,
                                          const char *msg)
{
    struct rte_mbuf *m;
    char *data;
    uint16_t len = strlen(msg) + 1;

    m = rte_pktmbuf_alloc(pool);
    if (m == NULL)
        return NULL;

    data = rte_pktmbuf_append(m, len);
    if (data == NULL) {
        rte_pktmbuf_free(m);
        return NULL;
    }

    strcpy(data, msg);

    return m;
}

/* 场景1：基本克隆 */
static void demo_basic_clone(struct rte_mempool *direct_pool,
                            struct rte_mempool *indirect_pool)
{
    struct rte_mbuf *orig, *clone1, *clone2;

    printf("\n=== 场景1：基本克隆操作 ===\n");

    /* 创建原始数据包 */
    orig = create_test_packet(direct_pool, "Original Packet Data");
    if (orig == NULL) {
        fprintf(stderr, "创建原始数据包失败\n");
        return;
    }

    printf("\n【步骤1】创建原始数据包\n");
    print_refcnt_info(orig, "原始mbuf");

    /* 克隆数据包 */
    printf("\n【步骤2】克隆数据包（clone1）\n");
    clone1 = rte_pktmbuf_clone(orig, indirect_pool);
    if (clone1 == NULL) {
        fprintf(stderr, "克隆失败\n");
        rte_pktmbuf_free(orig);
        return;
    }

    print_refcnt_info(orig, "原始mbuf（克隆后）");
    print_refcnt_info(clone1, "克隆mbuf（clone1）");

    /* 再次克隆 */
    printf("\n【步骤3】再次克隆（clone2）\n");
    clone2 = rte_pktmbuf_clone(orig, indirect_pool);
    if (clone2 == NULL) {
        fprintf(stderr, "第二次克隆失败\n");
        rte_pktmbuf_free(orig);
        rte_pktmbuf_free(clone1);
        return;
    }

    print_refcnt_info(orig, "原始mbuf（再次克隆后）");
    print_refcnt_info(clone2, "克隆mbuf（clone2）");

    /* 验证数据共享 */
    printf("\n【步骤4】验证数据共享\n");
    char *orig_data = rte_pktmbuf_mtod(orig, char *);
    char *clone1_data = rte_pktmbuf_mtod(clone1, char *);
    char *clone2_data = rte_pktmbuf_mtod(clone2, char *);

    printf("原始数据地址: %p, 内容: \"%s\"\n", orig_data, orig_data);
    printf("clone1数据地址: %p, 内容: \"%s\"\n", clone1_data, clone1_data);
    printf("clone2数据地址: %p, 内容: \"%s\"\n", clone2_data, clone2_data);

    if (orig_data == clone1_data && orig_data == clone2_data) {
        printf("✓ 所有mbuf共享同一份数据（零拷贝）\n");
    }

    /* 释放顺序测试 */
    printf("\n【步骤5】释放clone1\n");
    rte_pktmbuf_free(clone1);
    print_refcnt_info(orig, "原始mbuf（释放clone1后）");

    printf("\n【步骤6】释放clone2\n");
    rte_pktmbuf_free(clone2);
    print_refcnt_info(orig, "原始mbuf（释放clone2后）");

    printf("\n【步骤7】释放原始mbuf\n");
    rte_pktmbuf_free(orig);
    printf("✓ 所有mbuf已释放，数据也被回收\n");
}

/* 场景2：多播场景（一对多发送） */
static void demo_multicast_scenario(struct rte_mempool *direct_pool,
                                   struct rte_mempool *indirect_pool)
{
    struct rte_mbuf *pkt;
    struct rte_mbuf *clones[NUM_CLONES];
    int i;

    printf("\n\n=== 场景2：多播场景（一对多） ===\n");

    /* 创建原始数据包 */
    pkt = create_test_packet(direct_pool, "Multicast Packet");
    if (pkt == NULL) {
        fprintf(stderr, "创建数据包失败\n");
        return;
    }

    printf("原始数据包: %u字节\n", pkt->pkt_len);
    print_refcnt_info(pkt, "原始数据包");

    /* 克隆多份（模拟发送到多个端口） */
    printf("\n【克隆%d份数据包】\n", NUM_CLONES);
    for (i = 0; i < NUM_CLONES; i++) {
        clones[i] = rte_pktmbuf_clone(pkt, indirect_pool);
        if (clones[i] == NULL) {
            fprintf(stderr, "克隆#%d失败\n", i);
            break;
        }
        printf("克隆#%d成功, 原始mbuf refcnt=%u\n",
               i, rte_mbuf_refcnt_read(pkt));
    }

    int num_clones = i;

    printf("\n【模拟发送到%d个端口】\n", num_clones);
    for (i = 0; i < num_clones; i++) {
        printf("发送克隆#%d到端口%d...\n", i, i);
        /* 实际场景中这里会调用rte_eth_tx_burst() */
        /* 发送完成后释放 */
        rte_pktmbuf_free(clones[i]);
        printf("  端口%d发送完成，refcnt=%u\n",
               i, rte_mbuf_refcnt_read(pkt));
    }

    printf("\n【释放原始数据包】\n");
    rte_pktmbuf_free(pkt);
    printf("✓ 多播完成，所有数据包已释放\n");

    printf("\n多播优势:\n");
    printf("  - 零拷贝：只有一份数据在内存中\n");
    printf("  - 高效：避免了%d次数据复制\n", num_clones);
    printf("  - 节省内存：%d个数据包只占用1份数据空间\n", num_clones + 1);
}

/* 场景3：引用计数管理 */
static void demo_refcnt_management(struct rte_mempool *pool)
{
    struct rte_mbuf *m;

    printf("\n\n=== 场景3：引用计数管理 ===\n");

    m = rte_pktmbuf_alloc(pool);
    if (m == NULL) {
        fprintf(stderr, "分配mbuf失败\n");
        return;
    }

    printf("\n【初始状态】\n");
    printf("refcnt = %u (新分配)\n", rte_mbuf_refcnt_read(m));

    printf("\n【手动增加引用】\n");
    rte_mbuf_refcnt_update(m, 1);
    printf("refcnt = %u (增加1)\n", rte_mbuf_refcnt_read(m));

    printf("\n【再次增加引用】\n");
    rte_mbuf_refcnt_update(m, 2);
    printf("refcnt = %u (增加2)\n", rte_mbuf_refcnt_read(m));

    printf("\n【减少引用】\n");
    rte_mbuf_refcnt_update(m, -1);
    printf("refcnt = %u (减少1)\n", rte_mbuf_refcnt_read(m));

    printf("\n【尝试释放mbuf】\n");
    printf("调用rte_pktmbuf_free()...\n");
    rte_pktmbuf_free(m);
    /* 注意：因为refcnt > 1，mbuf不会被真正释放 */

    printf("mbuf未被真正释放（refcnt > 1）\n");

    /* 注意：此处省略了后续释放操作，实际应用中需要
     * 确保所有引用都被正确释放 */

    printf("\n引用计数规则:\n");
    printf("  - 新分配: refcnt = 1\n");
    printf("  - 克隆: 直接mbuf的refcnt++\n");
    printf("  - 释放: refcnt--，当refcnt=0时真正释放\n");
    printf("  - 手动管理: 使用rte_mbuf_refcnt_update()\n");
}

/* 场景4：链式mbuf的克隆 */
static void demo_chained_clone(struct rte_mempool *direct_pool,
                              struct rte_mempool *indirect_pool)
{
    struct rte_mbuf *orig, *seg1, *seg2, *clone;

    printf("\n\n=== 场景4：链式Mbuf克隆 ===\n");

    /* 创建三段mbuf链 */
    orig = rte_pktmbuf_alloc(direct_pool);
    seg1 = rte_pktmbuf_alloc(direct_pool);
    seg2 = rte_pktmbuf_alloc(direct_pool);

    if (!orig || !seg1 || !seg2) {
        fprintf(stderr, "分配mbuf失败\n");
        return;
    }

    /* 填充数据并链接 */
    char *data;
    data = rte_pktmbuf_append(orig, 100);
    memset(data, 'A', 100);
    data = rte_pktmbuf_append(seg1, 100);
    memset(data, 'B', 100);
    data = rte_pktmbuf_append(seg2, 100);
    memset(data, 'C', 100);

    orig->next = seg1;
    seg1->next = seg2;
    orig->nb_segs = 3;
    orig->pkt_len = 300;

    printf("【原始链式mbuf】\n");
    printf("段数: %u\n", orig->nb_segs);
    printf("总长度: %u字节\n", orig->pkt_len);

    /* 克隆整条链 */
    printf("\n【克隆整条链】\n");
    clone = rte_pktmbuf_clone(orig, indirect_pool);
    if (clone == NULL) {
        fprintf(stderr, "克隆链式mbuf失败\n");
        goto cleanup;
    }

    printf("✓ 克隆成功\n");
    printf("克隆链段数: %u\n", clone->nb_segs);
    printf("克隆链长度: %u字节\n", clone->pkt_len);

    /* 验证引用计数 */
    printf("\n【验证引用计数】\n");
    struct rte_mbuf *orig_seg = orig;
    struct rte_mbuf *clone_seg = clone;
    int seg_num = 0;

    while (orig_seg && clone_seg) {
        printf("段#%d - 原始refcnt=%u, 克隆是间接mbuf=%s\n",
               seg_num,
               rte_mbuf_refcnt_read(orig_seg),
               RTE_MBUF_DIRECT(clone_seg) ? "否" : "是");
        orig_seg = orig_seg->next;
        clone_seg = clone_seg->next;
        seg_num++;
    }

    printf("\n✓ 每一段都被正确克隆（零拷贝）\n");

    /* 释放 */
    rte_pktmbuf_free(clone);

cleanup:
    rte_pktmbuf_free(orig);
}

int main(int argc, char *argv[])
{
    struct rte_mempool *direct_pool, *indirect_pool;
    int ret;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        return -1;

    printf("=== DPDK Mbuf克隆与零拷贝 ===\n");

    /* 创建直接mbuf内存池 */
    direct_pool = rte_pktmbuf_pool_create("DIRECT_POOL", NUM_MBUFS, 256, 0,
                                           RTE_MBUF_DEFAULT_DATAROOM,
                                           rte_socket_id());
    if (direct_pool == NULL) {
        fprintf(stderr, "创建直接内存池失败\n");
        return -1;
    }

    /* 创建间接mbuf内存池（data_room_size=0） */
    indirect_pool = rte_pktmbuf_pool_create("INDIRECT_POOL", NUM_MBUFS, 256, 0,
                                             0,  /* 间接mbuf不需要数据空间 */
                                             rte_socket_id());
    if (indirect_pool == NULL) {
        fprintf(stderr, "创建间接内存池失败\n");
        return -1;
    }

    printf("\n内存池创建成功:\n");
    printf("  - 直接mbuf池: data_room=%u\n", RTE_MBUF_DEFAULT_DATAROOM);
    printf("  - 间接mbuf池: data_room=0 (仅元数据)\n");

    /* 运行各个场景 */
    demo_basic_clone(direct_pool, indirect_pool);
    demo_multicast_scenario(direct_pool, indirect_pool);
    demo_refcnt_management(direct_pool);
    demo_chained_clone(direct_pool, indirect_pool);

    printf("\n\n=== 总结 ===\n");
    printf("Mbuf克隆的关键概念:\n");
    printf("  1. 零拷贝: 克隆共享数据，不复制\n");
    printf("  2. 引用计数: 自动管理生命周期\n");
    printf("  3. 间接mbuf: 指向直接mbuf的数据\n");
    printf("  4. 多播优化: 一份数据发送给多个目标\n");
    printf("\n使用要点:\n");
    printf("  1. 间接mbuf池的data_room设为0\n");
    printf("  2. 使用rte_pktmbuf_clone()克隆\n");
    printf("  3. 引用计数自动管理，无需手动干预\n");
    printf("  4. 支持链式mbuf的克隆\n");

    rte_eal_cleanup();
    printf("\n=== 程序结束 ===\n");

    return 0;
}
```

**编译运行**：

```bash
cat > Makefile << 'EOF'
DPDK_PATH ?= /home/work/dpdk-stable-24.11.1
PKG_CONFIG_PATH = $(DPDK_PATH)/build/meson-private

CC = gcc
CFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags libdpdk)
LDFLAGS = $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs libdpdk)

all: mbuf_clone

mbuf_clone: mbuf_clone.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f mbuf_clone

run: mbuf_clone
	sudo ./mbuf_clone --no-pci

.PHONY: all clean run
EOF

make
make run
```

**验收标准**：
- [ ] 理解克隆的零拷贝特性
- [ ] 观察到引用计数随克隆和释放变化
- [ ] 理解间接mbuf内存池的配置（data_room=0）
- [ ] 看到多播场景的内存节省效果

---

### 6.3 阶段6成果验收

**成果文件清单**：
```
stage6/
├── mbuf_clone.c             # Mbuf克隆实现
├── Makefile
└── mbuf_clone               # 可执行文件

notes/
└── 06_indirect_mbuf.md      # 学习笔记
```

**必须回答的问题**：
1. **什么是零拷贝？**
   - 答：多个mbuf共享同一份数据缓冲区，避免数据复制

2. **间接mbuf内存池为什么要设置data_room=0？**
   - 答：间接mbuf不拥有数据缓冲区，只需要元数据空间

3. **引用计数的作用？**
   - 答：跟踪有多少个mbuf引用同一份数据，当refcnt降为0时释放数据

4. **多播场景为什么使用克隆？**
   - 答：避免多次复制数据，节省内存和CPU时间

---

## 附录：常用API速查

### Mbuf分配和释放

```c
/* 分配mbuf */
struct rte_mbuf *m = rte_pktmbuf_alloc(pool);

/* 释放mbuf */
rte_pktmbuf_free(m);

/* 批量释放 */
rte_pktmbuf_free_bulk(mbufs, count);
```

### 数据操作

```c
/* 追加数据 */
char *data = rte_pktmbuf_append(m, len);

/* 前置数据 */
char *data = rte_pktmbuf_prepend(m, len);

/* 删除头部数据 */
char *data = rte_pktmbuf_adj(m, len);

/* 删除尾部数据 */
int ret = rte_pktmbuf_trim(m, len);

/* 获取数据指针 */
char *data = rte_pktmbuf_mtod(m, char *);

/* 获取指定偏移的数据指针 */
char *data = rte_pktmbuf_mtod_offset(m, char *, offset);
```

### 克隆和链操作

```c
/* 克隆mbuf */
struct rte_mbuf *clone = rte_pktmbuf_clone(m, indirect_pool);

/* 获取最后一段 */
struct rte_mbuf *last = rte_pktmbuf_lastseg(m);

/* 链接两个mbuf */
int ret = rte_pktmbuf_chain(head, tail);
```

### 元数据操作

```c
/* 设置协议层长度 */
m->l2_len = sizeof(struct rte_ether_hdr);
m->l3_len = sizeof(struct rte_ipv4_hdr);
m->l4_len = sizeof(struct rte_tcp_hdr);

/* 设置offload标志 */
m->ol_flags |= RTE_MBUF_F_TX_IP_CKSUM | RTE_MBUF_F_TX_TCP_CKSUM;

/* 设置VLAN */
m->vlan_tci = vlan_id;
m->ol_flags |= RTE_MBUF_F_TX_VLAN;
```

