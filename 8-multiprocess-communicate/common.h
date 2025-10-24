/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2024
 */

#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <rte_ring.h>
#include <rte_mempool.h>

/* 配置参数 */
#define RING_SIZE           1024
#define MBUF_CACHE_SIZE     250
#define NUM_MBUFS           8191
#define BURST_SIZE          32
#define MAX_CLIENTS         4

/* 共享对象名称 */
#define PKTMBUF_POOL_NAME   "mbuf_pool"
#define CLIENT_RING_PREFIX  "client_ring_"
#define MZ_SHARED_INFO      "shared_info"

/* IPC消息名称定义 */
#define MSG_HELLO_REQUEST       "hello_request"      // 同步请求
#define MSG_HELLO_ASYNC_REQUEST "hello_async_req"    // 异步请求

/* 注意: 回复消息的name必须与请求消息的name相同，因此不需要单独定义REPLY名称 */

/* 消息数据最大长度 */
#define MAX_MSG_LEN         128

/* 共享信息结构 */
struct shared_info {
    volatile uint32_t force_quit;       // 退出标志
    uint64_t request_count;             // 同步请求计数
    uint64_t async_request_count;       // 异步请求计数
} __rte_cache_aligned;

#endif /* _COMMON_H_ */
