/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Client-Server Example - Client
 *
 * 功能说明:
 * Client进程(Secondary)负责:
 * 1. 查找Server创建的共享内存池
 * 2. 查找属于自己的Ring队列
 * 3. 从Ring接收数据包
 * 4. 处理数据包(这里只是打印)
 * 5. 归还mbuf到内存池
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ring.h>
#include <rte_cycles.h>

#include "common.h"

static volatile int force_quit = 0;

/* Client配置 */
static uint32_t client_id = 0;

static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n收到退出信号...\n");
        force_quit = 1;
    }
}

/* 解析命令行参数 */
static int
parse_args(int argc, char **argv)
{
    int opt;
    const char *prgname = argv[0];

    while ((opt = getopt(argc, argv, "n:")) != -1) {
        switch (opt) {
        case 'n':
            client_id = atoi(optarg);
            if (client_id >= MAX_CLIENTS) {
                fprintf(stderr, "无效的client ID: %u (范围: 0-%u)\n",
                        client_id, MAX_CLIENTS - 1);
                return -1;
            }
            break;
        default:
            fprintf(stderr, "用法: %s [EAL选项] -- -n <client_id>\n",
                    prgname);
            return -1;
        }
    }

    return 0;
}

/* 处理接收到的数据包 */
static void
process_packet(struct rte_mbuf *m, uint32_t client_id, uint64_t *stats)
{
    struct packet *pkt;

    pkt = rte_pktmbuf_mtod(m, struct packet *);

    /* 简单处理: 打印包信息 */
    if (*stats % 1000 == 0) {  // 每1000个包打印一次
        printf("[Client %u] 处理包 #%u\n", client_id, pkt->seq_num);
        printf("           时间戳: %lu\n", pkt->timestamp);
        printf("           内容: %s\n\n", pkt->payload);
    }

    (*stats)++;
}

int
main(int argc, char **argv)
{
    struct rte_mempool *pktmbuf_pool = NULL;
    struct rte_ring *rx_ring = NULL;
    int ret;
    uint64_t rx_count = 0;
    char ring_name[32];

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL (Secondary模式) */
    printf("=== Client-Server架构 - Client (Secondary) ===\n\n");
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "错误: EAL初始化失败\n");
        return -1;
    }

    argc -= ret;
    argv += ret;

    /* 解析应用参数 */
    ret = parse_args(argc, argv);
    if (ret < 0) {
        rte_eal_cleanup();
        return -1;
    }

    if (rte_eal_process_type() != RTE_PROC_SECONDARY) {
        fprintf(stderr, "错误: Client必须作为Secondary进程运行\n");
        fprintf(stderr, "提示: 使用 --proc-type=secondary\n");
        rte_eal_cleanup();
        return -1;
    }

    printf("Client ID: %u\n\n", client_id);

    /* 查找共享mbuf内存池 */
    printf("步骤1: 查找共享mbuf内存池...\n");
    pktmbuf_pool = rte_mempool_lookup(PKTMBUF_POOL_NAME);
    if (pktmbuf_pool == NULL) {
        fprintf(stderr, "错误: 找不到mbuf pool '%s'\n", PKTMBUF_POOL_NAME);
        fprintf(stderr, "提示: 请先启动Server进程\n");
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ Mbuf pool查找成功\n\n");

    /* 查找自己的Ring队列 */
    printf("步骤2: 查找自己的Ring队列...\n");
    snprintf(ring_name, sizeof(ring_name), CLIENT_RING_NAME, client_id);

    rx_ring = rte_ring_lookup(ring_name);
    if (rx_ring == NULL) {
        fprintf(stderr, "错误: 找不到Ring '%s'\n", ring_name);
        fprintf(stderr, "提示: 请确保Server已创建此Ring\n");
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ Ring '%s' 查找成功\n\n", ring_name);

    /* 提示信息 */
    printf("========================================\n");
    printf("Client %u 已准备就绪!\n", client_id);
    printf("等待接收数据包...\n");
    printf("按 Ctrl+C 退出\n");
    printf("========================================\n\n");

    /* 主循环: 接收并处理数据包 */
    uint64_t last_stats_time = rte_get_tsc_cycles();
    uint64_t stats_interval = rte_get_tsc_hz() * 5;  // 5秒

    while (!force_quit) {
        struct rte_mbuf *pkts[BURST_SIZE];
        uint32_t nb_rx;

        /* 批量从Ring接收数据包 */
        nb_rx = rte_ring_dequeue_burst(rx_ring,
                                       (void **)pkts,
                                       BURST_SIZE,
                                       NULL);

        if (nb_rx > 0) {
            /* 处理接收到的数据包 */
            for (uint32_t i = 0; i < nb_rx; i++) {
                process_packet(pkts[i], client_id, &rx_count);

                /* 处理完成后释放mbuf */
                rte_pktmbuf_free(pkts[i]);
            }
        }

        /* 打印统计信息 */
        if (rte_get_tsc_cycles() - last_stats_time > stats_interval) {
            printf("--- Client %u 统计 ---\n", client_id);
            printf("已接收数据包: %lu\n", rx_count);
            printf("Ring使用: %u/%u\n",
                   rte_ring_count(rx_ring),
                   rte_ring_get_capacity(rx_ring));
            printf("Mbuf可用: %u\n",
                   rte_mempool_avail_count(pktmbuf_pool));
            printf("---------------------\n\n");

            last_stats_time = rte_get_tsc_cycles();
        }

        /* 如果Ring为空,短暂休眠 */
        if (nb_rx == 0) {
            usleep(1000);  // 1ms
        }
    }

    /* 清理 */
    printf("\n清理资源...\n");
    printf("Client %u 总共接收了 %lu 个数据包\n", client_id, rx_count);

    rte_eal_cleanup();
    printf("Client %u 进程退出\n", client_id);

    return 0;
}
