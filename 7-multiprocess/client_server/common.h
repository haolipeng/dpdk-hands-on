/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK Multiprocess Client-Server Example - Common Definitions
 */

#ifndef _CLIENT_SERVER_COMMON_H_
#define _CLIENT_SERVER_COMMON_H_

#include <stdint.h>
#include <rte_ether.h>

/* 共享对象名称 */
#define PKTMBUF_POOL_NAME "cs_mbuf_pool"
#define CLIENT_RING_NAME "cs_client_ring_%u"  // %u 为client_id

/* 配置参数 */
#define MAX_CLIENTS 4                 // 最多支持的client数量
#define NUM_MBUFS 8191               // mbuf数量(2^n - 1)
#define MBUF_CACHE_SIZE 250          // mbuf缓存
#define RTE_MBUF_DEFAULT_BUF_SIZE 2048  // mbuf大小
#define RING_SIZE 2048               // Ring大小

/* 数据包批处理大小 */
#define BURST_SIZE 32

/* 简单的数据包结构 */
struct packet {
    struct rte_ether_addr src_mac;
    struct rte_ether_addr dst_mac;
    uint32_t seq_num;
    uint64_t timestamp;
    char payload[64];
} __attribute__((packed));

/* Client统计信息(存储在共享内存) */
struct client_stats {
    volatile uint64_t rx_pkts;       // 从server接收的包数
    volatile uint64_t tx_pkts;       // 转发的包数
    volatile uint64_t dropped;       // 丢弃的包数
} __rte_cache_aligned;

/* 全局共享信息 */
struct shared_info {
    uint32_t num_clients;            // 当前client数量
    struct client_stats stats[MAX_CLIENTS];
} __rte_cache_aligned;

#endif /* _CLIENT_SERVER_COMMON_H_ */
