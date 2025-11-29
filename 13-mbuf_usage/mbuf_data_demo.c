/**
 * mbuf_data_demo.c - Mbuf 数据操作演示
 *
 * 本程序演示如何向 Mbuf 中添加数据：
 * 1. append - 在数据尾部追加
 * 2. prepend - 在数据头部添加
 * 3. 理解 headroom 和 tailroom 的变化
 *
 * 编译: make mbuf_data_demo
 * 运行: sudo ./bin/mbuf_data_demo -l 0 --no-pci
 */

#include <stdio.h>
#include <string.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

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

int main(int argc, char *argv[])
{
    struct rte_mempool *pool;
    struct rte_mbuf *mbuf;
    int ret;

    /* 初始化 EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        printf("EAL init failed\n");
        return -1;
    }

    printf("\n");
    printf("========================================\n");
    printf("      Mbuf Data Operations Demo\n");
    printf("========================================\n\n");

    /* 创建内存池 */
    pool = rte_pktmbuf_pool_create("DATA_DEMO_POOL", 1024, 256, 0,
                                    RTE_MBUF_DEFAULT_BUF_SIZE,
                                    rte_socket_id());
    if (pool == NULL) {
        printf("Create pool failed\n");
        rte_eal_cleanup();
        return -1;
    }

    /* 分配 mbuf */
    mbuf = rte_pktmbuf_alloc(pool);
    if (mbuf == NULL) {
        printf("Alloc mbuf failed\n");
        rte_eal_cleanup();
        return -1;
    }

    /* ========== 实验1: append 操作 ========== */
    printf("[Experiment 1] Append Data\n");
    printf("-----------------------------------------\n");

    print_mbuf_layout(mbuf, "Initial state (empty mbuf)");

    /* 要添加的消息 */
    const char *message = "Hello DPDK!";
    size_t msg_len = strlen(message) + 1;  /* 包含 '\0' */

    printf("\n  Adding message: \"%s\" (%zu bytes)\n\n", message, msg_len);

    /* 在尾部追加空间 */
    char *data = (char *)rte_pktmbuf_append(mbuf, msg_len);
    if (data == NULL) {
        printf("Append failed!\n");
        rte_pktmbuf_free(mbuf);
        rte_eal_cleanup();
        return -1;
    }

    /* 复制数据 */
    strcpy(data, message);

    print_mbuf_layout(mbuf, "After append");

    /* 读取数据 */
    char *read_data = rte_pktmbuf_mtod(mbuf, char *);
    printf("  Data content: \"%s\"\n\n", read_data);

    /* ========== 实验2: prepend 操作 ========== */
    printf("[Experiment 2] Prepend Header\n");
    printf("-----------------------------------------\n");

    print_mbuf_layout(mbuf, "Before prepend");

    /* 在头部添加自定义头 */
    struct simple_header *hdr;
    hdr = (struct simple_header *)rte_pktmbuf_prepend(mbuf, sizeof(*hdr));
    if (hdr == NULL) {
        printf("Prepend failed!\n");
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
    printf("    magic:   0x%08X\n", hdr->magic);
    printf("    version: %u\n", hdr->version);
    printf("    length:  %u\n", hdr->length);

    /* 读取头部后面的 payload */
    char *payload = rte_pktmbuf_mtod_offset(mbuf, char *, sizeof(*hdr));
    printf("    payload: \"%s\"\n\n", payload);

    /* ========== 实验3: 数据包结构可视化 ========== */
    printf("[Result] Final Packet Structure\n");
    printf("-----------------------------------------\n");
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
    printf("  Total data_len: %u bytes\n\n", mbuf->data_len);

    /* ========== 实验4: adj 和 trim 操作 ========== */
    printf("[Experiment 3] Remove Data (adj/trim)\n");
    printf("-----------------------------------------\n");

    print_mbuf_layout(mbuf, "Before operations");

    /* adj: 从头部移除数据 (去掉头部) */
    char *adj_result = rte_pktmbuf_adj(mbuf, sizeof(struct simple_header));
    if (adj_result == NULL) {
        printf("Adj failed!\n");
    } else {
        printf("\n  After rte_pktmbuf_adj (remove header):\n");
        print_mbuf_layout(mbuf, "After adj");
        printf("  Data now: \"%s\"\n", rte_pktmbuf_mtod(mbuf, char *));
    }

    /* trim: 从尾部移除数据 */
    int trim_result = rte_pktmbuf_trim(mbuf, 1);  /* 去掉 '\0' */
    if (trim_result != 0) {
        printf("Trim failed!\n");
    } else {
        printf("\n  After rte_pktmbuf_trim (remove 1 byte from tail):\n");
        print_mbuf_layout(mbuf, "After trim");
    }

    printf("\n");

    /* 清理 */
    rte_pktmbuf_free(mbuf);
    rte_eal_cleanup();

    printf("========================================\n");
    printf("           Demo Completed!\n");
    printf("========================================\n\n");

    return 0;
}
