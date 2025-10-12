/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Basic Example - Secondary Process
 *
 * 功能说明:
 * Secondary进程负责:
 * 1. 初始化EAL环境(以Secondary模式)
 * 2. 查找Primary创建的共享内存池
 * 3. 查找Primary创建的Ring队列
 * 4. 从Ring接收消息并处理
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

/* 退出标志 */
static volatile int force_quit = 0;

/* 信号处理函数 */
static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n信号 %d 收到,准备退出...\n", signum);
        force_quit = 1;
    }
}

int
main(int argc, char **argv)
{
    struct rte_mempool *mp = NULL;
    struct rte_ring *ring = NULL;
    int ret;
    uint32_t received_count = 0;

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* ========== 步骤1: 初始化EAL (Secondary模式) ========== */
    printf("步骤1: Secondary进程初始化EAL...\n");
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "错误: EAL初始化失败\n");
        return -1;
    }

    /* 验证这是Secondary进程 */
    if (rte_eal_process_type() != RTE_PROC_SECONDARY) {
        fprintf(stderr, "错误: 这个程序必须作为Secondary进程运行!\n");
        fprintf(stderr, "提示: 请使用 --proc-type=secondary 参数\n");
        rte_eal_cleanup();
        return -1;
    }

    printf("✓ EAL初始化成功 (进程类型: SECONDARY)\n\n");

    /* ========== 步骤2: 查找共享内存池 ========== */
    printf("步骤2: 查找Primary创建的内存池 (名称: %s)...\n", MEMPOOL_NAME);

    mp = rte_mempool_lookup(MEMPOOL_NAME);
    if (mp == NULL) {
        fprintf(stderr, "错误: 找不到内存池 '%s'\n", MEMPOOL_NAME);
        fprintf(stderr, "提示: 请确保Primary进程已启动\n");
        rte_eal_cleanup();
        return -1;
    }

    printf("✓ 内存池查找成功\n");
    printf("  - 对象数量: %u\n", mp->size);
    printf("  - 对象大小: %u 字节\n", mp->elt_size);
    printf("  - 当前可用: %u\n", rte_mempool_avail_count(mp));
    printf("\n");

    /* ========== 步骤3: 查找共享Ring队列 ========== */
    printf("步骤3: 查找Primary创建的Ring队列 (名称: %s)...\n", RING_NAME);

    ring = rte_ring_lookup(RING_NAME);
    if (ring == NULL) {
        fprintf(stderr, "错误: 找不到Ring '%s'\n", RING_NAME);
        fprintf(stderr, "提示: 请确保Primary进程已创建Ring\n");
        rte_eal_cleanup();
        return -1;
    }

    printf("✓ Ring队列查找成功\n");
    printf("  - Ring大小: %u\n", rte_ring_get_capacity(ring));
    printf("  - 当前使用: %u\n", rte_ring_count(ring));
    printf("\n");

    /* ========== 步骤4: 准备接收消息 ========== */
    printf("========================================\n");
    printf("Secondary进程已准备就绪!\n");
    printf("等待接收Primary发送的消息...\n");
    printf("按 Ctrl+C 退出\n");
    printf("========================================\n\n");

    /* ========== 步骤5: 主循环 - 接收并处理消息 ========== */
    while (!force_quit) {
        struct message *msg;

        /* 从Ring队列中取出一个消息 */
        ret = rte_ring_dequeue(ring, (void **)&msg);
        if (ret == 0) {
            /* 成功接收到消息 */
            received_count++;

            printf("[Secondary] 接收消息 #%u (总计: %u)\n",
                   msg->seq_num, received_count);
            printf("            发送者ID: %u\n", msg->sender_id);
            printf("            数据: %s\n", msg->data);
            printf("\n");

            /* 处理完成后,将对象归还给内存池 */
            rte_mempool_put(mp, msg);
        } else {
            /* Ring为空,等待100ms */
            usleep(100000);
        }

        /* 每隔10条消息打印统计信息 */
        if (received_count > 0 && received_count % 10 == 0) {
            printf("--- 统计信息 ---\n");
            printf("已接收消息数: %u\n", received_count);
            printf("内存池可用对象: %u\n", rte_mempool_avail_count(mp));
            printf("Ring队列使用: %u/%u\n",
                   rte_ring_count(ring),
                   rte_ring_get_capacity(ring));
            printf("----------------\n\n");
        }
    }

    /* ========== 步骤6: 清理资源 ========== */
    printf("\n正在清理资源...\n");
    printf("总共接收了 %u 条消息\n", received_count);

    /* Secondary进程不需要释放共享对象 */
    /* 这些对象由Primary进程管理 */

    rte_eal_cleanup();
    printf("Secondary进程退出\n");

    return 0;
}
