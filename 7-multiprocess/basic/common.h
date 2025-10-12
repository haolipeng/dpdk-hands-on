/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Basic Example - Common Definitions
 */

#ifndef _BASIC_COMMON_H_
#define _BASIC_COMMON_H_

#include <stdint.h>

/* 共享内存池的名称 - Primary创建,Secondary查找 */
#define MEMPOOL_NAME "mp_basic_pool"

/* 共享Ring队列的名称 - 用于进程间通信 */
#define RING_NAME "mp_basic_ring"

/* 内存池配置 */
#define NUM_MBUFS 512          // 内存池中的对象数量
#define MBUF_CACHE_SIZE 0      // 每个核心的缓存(0表示无缓存)
#define OBJ_SIZE 64            // 每个对象的大小(字节)

/* Ring队列配置 */
#define RING_SIZE 256          // Ring大小(必须是2的幂)

/* 消息结构体 - 在进程间传递的数据 */
struct message {
    uint32_t seq_num;          // 消息序号
    uint32_t sender_id;        // 发送者ID (0=Primary, >0=Secondary)
    char data[48];             // 消息数据
} __attribute__((packed));

#endif /* _BASIC_COMMON_H_ */
