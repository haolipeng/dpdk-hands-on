/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Ring Communication - Sender (Primary)
 *
 * 功能说明:
 * 1. 创建双向Ring队列: Primary->Secondary 和 Secondary->Primary
 * 2. 向Secondary发送Ping消息
 * 3. 接收Secondary返回的Pong消息
 * 4. 演示双向通信机制
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_cycles.h>

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

/* 获取当前时间戳(微秒) */
static uint64_t
get_timestamp_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

int
main(int argc, char **argv)
{
    struct rte_mempool *mp = NULL;
    struct rte_ring *ring_p2s = NULL;  // Primary to Secondary
    struct rte_ring *ring_s2p = NULL;  // Secondary to Primary
    int ret;
    uint32_t ping_count = 0;
    uint32_t pong_received = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    printf("=== Ring通信示例 - Sender (Primary) ===\n\n");
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "错误: EAL初始化失败\n");
        return -1;
    }

    if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
        fprintf(stderr, "错误: 必须作为Primary进程运行\n");
        rte_eal_cleanup();
        return -1;
    }

    /* 创建共享内存池 */
    printf("创建共享内存池...\n");
    mp = rte_mempool_create(
        MEMPOOL_NAME,
        NUM_MBUFS,
        MSG_SIZE,
        MBUF_CACHE_SIZE,
        0,
        NULL, NULL,
        NULL, NULL,
        rte_socket_id(),
        0);

    if (mp == NULL) {
        fprintf(stderr, "错误: 创建内存池失败\n");
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ 内存池创建成功\n\n");

    /* 创建双向Ring队列 */
    printf("创建双向Ring队列...\n");

    /* Ring 1: Primary -> Secondary */
    ring_p2s = rte_ring_create(
        RING_P2S_NAME,
        RING_SIZE,
        rte_socket_id(),
        RING_F_SP_ENQ | RING_F_SC_DEQ);

    if (ring_p2s == NULL) {
        fprintf(stderr, "错误: 创建P2S Ring失败\n");
        rte_mempool_free(mp);
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ Ring (Primary->Secondary) 创建成功\n");

    /* Ring 2: Secondary -> Primary */
    ring_s2p = rte_ring_create(
        RING_S2P_NAME,
        RING_SIZE,
        rte_socket_id(),
        RING_F_SP_ENQ | RING_F_SC_DEQ);

    if (ring_s2p == NULL) {
        fprintf(stderr, "错误: 创建S2P Ring失败\n");
        rte_mempool_free(mp);
        rte_eal_cleanup();
        return -1;
    }
    printf("✓ Ring (Secondary->Primary) 创建成功\n\n");

    /* 提示信息 */
    printf("========================================\n");
    printf("Sender进程已准备就绪!\n");
    printf("现在可以启动Receiver进程:\n");
    printf("  sudo ./bin/mp_ring_receiver -l 1 --proc-type=secondary\n");
    printf("========================================\n\n");

    printf("开始Ping-Pong通信测试...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* 主循环: 发送Ping,接收Pong */
    while (!force_quit) {
        struct comm_message *msg;

        /* 1. 发送Ping消息 */
        if (rte_mempool_get(mp, (void **)&msg) == 0) {
            msg->msg_id = ping_count++;
            msg->msg_type = MSG_TYPE_PING;
            msg->sender_pid = getpid();
            msg->timestamp = get_timestamp_us();
            snprintf(msg->payload, sizeof(msg->payload),
                     "Ping #%u from Primary", msg->msg_id);

            if (rte_ring_enqueue(ring_p2s, msg) == 0) {
                printf("[Sender] 发送 Ping #%u\n", msg->msg_id);
            } else {
                fprintf(stderr, "警告: Ring已满,Ping消息丢弃\n");
                rte_mempool_put(mp, msg);
            }
        }

        /* 2. 接收Pong消息 */
        if (rte_ring_dequeue(ring_s2p, (void **)&msg) == 0) {
            uint64_t now = get_timestamp_us();
            uint64_t rtt = now - msg->timestamp;  // 往返时延(微秒)

            pong_received++;
            printf("[Sender] 接收 Pong #%u (RTT: %lu us)\n",
                   msg->msg_id, rtt);
            printf("         内容: %s\n\n", msg->payload);

            rte_mempool_put(mp, msg);
        }

        /* 每5秒打印统计信息 */
        if (ping_count > 0 && ping_count % 5 == 0) {
            printf("--- 统计 (Sender) ---\n");
            printf("发送Ping: %u\n", ping_count);
            printf("收到Pong: %u\n", pong_received);
            printf("丢失率: %.2f%%\n",
                   100.0 * (ping_count - pong_received) / ping_count);
            printf("内存池可用: %u\n", rte_mempool_avail_count(mp));
            printf("--------------------\n\n");
        }

        /* 每秒发送一次 */
        sleep(1);
    }

    /* 清理 */
    printf("\n清理资源...\n");
    printf("最终统计: 发送%u, 接收%u\n", ping_count, pong_received);

    rte_eal_cleanup();
    printf("Sender进程退出\n");

    return 0;
}
