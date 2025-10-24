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
#include <rte_log.h>
#include <rte_string_fns.h>

#include "common.h"

#define RTE_LOGTYPE_SERVER RTE_LOGTYPE_USER1

/* 全局变量 */
static volatile int force_quit = 0;

/* 信号处理 */
static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        const struct rte_memzone *mz;
        struct shared_info *info;

        printf("\n\nServer: 收到信号 %d, 准备退出...\n", signum);
        force_quit = 1;

        mz = rte_memzone_lookup(MZ_SHARED_INFO);
        if (mz != NULL) {
            info = (struct shared_info *)mz->addr;
            info->force_quit = 1;
        }
    }
}

/* IPC消息处理: 处理客户端的Hello请求 (同步请求/响应) */
static int
handle_hello_request(const struct rte_mp_msg *msg, const void *peer)
{
    const struct rte_memzone *mz;
    struct shared_info *info;
    struct rte_mp_msg reply;
    char request_msg[MAX_MSG_LEN];
    char reply_msg[MAX_MSG_LEN];

    /* 解析请求消息 */
    if (msg->len_param > 0 && msg->len_param < MAX_MSG_LEN) {
        memcpy(request_msg, msg->param, msg->len_param);
        request_msg[msg->len_param] = '\0';
        printf("[IPC-SYNC] 收到同步请求: %s\n", request_msg);
    } else {
        printf("[IPC-SYNC] 收到空请求\n");
        strlcpy(request_msg, "(empty)", sizeof(request_msg));
    }

    /* 获取共享内存,更新请求计数 */
    mz = rte_memzone_lookup(MZ_SHARED_INFO);
    if (mz != NULL) {
        info = (struct shared_info *)mz->addr;
        info->request_count++;
    }

    /* 构造回复消息 */
    snprintf(reply_msg, sizeof(reply_msg),
             "Server SYNC reply: Got your message '%s'", request_msg);

    memset(&reply, 0, sizeof(reply));
    /* 回复消息的name必须与请求消息的name相同 */
    strlcpy(reply.name, msg->name, sizeof(reply.name));
    memcpy(reply.param, reply_msg, strlen(reply_msg) + 1);
    reply.len_param = strlen(reply_msg) + 1;

    printf("[IPC-SYNC] 发送同步回复: %s\n", reply_msg);

    /* 发送回复 */
    return rte_mp_reply(&reply, peer);
}

/* IPC消息处理: 处理客户端的异步Hello请求 */
static int
handle_hello_async_request(const struct rte_mp_msg *msg, const void *peer)
{
    const struct rte_memzone *mz;
    struct shared_info *info;
    struct rte_mp_msg reply;
    char request_msg[MAX_MSG_LEN];
    char reply_msg[MAX_MSG_LEN];

    /* 解析请求消息 */
    if (msg->len_param > 0 && msg->len_param < MAX_MSG_LEN) {
        memcpy(request_msg, msg->param, msg->len_param);
        request_msg[msg->len_param] = '\0';
        printf("[IPC-ASYNC] 收到异步请求: %s\n", request_msg);
    } else {
        printf("[IPC-ASYNC] 收到空请求\n");
        strlcpy(request_msg, "(empty)", sizeof(request_msg));
    }

    /* 获取共享内存,更新异步请求计数 */
    mz = rte_memzone_lookup(MZ_SHARED_INFO);
    if (mz != NULL) {
        info = (struct shared_info *)mz->addr;
        info->async_request_count++;
    }

    /* 构造回复消息 */
    snprintf(reply_msg, sizeof(reply_msg),
             "Server ASYNC reply: Got your async message '%s'", request_msg);

    memset(&reply, 0, sizeof(reply));
    /* 回复消息的name必须与请求消息的name相同 */
    strlcpy(reply.name, msg->name, sizeof(reply.name));
    memcpy(reply.param, reply_msg, strlen(reply_msg) + 1);
    reply.len_param = strlen(reply_msg) + 1;

    printf("[IPC-ASYNC] 发送异步回复: %s\n", reply_msg);

    /* 发送回复 */
    return rte_mp_reply(&reply, peer);
}

/* 初始化共享内存 */
static int
init_shared_memory(void)
{
    const struct rte_memzone *mz;
    struct shared_info *info;

    /* 创建共享内存区域 */
    mz = rte_memzone_reserve(MZ_SHARED_INFO, sizeof(struct shared_info),
                             rte_socket_id(), 0);
    if (mz == NULL) {
        printf("无法分配共享内存\n");
        return -1;
    }

    info = (struct shared_info *)mz->addr;
    memset(info, 0, sizeof(struct shared_info));

    printf("共享内存初始化完成\n");
    return 0;
}

int
main(int argc, char **argv)
{
    int ret;
    const struct rte_memzone *mz;
    struct shared_info *info;
    uint64_t last_time = 0;
    uint64_t cur_time;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "EAL初始化失败\n");

    argc -= ret;
    argv += ret;

    printf("\n========================================\n");
    printf("DPDK Multi-Process IPC Server\n");
    printf("========================================\n\n");

    /* 必须是主进程 */
    if (rte_eal_process_type() != RTE_PROC_PRIMARY)
        rte_exit(EXIT_FAILURE, "Server必须以Primary进程运行\n");

    /* 初始化共享内存 */
    if (init_shared_memory() < 0)
        rte_exit(EXIT_FAILURE, "共享内存初始化失败\n");

    /* 获取共享内存 */
    mz = rte_memzone_lookup(MZ_SHARED_INFO);
    if (mz == NULL)
        rte_exit(EXIT_FAILURE, "查找共享内存失败\n");
    info = (struct shared_info *)mz->addr;

    /* 注册IPC消息处理器 (同步请求/响应) */
    ret = rte_mp_action_register(MSG_HELLO_REQUEST, handle_hello_request);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "注册hello_request处理器失败\n");

    /* 注册IPC消息处理器 (异步请求/响应) */
    ret = rte_mp_action_register(MSG_HELLO_ASYNC_REQUEST, handle_hello_async_request);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "注册hello_async_request处理器失败\n");

    printf("IPC消息处理器注册完成 (同步+异步)\n");
    printf("Server初始化完成\n");

    /* 进入主循环 */
    printf("\n========================================\n");
    printf("Server进入主循环，等待Client发送消息...\n");
    printf("========================================\n\n");

    while (!force_quit) {
        /* 每3秒打印一次请求计数 */
        cur_time = rte_get_timer_cycles();
        if (cur_time - last_time >= rte_get_timer_hz() * 3) {
            printf("[Server] 已处理同步请求: %lu 个, 异步请求: %lu 个\n",
                   info->request_count, info->async_request_count);
            last_time = cur_time;
        }

        usleep(100000);  // 100ms
    }

    printf("\nServer退出\n");

    /* 注销IPC处理器 */
    rte_mp_action_unregister(MSG_HELLO_REQUEST);
    rte_mp_action_unregister(MSG_HELLO_ASYNC_REQUEST);

    rte_eal_cleanup();

    printf("Server正常退出\n");
    return 0;
}
