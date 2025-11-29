/**
 * mbuf_hello.c - DPDK Mbuf 入门示例
 *
 * 本程序演示 Mbuf 的完整生命周期：
 * 1. 创建 Mbuf 内存池
 * 2. 分配 Mbuf
 * 3. 查看 Mbuf 信息
 * 4. 释放 Mbuf
 *
 * 编译: make mbuf_hello
 * 运行: sudo ./bin/mbuf_hello -l 0 --no-pci
 */

#include <stdio.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_errno.h>

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
    printf("========================================\n");
    printf("     DPDK Mbuf Hello World Demo\n");
    printf("========================================\n\n");

    /* ===== 第2步：创建 Mbuf 内存池 ===== */
    printf("[Step 1] Create Mbuf Pool\n");
    printf("  - Pool size: 8192 mbufs\n");
    printf("  - Cache size: 256 per core\n");
    printf("  - Buffer size: %d bytes\n\n", RTE_MBUF_DEFAULT_BUF_SIZE);

    mbuf_pool = rte_pktmbuf_pool_create(
        "HELLO_MBUF_POOL",          /* 内存池名称 */
        8192,                        /* mbuf 数量 */
        256,                         /* 每个 CPU 缓存大小 */
        0,                           /* 私有数据大小 */
        RTE_MBUF_DEFAULT_BUF_SIZE,   /* 数据缓冲区大小 */
        rte_socket_id()              /* NUMA socket ID */
    );

    if (mbuf_pool == NULL) {
        printf("ERROR: Cannot create mbuf pool: %s\n",
               rte_strerror(rte_errno));
        rte_eal_cleanup();
        return -1;
    }
    printf("  [OK] Pool created successfully!\n\n");

    /* ===== 第3步：从池中分配一个 Mbuf ===== */
    printf("[Step 2] Allocate one Mbuf\n");

    mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (mbuf == NULL) {
        printf("ERROR: Cannot allocate mbuf\n");
        rte_eal_cleanup();
        return -1;
    }
    printf("  [OK] Mbuf allocated!\n\n");

    /* ===== 第4步：查看 Mbuf 信息 ===== */
    printf("[Step 3] Mbuf Information\n");
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

    printf("  Memory Layout:\n");
    printf("  +-------------------+\n");
    printf("  | Headroom: %4u    |  (space before data)\n", headroom);
    printf("  +-------------------+\n");
    printf("  | Data:     %4u    |  (actual packet data)\n", mbuf->data_len);
    printf("  +-------------------+\n");
    printf("  | Tailroom: %4u    |  (space after data)\n", tailroom);
    printf("  +-------------------+\n");
    printf("  Total available: %u bytes\n\n", headroom + tailroom);

    /* ===== 第5步：释放 Mbuf ===== */
    printf("[Step 4] Free Mbuf\n");
    rte_pktmbuf_free(mbuf);
    printf("  [OK] Mbuf returned to pool!\n\n");

    /* ===== 清理并退出 ===== */
    rte_eal_cleanup();

    printf("========================================\n");
    printf("           Demo Completed!\n");
    printf("========================================\n\n");

    return 0;
}
