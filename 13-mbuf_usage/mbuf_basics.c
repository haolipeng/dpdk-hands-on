/**
 * mbuf_basics.c - DPDK Mbuf 基础入门示例
 *
 * 本程序演示 Mbuf 的完整使用流程：
 * 1. 创建 Mbuf 内存池
 * 2. 分配 Mbuf 并查看其结构
 * 3. 使用 append 添加数据
 * 4. 使用 prepend 添加头部
 * 5. 使用 adj/trim 移除数据
 * 6. 释放 Mbuf
 *
 * 编译: make mbuf_basics
 * 运行: sudo ./bin/mbuf_basics -l 0 --no-pci
 */

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
           rte_pktmbuf_headroom(m),
           m->data_len,
           rte_pktmbuf_tailroom(m));
}

/* 打印分隔线 */
static void print_separator(const char *title)
{
    printf("\n");
    printf("==========================================\n");
    printf("  %s\n", title);
    printf("==========================================\n\n");
}

int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    struct rte_mbuf *mbuf;
    int ret;

    /* ===== 第1步：初始化 DPDK 环境 ===== */
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        printf("DPDK EAL init failed\n");
        return -1;
    }

    printf("\n");
    printf("############################################\n");
    printf("#     DPDK Mbuf Basics Tutorial Demo       #\n");
    printf("############################################\n");

    /*
     * =========================================================
     * Part 1: 创建 Mbuf 内存池并查看 Mbuf 结构
     * =========================================================
     */
    print_separator("Part 1: Create Mbuf Pool & Inspect Structure");

    printf("[Step 1.1] Create Mbuf Pool\n");
    printf("  - Pool size: 8192 mbufs\n");
    printf("  - Cache size: 256 per core\n");
    printf("  - Buffer size: %d bytes\n\n", RTE_MBUF_DEFAULT_BUF_SIZE);

    mbuf_pool = rte_pktmbuf_pool_create(
        "BASICS_MBUF_POOL",          /* 内存池名称 */
        8192,                         /* mbuf 数量 */
        256,                          /* 每个 CPU 缓存大小 */
        0,                            /* 私有数据大小 */
        RTE_MBUF_DEFAULT_BUF_SIZE,    /* 数据缓冲区大小 */
        rte_socket_id()               /* NUMA socket ID */
    );

    if (mbuf_pool == NULL) {
        printf("ERROR: Cannot create mbuf pool: %s\n",
               rte_strerror(rte_errno));
        rte_eal_cleanup();
        return -1;
    }
    printf("  [OK] Pool created successfully!\n\n");

    /* 分配一个 Mbuf */
    printf("[Step 1.2] Allocate one Mbuf\n");

    mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (mbuf == NULL) {
        printf("ERROR: Cannot allocate mbuf\n");
        rte_eal_cleanup();
        return -1;
    }
    printf("  [OK] Mbuf allocated!\n\n");

    /* 查看 Mbuf 详细信息 */
    printf("[Step 1.3] Inspect Mbuf Structure\n");
    printf("  +----------------------------------+\n");
    printf("  | Basic Info                       |\n");
    printf("  +----------------------------------+\n");
    printf("  | Address:     %p     |\n", (void *)mbuf);
    printf("  | Pool name:   %-18s |\n", mbuf->pool->name);
    printf("  | Ref count:   %-18u |\n", rte_mbuf_refcnt_read(mbuf));
    printf("  +----------------------------------+\n");
    printf("  | Buffer Layout                    |\n");
    printf("  +----------------------------------+\n");
    printf("  | buf_len:     %-5u bytes         |\n", mbuf->buf_len);
    printf("  | data_off:    %-5u bytes         |\n", mbuf->data_off);
    printf("  | data_len:    %-5u bytes         |\n", mbuf->data_len);
    printf("  +----------------------------------+\n\n");

    /* 计算各区域大小 */
    uint16_t headroom = rte_pktmbuf_headroom(mbuf);
    uint16_t tailroom = rte_pktmbuf_tailroom(mbuf);

    printf("  Memory Layout (empty mbuf):\n");
    printf("  +-------------------+\n");
    printf("  | Headroom: %4u    |  (space before data, for headers)\n", headroom);
    printf("  +-------------------+\n");
    printf("  | Data:     %4u    |  (actual packet data, currently empty)\n", mbuf->data_len);
    printf("  +-------------------+\n");
    printf("  | Tailroom: %4u    |  (space after data)\n", tailroom);
    printf("  +-------------------+\n");
    printf("  Total available: %u bytes\n", headroom + tailroom);

    /*
     * =========================================================
     * Part 2: 使用 append 添加数据
     * =========================================================
     */
    print_separator("Part 2: Append Data (add to tail)");

    print_mbuf_layout(mbuf, "Initial state (empty mbuf)");

    /* 要添加的消息 */
    const char *message = "Hello DPDK!";
    size_t msg_len = strlen(message) + 1;  /* 包含 '\0' */

    printf("\n  Adding message: \"%s\" (%zu bytes)\n\n", message, msg_len);

    /* 在尾部追加空间 */
    char *data = (char *)rte_pktmbuf_append(mbuf, msg_len);
    if (data == NULL) {
        printf("ERROR: Append failed!\n");
        rte_pktmbuf_free(mbuf);
        rte_eal_cleanup();
        return -1;
    }

    /* 复制数据 */
    strcpy(data, message);

    print_mbuf_layout(mbuf, "After append");

    /* 读取数据 */
    char *read_data = rte_pktmbuf_mtod(mbuf, char *);
    printf("  Data content: \"%s\"\n", read_data);

    printf("\n  Key Point: append() reduces tailroom, increases data_len\n");

    /*
     * =========================================================
     * Part 3: 使用 prepend 添加头部
     * =========================================================
     */
    print_separator("Part 3: Prepend Header (add to head)");

    print_mbuf_layout(mbuf, "Before prepend");

    /* 在头部添加自定义头 */
    struct simple_header *hdr;
    hdr = (struct simple_header *)rte_pktmbuf_prepend(mbuf, sizeof(*hdr));
    if (hdr == NULL) {
        printf("ERROR: Prepend failed!\n");
        rte_pktmbuf_free(mbuf);
        rte_eal_cleanup();
        return -1;
    }

    /* 填充头部信息 */
    hdr->magic = 0xDEADBEEF;
    hdr->version = 1;
    hdr->length = msg_len;

    print_mbuf_layout(mbuf, "After prepend");

    printf("\n  Header info:\n");
    printf("    prepend data len:   %lu\n", sizeof(*hdr));

    /* 读取头部后面的 payload */
    char *payload = rte_pktmbuf_mtod_offset(mbuf, char *, sizeof(*hdr));
    printf("    payload: \"%s\"\n", payload);

    printf("\n  Key Point: prepend() reduces headroom, increases data_len\n");

    /*
     * =========================================================
     * Part 4: 数据包结构可视化
     * =========================================================
     */
    print_separator("Part 4: Final Packet Structure");

    printf("  +---------------------------+\n");
    printf("  | Headroom: %4u bytes      |\n", rte_pktmbuf_headroom(mbuf));
    printf("  +---------------------------+\n");
    printf("  | Header:   %4zu bytes      |  <- magic, version, length\n",
           sizeof(struct simple_header));
    printf("  +---------------------------+\n");
    printf("  | Payload:  %4zu bytes      |  <- \"%s\"\n", msg_len, message);
    printf("  +---------------------------+\n");
    printf("  | Tailroom: %4u bytes      |\n", rte_pktmbuf_tailroom(mbuf));
    printf("  +---------------------------+\n");
    printf("  Total data_len: %u bytes\n", mbuf->data_len);

    /*
     * =========================================================
     * Part 5: 使用 adj 和 trim 移除数据
     * =========================================================
     */
    print_separator("Part 5: Remove Data (adj/trim)");

    print_mbuf_layout(mbuf, "Before operations");

    /* adj: 从头部移除数据 (去掉头部) */
    printf("\n  [5.1] rte_pktmbuf_adj - remove header from head\n");
    char *adj_result = rte_pktmbuf_adj(mbuf, sizeof(struct simple_header));
    if (adj_result == NULL) {
        printf("  ERROR: Adj failed!\n");
    } else {
        print_mbuf_layout(mbuf, "After adj (header removed)");
        printf("  Data now: \"%s\"\n", rte_pktmbuf_mtod(mbuf, char *));
        printf("  Key Point: adj() increases headroom, decreases data_len\n");
    }

    /* trim: 从尾部移除数据 */
    printf("\n  [5.2] rte_pktmbuf_trim - remove 1 byte from tail\n");
    int trim_result = rte_pktmbuf_trim(mbuf, 1);  /* 去掉 '\0' */
    if (trim_result != 0) {
        printf("  ERROR: Trim failed!\n");
    } else {
        print_mbuf_layout(mbuf, "After trim (1 byte removed)");
        printf("  Key Point: trim() increases tailroom, decreases data_len\n");
    }

    /*
     * =========================================================
     * Part 6: 释放 Mbuf
     * =========================================================
     */
    print_separator("Part 6: Free Mbuf");

    printf("  Returning mbuf to pool...\n");
    rte_pktmbuf_free(mbuf);
    printf("  [OK] Mbuf returned to pool and can be reused!\n");

    /* 清理并退出 */
    rte_eal_cleanup();

    return 0;
}
