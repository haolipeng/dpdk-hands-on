/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_eal.h>
#include <rte_memzone.h>
#include <rte_cycles.h>
#include <rte_string_fns.h>

#include "common.h"

/* 全局变量 */
static volatile int force_quit = 0;

/* 异步请求回复的回调函数 */
static int
async_reply_callback(const struct rte_mp_msg *request,
                     const struct rte_mp_reply *reply)
{
    char *reply_msg;
    unsigned int i;

    printf("[IPC-ASYNC] 异步回调被调用:\n");

    if (reply->nb_sent != reply->nb_received) {
        printf("[IPC-ASYNC] 发送 %d 个请求, 但只收到 %d 个回复\n",
               reply->nb_sent, reply->nb_received);
    }

    /* 处理所有收到的回复 */
    for (i = 0; i < reply->nb_received; i++) {
        reply_msg = (char *)reply->msgs[i].param;
        printf("[IPC-ASYNC] 回复[%u]: %s\n", i, reply_msg);
    }

    return 0;
}

/* 信号处理 */
static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nClient: 收到信号 %d, 准备退出...\n", signum);
        force_quit = 1;
    }
}

int
main(int argc, char **argv)
{
    int ret;
    const struct rte_memzone *mz;
    struct shared_info *info;
    struct rte_mp_msg req;
    struct rte_mp_reply reply;
    struct timespec ts = {.tv_sec = 5, .tv_nsec = 0};
    char request_msg[MAX_MSG_LEN];
    char *reply_msg;
    uint64_t prev_sync_time = 0;
    uint64_t prev_async_time = 0;
    uint64_t cur_time;
    uint64_t sync_interval;
    uint64_t async_interval;
    static uint32_t sync_msg_count = 0;
    static uint32_t async_msg_count = 0;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "EAL初始化失败\n");

    argc -= ret;
    argv += ret;

    printf("\n========================================\n");
    printf("DPDK Multi-Process IPC Client\n");
    printf("========================================\n\n");

    /* 必须是副进程 */
    if (rte_eal_process_type() != RTE_PROC_SECONDARY)
        rte_exit(EXIT_FAILURE, "Client必须以Secondary进程运行\n");

    /* 查找共享内存 */
    mz = rte_memzone_lookup(MZ_SHARED_INFO);
    if (mz == NULL)
        rte_exit(EXIT_FAILURE, "找不到共享内存 - Server未运行?\n");
    info = (struct shared_info *)mz->addr;

    printf("共享内存查找成功\n");
    printf("Client初始化完成\n");

    sleep(1);  // 等待初始化完成

    /* 进入主循环 */
    sync_interval = rte_get_timer_hz() * 3;   // 每3秒发送一次同步消息
    async_interval = rte_get_timer_hz() * 5;  // 每5秒发送一次异步消息

    printf("\n========================================\n");
    printf("Client进入主循环 (按 Ctrl+C 退出)\n");
    printf("每3秒发送同步请求, 每5秒发送异步请求...\n");
    printf("========================================\n\n");

    while (!force_quit && !info->force_quit) {
        cur_time = rte_get_timer_cycles();

        /* 定期发送同步Hello消息 (同步请求/响应) */
        if (cur_time - prev_sync_time >= sync_interval) {
            sync_msg_count++;

            /* 构造同步请求消息 */
            snprintf(request_msg, sizeof(request_msg),
                     "SYNC Hello %u from Client", sync_msg_count);

            memset(&req, 0, sizeof(req));
            strlcpy(req.name, MSG_HELLO_REQUEST, sizeof(req.name));
            memcpy(req.param, request_msg, strlen(request_msg) + 1);
            req.len_param = strlen(request_msg) + 1;

            /* 发送同步请求 */
            printf("[IPC-SYNC] 发送同步请求: %s\n", request_msg);
            ret = rte_mp_request_sync(&req, &reply, &ts);
            if (ret < 0) {
                printf("[IPC-SYNC] 同步请求失败\n");
            } else if (reply.nb_received > 0) {
                /* 解析响应 */
                reply_msg = (char *)reply.msgs[0].param;

                printf("[IPC-SYNC] 收到同步回复: %s\n\n", reply_msg);

                /* 释放响应消息 */
                free(reply.msgs);
            } else {
                printf("[IPC-SYNC] 未收到同步响应\n");
            }

            prev_sync_time = cur_time;
        }

        /* 定期发送异步Hello消息 (异步请求/响应) */
        if (cur_time - prev_async_time >= async_interval) {
            async_msg_count++;

            /* 构造异步请求消息 */
            snprintf(request_msg, sizeof(request_msg),
                     "ASYNC Hello %u from Client", async_msg_count);

            memset(&req, 0, sizeof(req));
            strlcpy(req.name, MSG_HELLO_ASYNC_REQUEST, sizeof(req.name));
            memcpy(req.param, request_msg, strlen(request_msg) + 1);
            req.len_param = strlen(request_msg) + 1;

            /* 发送异步请求 */
            printf("[IPC-ASYNC] 发送异步请求: %s\n", request_msg);
            ret = rte_mp_request_async(&req, &ts, async_reply_callback);
            if (ret < 0) {
                printf("[IPC-ASYNC] 异步请求发送失败\n");
            } else {
                printf("[IPC-ASYNC] 异步请求已发送，等待回调...\n\n");
            }

            prev_async_time = cur_time;
        }

        usleep(100000);  // 100ms
    }

    printf("\nClient退出\n");

    rte_eal_cleanup();

    printf("Client正常退出\n");
    return 0;
}
