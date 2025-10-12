/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Ring Communication Example - Common Definitions
 */

#ifndef _RING_COMM_COMMON_H_
#define _RING_COMM_COMMON_H_

#include <stdint.h>

/* 双向通信的Ring队列名称 */
#define RING_P2S_NAME "ring_primary_to_secondary"  // Primary -> Secondary
#define RING_S2P_NAME "ring_secondary_to_primary"  // Secondary -> Primary

/* 共享内存池 */
#define MEMPOOL_NAME "ring_comm_pool"

/* 配置参数 */
#define NUM_MBUFS 1024
#define MBUF_CACHE_SIZE 256
#define MSG_SIZE 128
#define RING_SIZE 512

/* 消息类型 */
enum msg_type {
    MSG_TYPE_REQUEST = 1,      // 请求消息
    MSG_TYPE_RESPONSE = 2,     // 响应消息
    MSG_TYPE_NOTIFY = 3,       // 通知消息
    MSG_TYPE_PING = 4,         // Ping消息
    MSG_TYPE_PONG = 5,         // Pong消息
};

/* 消息结构体 */
struct comm_message {
    uint32_t msg_id;           // 消息ID
    uint32_t msg_type;         // 消息类型
    uint32_t sender_pid;       // 发送者进程ID
    uint64_t timestamp;        // 时间戳
    char payload[96];          // 消息负载
} __attribute__((packed));

#endif /* _RING_COMM_COMMON_H_ */
