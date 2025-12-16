/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Client-Server Example - Server
 *
 * 功能说明:
 * Server进程(Primary)负责:
 * 1. 创建共享内存池(pktmbuf pool)
 * 2. 为每个Client创建Ring队列
 * 3. 生成模拟数据包
 * 4. 按轮询(Round-Robin)方式分发给各个Client
 * 5. 打印统计信息
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
#include <rte_ether.h>
#include <rte_errno.h>

#include "common.h"

static volatile int force_quit = 0;

/* 配置参数 */
static uint32_t num_clients = 2;  // 默认2个client

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
            num_clients = atoi(optarg);
            if (num_clients == 0 || num_clients > MAX_CLIENTS) {
                fprintf(stderr, "无效的client数量: %u (范围: 1-%u)\n",
                        num_clients, MAX_CLIENTS);
                return -1;
            }
            break;
        default:
            fprintf(stderr, "用法: %s [EAL选项] -- [-n num_clients]\n",
                    prgname);
            return -1;
        }
    }

    return 0;
}

/* 创建模拟数据包 */
static struct rte_mbuf *
create_packet(struct rte_mempool *mp, uint32_t seq_num, uint32_t client_id)
{
    struct rte_mbuf *m;
    struct packet *pkt;

    m = rte_pktmbuf_alloc(mp);
    if (m == NULL)
        return NULL;

    /* 构造数据包 */
    pkt = rte_pktmbuf_mtod(m, struct packet *);
    memset(pkt, 0, sizeof(*pkt));

    /* 填充MAC地址(模拟) */
    memset(&pkt->src_mac, 0xAA, RTE_ETHER_ADDR_LEN);
    memset(&pkt->dst_mac, 0xBB, RTE_ETHER_ADDR_LEN);

    /* 填充数据 */
    pkt->seq_num = seq_num;
    pkt->timestamp = rte_get_tsc_cycles();
    snprintf(pkt->payload, sizeof(pkt->payload),
             "Packet #%u for Client %u", seq_num, client_id);

    m->pkt_len = sizeof(*pkt);
    m->data_len = sizeof(*pkt);

    return m;
}

int
main(int argc, char **argv)
{
    struct rte_mempool *pktmbuf_pool = NULL;
    struct rte_ring *client_rings[MAX_CLIENTS] = {NULL};
    int ret;
    uint32_t pkt_count = 0;
    uint32_t next_client = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    printf("=== Client-Server架构 - Server (Primary) ===\n\n");
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

    if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
        fprintf(stderr, "错误: Server必须作为Primary进程运行\n");
        rte_eal_cleanup();
        return -1;
    }

    printf("配置: %u 个Client进程\n\n", num_clients);

    /* 创建packet mbuf内存池 */
    printf("步骤1: 创建packet mbuf内存池...\n");
    pktmbuf_pool = rte_pktmbuf_pool_create(
        PKTMBUF_POOL_NAME,
        NUM_MBUFS * num_clients,
        MBUF_CACHE_SIZE,
        0,
        RTE_MBUF_DEFAULT_BUF_SIZE,
        rte_socket_id());

    if (pktmbuf_pool == NULL) {
        fprintf(stderr, "错误: 创建mbuf pool失败: %s\n",
                rte_strerror(rte_errno));
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ Mbuf pool创建成功 (总mbuf: %u)\n\n",
           NUM_MBUFS * num_clients);

    /* 为每个Client创建Ring队列 */
    printf("步骤2: 为每个Client创建Ring队列...\n");
    for (uint32_t i = 0; i < num_clients; i++) {
        char ring_name[32];
        snprintf(ring_name, sizeof(ring_name), CLIENT_RING_NAME, i);

        client_rings[i] = rte_ring_create(
            ring_name,
            RING_SIZE,
            rte_socket_id(),
            RING_F_SP_ENQ | RING_F_SC_DEQ);

        if (client_rings[i] == NULL) {
            fprintf(stderr, "错误: 创建Ring '%s' 失败\n", ring_name);
            rte_eal_cleanup();
            return -1;
        }
        printf("✓ Ring '%s' 创建成功\n", ring_name);
    }
    printf("\n");

    /* 提示信息 */
    printf("========================================\n");
    printf("Server已准备就绪!\n");
    printf("现在可以启动Client进程:\n");
    for (uint32_t i = 0; i < num_clients; i++) {
        printf("  Client %u: sudo ./bin/mp_cs_client -l %u "
               "--proc-type=secondary -- -n %u\n",
               i, i + 1, i);
    }
    printf("========================================\n\n");

    printf("Server开始生成并分发数据包...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* 主循环: 生成数据包并分发给Client */
    uint64_t last_stats_time = rte_get_tsc_cycles();
    uint64_t stats_interval = rte_get_tsc_hz() * 5;  // 5秒

    while (!force_quit) {
        struct rte_mbuf *pkts[BURST_SIZE];
        uint32_t nb_tx;

        /* 批量生成数据包 */
        for (uint32_t i = 0; i < BURST_SIZE; i++) {
            pkts[i] = create_packet(pktmbuf_pool, pkt_count++, next_client);
            if (pkts[i] == NULL) {
                fprintf(stderr, "警告: mbuf分配失败\n");
                /* 释放已分配的 */
                for (uint32_t j = 0; j < i; j++)
                    rte_pktmbuf_free(pkts[j]);
                goto wait_and_continue;
            }
        }

        /* 发送到对应Client的Ring */
        nb_tx = rte_ring_enqueue_burst(client_rings[next_client],
                                       (void **)pkts,
                                       BURST_SIZE,
                                       NULL);

        /* 释放未发送成功的包 */
        if (nb_tx < BURST_SIZE) {
            printf("[Server] 警告: Client %u Ring已满,丢弃 %u 个包\n",
                   next_client, BURST_SIZE - nb_tx);
            for (uint32_t i = nb_tx; i < BURST_SIZE; i++)
                rte_pktmbuf_free(pkts[i]);
        }

        /* 轮询到下一个Client */
        next_client = (next_client + 1) % num_clients;

wait_and_continue:
        /* 打印统计信息 */
        if (rte_get_tsc_cycles() - last_stats_time > stats_interval) {
            printf("\n--- Server统计 ---\n");
            printf("已生成数据包: %u\n", pkt_count);
            printf("Mbuf可用: %u\n",
                   rte_mempool_avail_count(pktmbuf_pool));

            for (uint32_t i = 0; i < num_clients; i++) {
                printf("Client %u Ring使用: %u/%u\n",
                       i,
                       rte_ring_count(client_rings[i]),
                       rte_ring_get_capacity(client_rings[i]));
            }
            printf("------------------\n\n");

            last_stats_time = rte_get_tsc_cycles();
        }

        /* 控制发送速率(约10Kpps) */
        usleep(3000);  // 3ms
    }

    /* 清理 */
    printf("\n清理资源...\n");
    printf("总共生成了 %u 个数据包\n", pkt_count);

    rte_eal_cleanup();
    printf("Server进程退出\n");

    return 0;
}
