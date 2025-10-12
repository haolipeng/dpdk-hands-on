/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Ring Communication - Receiver (Secondary)
 *
 * 功能说明:
 * 1. 查找Primary创建的双向Ring队列
 * 2. 接收Primary发送的Ping消息
 * 3. 回复Pong消息给Primary
 * 4. 演示Secondary进程的通信能力
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#include "common.h"

static volatile int force_quit = 0;

static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n收到退出信号...\n");
        force_quit = 1;
    }
}

int
main(int argc, char **argv)
{
    struct rte_mempool *mp = NULL;
    struct rte_ring *ring_p2s = NULL;  // Primary to Secondary
    struct rte_ring *ring_s2p = NULL;  // Secondary to Primary
    int ret;
    uint32_t ping_received = 0;
    uint32_t pong_sent = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL (Secondary模式) */
    printf("=== Ring通信示例 - Receiver (Secondary) ===\n\n");
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "错误: EAL初始化失败\n");
        return -1;
    }

    if (rte_eal_process_type() != RTE_PROC_SECONDARY) {
        fprintf(stderr, "错误: 必须作为Secondary进程运行\n");
        fprintf(stderr, "提示: 使用 --proc-type=secondary\n");
        rte_eal_cleanup();
        return -1;
    }

    /* 查找共享内存池 */
    printf("查找共享内存池...\n");
    mp = rte_mempool_lookup(MEMPOOL_NAME);
    if (mp == NULL) {
        fprintf(stderr, "错误: 找不到内存池 '%s'\n", MEMPOOL_NAME);
        fprintf(stderr, "提示: 请先启动Sender进程\n");
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ 内存池查找成功\n\n");

    /* 查找双向Ring队列 */
    printf("查找双向Ring队列...\n");

    /* Ring 1: Primary -> Secondary */
    ring_p2s = rte_ring_lookup(RING_P2S_NAME);
    if (ring_p2s == NULL) {
        fprintf(stderr, "错误: 找不到Ring '%s'\n", RING_P2S_NAME);
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ Ring (Primary->Secondary) 查找成功\n");

    /* Ring 2: Secondary -> Primary */
    ring_s2p = rte_ring_lookup(RING_S2P_NAME);
    if (ring_s2p == NULL) {
        fprintf(stderr, "错误: 找不到Ring '%s'\n", RING_S2P_NAME);
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ Ring (Secondary->Primary) 查找成功\n\n");

    /* 提示信息 */
    printf("========================================\n");
    printf("Receiver进程已准备就绪!\n");
    printf("等待接收Ping消息...\n");
    printf("按 Ctrl+C 退出\n");
    printf("========================================\n\n");

    /* 主循环: 接收Ping,回复Pong */
    while (!force_quit) {
        struct comm_message *ping_msg;
        struct comm_message *pong_msg;

        /* 1. 从P2S Ring接收Ping消息 */
        ret = rte_ring_dequeue(ring_p2s, (void **)&ping_msg);
        if (ret == 0) {
            ping_received++;

            printf("[Receiver] 接收 Ping #%u\n", ping_msg->msg_id);
            printf("           发送者PID: %u\n", ping_msg->sender_pid);
            printf("           内容: %s\n", ping_msg->payload);

            /* 2. 获取新对象用于Pong消息 */
            if (rte_mempool_get(mp, (void **)&pong_msg) == 0) {
                /* 构造Pong消息(保留原始timestamp用于RTT计算) */
                pong_msg->msg_id = ping_msg->msg_id;
                pong_msg->msg_type = MSG_TYPE_PONG;
                pong_msg->sender_pid = getpid();
                pong_msg->timestamp = ping_msg->timestamp;  // 保留原始时间戳
                snprintf(pong_msg->payload, sizeof(pong_msg->payload),
                         "Pong #%u from Secondary", pong_msg->msg_id);

                /* 3. 发送Pong到S2P Ring */
                if (rte_ring_enqueue(ring_s2p, pong_msg) == 0) {
                    pong_sent++;
                    printf("[Receiver] 回复 Pong #%u\n\n", pong_msg->msg_id);
                } else {
                    fprintf(stderr, "警告: Ring已满,Pong消息丢弃\n");
                    rte_mempool_put(mp, pong_msg);
                }
            } else {
                fprintf(stderr, "警告: 内存池已空,无法发送Pong\n");
            }

            /* 归还Ping消息对象 */
            rte_mempool_put(mp, ping_msg);

        } else {
            /* Ring为空,等待 */
            usleep(100000);  // 100ms
        }

        /* 每10条消息打印统计 */
        if (ping_received > 0 && ping_received % 10 == 0) {
            printf("--- 统计 (Receiver) ---\n");
            printf("收到Ping: %u\n", ping_received);
            printf("发送Pong: %u\n", pong_sent);
            printf("内存池可用: %u\n", rte_mempool_avail_count(mp));
            printf("P2S Ring: %u/%u\n",
                   rte_ring_count(ring_p2s),
                   rte_ring_get_capacity(ring_p2s));
            printf("S2P Ring: %u/%u\n",
                   rte_ring_count(ring_s2p),
                   rte_ring_get_capacity(ring_s2p));
            printf("----------------------\n\n");
        }
    }

    /* 清理 */
    printf("\n清理资源...\n");
    printf("最终统计: 接收%u, 发送%u\n", ping_received, pong_sent);

    rte_eal_cleanup();
    printf("Receiver进程退出\n");

    return 0;
}
