/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Basic Example - Primary Process
 *
 * 功能说明:
 * Primary进程负责:
 * 1. 初始化EAL环境
 * 2. 创建共享内存池(mempool)
 * 3. 创建共享Ring队列
 * 4. 向Secondary进程发送消息
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_mempool.h>
#include <rte_ring.h>
#include <rte_cycles.h>

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
    uint32_t msg_count = 0;

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* ========== 步骤1: 初始化EAL ========== */
    printf("步骤1: Primary进程初始化EAL...\n");
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        fprintf(stderr, "错误: EAL初始化失败\n");
        return -1;
    }

    /* 验证这是Primary进程 */
    if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
        fprintf(stderr, "错误: 这个程序必须作为Primary进程运行!\n");
        fprintf(stderr, "提示: 不要使用 --proc-type=secondary 参数\n");
        rte_eal_cleanup();
        return -1;
    }

    printf("✓ EAL初始化成功 (进程类型: PRIMARY)\n\n");

    /* ========== 步骤2: 创建共享内存池 ========== */
    printf("步骤2: 创建共享内存池 (名称: %s)...\n", MEMPOOL_NAME);

    mp = rte_mempool_create(
        MEMPOOL_NAME,           // 内存池名称(全局唯一)
        NUM_MBUFS,              // 对象数量
        OBJ_SIZE,               // 对象大小
        MBUF_CACHE_SIZE,        // 缓存大小
        0,                      // 私有数据大小
        NULL, NULL,             // mempool构造函数
        NULL, NULL,             // 对象构造函数
        rte_socket_id(),        // NUMA节点
        0);                     // 标志位(0=多生产者多消费者)

    if (mp == NULL) {
        fprintf(stderr, "错误: 创建内存池失败 (%s)\n",
                rte_strerror(rte_errno));
        rte_eal_cleanup();
        return -1;
    }

    printf("✓ 内存池创建成功\n");
    printf("  - 对象数量: %u\n", NUM_MBUFS);
    printf("  - 对象大小: %u 字节\n", OBJ_SIZE);
    printf("  - 可用对象: %u\n", rte_mempool_avail_count(mp));
    printf("\n");

    /* ========== 步骤3: 创建共享Ring队列 ========== */
    printf("步骤3: 创建共享Ring队列 (名称: %s)...\n", RING_NAME);

    ring = rte_ring_create(
        RING_NAME,              // Ring名称(全局唯一)
        RING_SIZE,              // Ring大小(2的幂)
        rte_socket_id(),        // NUMA节点
        RING_F_SP_ENQ | RING_F_SC_DEQ); // 单生产者单消费者模式

    if (ring == NULL) {
        fprintf(stderr, "错误: 创建Ring失败 (%s)\n",
                rte_strerror(rte_errno));
        rte_mempool_free(mp);
        rte_eal_cleanup();
        return -1;
    }

    printf("✓ Ring队列创建成功\n");
    printf("  - Ring大小: %u\n", RING_SIZE);
    printf("  - 空闲空间: %u\n", rte_ring_free_count(ring));
    printf("\n");

    /* ========== 步骤4: 等待Secondary进程连接 ========== */
    printf("========================================\n");
    printf("Primary进程已准备就绪!\n");
    printf("现在可以启动Secondary进程了:\n");
    printf("  sudo ./bin/mp_basic_secondary -l 1 --proc-type=secondary\n");
    printf("========================================\n\n");

    printf("Primary进程将每秒发送一条消息到Ring...\n");
    printf("按 Ctrl+C 退出\n\n");

    /* ========== 步骤5: 主循环 - 发送消息 ========== */
    while (!force_quit) {
        struct message *msg;

        /* 从内存池获取一个对象 */
        if (rte_mempool_get(mp, (void **)&msg) < 0) {
            fprintf(stderr, "警告: 内存池已空\n");
            sleep(1);
            continue;
        }

        /* 填充消息内容 */
        msg->seq_num = msg_count++;
        msg->sender_id = 0;  // 0表示Primary进程
        snprintf(msg->data, sizeof(msg->data),
                 "Hello from Primary #%u", msg->seq_num);

        /* 将消息放入Ring队列 */
        if (rte_ring_enqueue(ring, msg) < 0) {
            fprintf(stderr, "警告: Ring队列已满,消息丢弃\n");
            rte_mempool_put(mp, msg);  // 归还对象
        } else {
            printf("[Primary] 发送消息 #%u: %s\n",
                   msg->seq_num, msg->data);
        }

        /* 等待1秒 */
        sleep(1);
    }

    /* ========== 步骤6: 清理资源 ========== */
    printf("\n正在清理资源...\n");

    /* 注意: 不要释放共享对象,让Secondary进程自己决定何时退出 */
    printf("提示: 共享对象(mempool和ring)不会被释放\n");
    printf("      Secondary进程仍然可以访问它们\n");

    rte_eal_cleanup();
    printf("Primary进程退出\n");

    return 0;
}
