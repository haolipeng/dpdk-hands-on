/*
 * DPDK rte_flow Flow Control Demonstration
 * Lesson 19: Hardware-Accelerated Flow Classification
 *
 * This example demonstrates:
 * 1. rte_flow basic usage
 * 2. Creating flow rules for traffic classification
 * 3. Pattern matching (IP, TCP/UDP ports)
 * 4. Actions (queue, drop, mark, count)
 * 5. Dynamic rule management
 * 6. Flow statistics and monitoring
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_flow.h>

/* 配置参数 */
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define MAX_FLOWS 128

/* 全局变量 */
static volatile int force_quit = 0;
static uint16_t nb_rxd = RX_RING_SIZE;
static uint16_t nb_txd = TX_RING_SIZE;

/* Flow 规则管理器 */
struct flow_entry {
    struct rte_flow *flow;
    char description[128];
    uint64_t hits;
    uint64_t bytes;
    int active;
};

static struct flow_entry flow_table[MAX_FLOWS];
static uint32_t num_flows = 0;

/* 统计信息 */
struct queue_stats {
    uint64_t rx_packets;
    uint64_t rx_bytes;
} __rte_cache_aligned;

static struct queue_stats queue_stats[16];

/*
 * 信号处理函数
 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = 1;
    }
}

/*
 * 添加 flow 到管理器
 */
static int add_flow_entry(struct rte_flow *flow, const char *desc)
{
    if (num_flows >= MAX_FLOWS) {
        printf("Flow table full\n");
        return -1;
    }

    flow_table[num_flows].flow = flow;
    snprintf(flow_table[num_flows].description,
             sizeof(flow_table[num_flows].description),
             "%s", desc);
    flow_table[num_flows].active = 1;
    flow_table[num_flows].hits = 0;
    flow_table[num_flows].bytes = 0;

    num_flows++;
    return 0;
}

/*
 * 创建 IPv4 规则: 匹配目的 IP,发送到指定队列
 */
static struct rte_flow *
create_ipv4_flow(uint16_t port_id, uint16_t queue_id,
                 uint32_t dest_ip, const char *desc)
{
    struct rte_flow_attr attr;
    struct rte_flow_item pattern[4];
    struct rte_flow_action action[4];
    struct rte_flow *flow = NULL;
    struct rte_flow_error error;
    struct rte_flow_action_queue queue_action = { .index = queue_id };
    struct rte_flow_action_count count_action = { .id = num_flows };
    struct rte_flow_item_ipv4 ipv4_spec;
    struct rte_flow_item_ipv4 ipv4_mask;

    memset(&attr, 0, sizeof(struct rte_flow_attr));
    memset(pattern, 0, sizeof(pattern));
    memset(action, 0, sizeof(action));
    memset(&ipv4_spec, 0, sizeof(struct rte_flow_item_ipv4));
    memset(&ipv4_mask, 0, sizeof(struct rte_flow_item_ipv4));

    /* 属性: ingress 方向 */
    attr.ingress = 1;
    attr.priority = 0;

    /* Pattern: ETH / IPv4(dest=X) / END */
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;

    ipv4_spec.hdr.dst_addr = rte_cpu_to_be_32(dest_ip);
    ipv4_mask.hdr.dst_addr = RTE_BE32(0xFFFFFFFF);

    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[1].spec = &ipv4_spec;
    pattern[1].mask = &ipv4_mask;

    pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

    /* Actions: COUNT / QUEUE / END */
    action[0].type = RTE_FLOW_ACTION_TYPE_COUNT;
    action[0].conf = &count_action;

    action[1].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    action[1].conf = &queue_action;

    action[2].type = RTE_FLOW_ACTION_TYPE_END;

    /* 验证规则 */
    int res = rte_flow_validate(port_id, &attr, pattern, action, &error);
    if (res != 0) {
        printf("Flow validation failed: %s\n", error.message);
        return NULL;
    }

    /* 创建规则 */
    flow = rte_flow_create(port_id, &attr, pattern, action, &error);
    if (flow == NULL) {
        printf("Flow creation failed: %s\n", error.message);
        return NULL;
    }

    printf("✓ Created flow: %s (Queue %u)\n", desc, queue_id);

    /* 添加到管理器 */
    add_flow_entry(flow, desc);

    return flow;
}

/*
 * 创建 TCP 端口规则
 */
static struct rte_flow *
create_tcp_port_flow(uint16_t port_id, uint16_t queue_id,
                     uint16_t tcp_port, const char *desc)
{
    struct rte_flow_attr attr;
    struct rte_flow_item pattern[5];
    struct rte_flow_action action[4];
    struct rte_flow *flow = NULL;
    struct rte_flow_error error;
    struct rte_flow_action_queue queue_action = { .index = queue_id };
    struct rte_flow_action_count count_action = { .id = num_flows };
    struct rte_flow_item_tcp tcp_spec;
    struct rte_flow_item_tcp tcp_mask;

    memset(&attr, 0, sizeof(struct rte_flow_attr));
    memset(pattern, 0, sizeof(pattern));
    memset(action, 0, sizeof(action));
    memset(&tcp_spec, 0, sizeof(struct rte_flow_item_tcp));
    memset(&tcp_mask, 0, sizeof(struct rte_flow_item_tcp));

    /* 属性 */
    attr.ingress = 1;
    attr.priority = 0;

    /* Pattern: ETH / IPv4 / TCP(dst_port=X) / END */
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;

    tcp_spec.hdr.dst_port = rte_cpu_to_be_16(tcp_port);
    tcp_mask.hdr.dst_port = RTE_BE16(0xFFFF);

    pattern[2].type = RTE_FLOW_ITEM_TYPE_TCP;
    pattern[2].spec = &tcp_spec;
    pattern[2].mask = &tcp_mask;

    pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

    /* Actions: COUNT / QUEUE / END */
    action[0].type = RTE_FLOW_ACTION_TYPE_COUNT;
    action[0].conf = &count_action;

    action[1].type = RTE_FLOW_ACTION_TYPE_QUEUE;
    action[1].conf = &queue_action;

    action[2].type = RTE_FLOW_ACTION_TYPE_END;

    /* 验证并创建 */
    int res = rte_flow_validate(port_id, &attr, pattern, action, &error);
    if (res != 0) {
        printf("Flow validation failed: %s\n", error.message);
        return NULL;
    }

    flow = rte_flow_create(port_id, &attr, pattern, action, &error);
    if (flow == NULL) {
        printf("Flow creation failed: %s\n", error.message);
        return NULL;
    }

    printf("✓ Created flow: %s (Queue %u)\n", desc, queue_id);

    add_flow_entry(flow, desc);

    return flow;
}

/*
 * 创建 DROP 规则 (用于阻止特定流量)
 */
static struct rte_flow *
create_drop_flow(uint16_t port_id, uint32_t src_ip, const char *desc)
{
    struct rte_flow_attr attr;
    struct rte_flow_item pattern[4];
    struct rte_flow_action action[3];
    struct rte_flow *flow = NULL;
    struct rte_flow_error error;
    struct rte_flow_action_count count_action = { .id = num_flows };
    struct rte_flow_item_ipv4 ipv4_spec;
    struct rte_flow_item_ipv4 ipv4_mask;

    memset(&attr, 0, sizeof(struct rte_flow_attr));
    memset(pattern, 0, sizeof(pattern));
    memset(action, 0, sizeof(action));
    memset(&ipv4_spec, 0, sizeof(struct rte_flow_item_ipv4));
    memset(&ipv4_mask, 0, sizeof(struct rte_flow_item_ipv4));

    /* 属性: 高优先级 */
    attr.ingress = 1;
    attr.priority = 0;  /* 最高优先级 */

    /* Pattern: ETH / IPv4(src=X) / END */
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;

    ipv4_spec.hdr.src_addr = rte_cpu_to_be_32(src_ip);
    ipv4_mask.hdr.src_addr = RTE_BE32(0xFFFFFFFF);

    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[1].spec = &ipv4_spec;
    pattern[1].mask = &ipv4_mask;

    pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

    /* Actions: COUNT / DROP / END */
    action[0].type = RTE_FLOW_ACTION_TYPE_COUNT;
    action[0].conf = &count_action;

    action[1].type = RTE_FLOW_ACTION_TYPE_DROP;

    action[2].type = RTE_FLOW_ACTION_TYPE_END;

    /* 验证并创建 */
    int res = rte_flow_validate(port_id, &attr, pattern, action, &error);
    if (res != 0) {
        printf("Flow validation failed: %s\n", error.message);
        return NULL;
    }

    flow = rte_flow_create(port_id, &attr, pattern, action, &error);
    if (flow == NULL) {
        printf("Flow creation failed: %s\n", error.message);
        return NULL;
    }

    printf("✓ Created drop flow: %s\n", desc);

    add_flow_entry(flow, desc);

    return flow;
}

/*
 * 查询 flow 统计
 */
static int query_flow_stats(uint16_t port_id, struct rte_flow *flow,
                            uint64_t *hits, uint64_t *bytes)
{
    struct rte_flow_error error;
    struct rte_flow_query_count count_query;
    struct rte_flow_action action;

    memset(&count_query, 0, sizeof(count_query));

    action.type = RTE_FLOW_ACTION_TYPE_COUNT;
    action.conf = NULL;

    int ret = rte_flow_query(port_id, flow, &action, &count_query, &error);
    if (ret != 0) {
        return -1;
    }

    if (count_query.hits_set) {
        *hits = count_query.hits;
    }
    if (count_query.bytes_set) {
        *bytes = count_query.bytes;
    }

    return 0;
}

/*
 * 打印所有 flow 统计
 */
static void print_flow_stats(uint16_t port_id)
{
    printf("\n=== Flow Rules Statistics ===\n");
    printf("┌────┬─────────────────────────────────────────────┬──────────────┬──────────────┐\n");
    printf("│ ID │ Description                                 │ Packets      │ Bytes        │\n");
    printf("├────┼─────────────────────────────────────────────┼──────────────┼──────────────┤\n");

    for (uint32_t i = 0; i < num_flows; i++) {
        if (!flow_table[i].active)
            continue;

        uint64_t hits = 0, bytes = 0;
        query_flow_stats(port_id, flow_table[i].flow, &hits, &bytes);

        flow_table[i].hits = hits;
        flow_table[i].bytes = bytes;

        printf("│ %2u │ %-43s │ %12"PRIu64" │ %12"PRIu64" │\n",
               i, flow_table[i].description, hits, bytes);
    }

    printf("└────┴─────────────────────────────────────────────┴──────────────┴──────────────┘\n");
}

/*
 * Worker 核心处理函数
 */
static int worker_main(void *arg)
{
    uint16_t port_id = *(uint16_t *)arg;
    uint16_t queue_id = rte_lcore_id() - 1;  /* 假设 lcore 1 → queue 0 */
    unsigned lcore_id = rte_lcore_id();

    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;

    printf("Worker core %u started on queue %u\n", lcore_id, queue_id);

    while (!force_quit) {
        nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0))
            continue;

        /* 更新统计 */
        queue_stats[queue_id].rx_packets += nb_rx;

        for (uint16_t i = 0; i < nb_rx; i++) {
            queue_stats[queue_id].rx_bytes += rte_pktmbuf_pkt_len(bufs[i]);

            /* 检查是否有标记 */
            if (bufs[i]->ol_flags & RTE_MBUF_F_RX_FDIR_ID) {
                uint32_t mark = bufs[i]->hash.fdir.hi;
                printf("Lcore %u: Received marked packet (mark=%u)\n",
                       lcore_id, mark);
            }

            rte_pktmbuf_free(bufs[i]);
        }
    }

    printf("Worker core %u stopped\n", lcore_id);
    return 0;
}

/*
 * 打印队列统计
 */
static void print_queue_stats(void)
{
    printf("\n=== Queue Statistics ===\n");
    printf("┌────────┬──────────────┬──────────────┐\n");
    printf("│ Queue  │ RX Packets   │ RX Bytes     │\n");
    printf("├────────┼──────────────┼──────────────┤\n");

    for (int q = 0; q < 8; q++) {
        if (queue_stats[q].rx_packets == 0)
            continue;

        printf("│ %6d │ %12"PRIu64" │ %12"PRIu64" │\n",
               q, queue_stats[q].rx_packets, queue_stats[q].rx_bytes);
    }

    printf("└────────┴──────────────┴──────────────┘\n");
}

/*
 * 初始化端口
 */
static int port_init(uint16_t port, struct rte_mempool *mbuf_pool,
                     uint16_t nb_queues)
{
    struct rte_eth_conf port_conf;
    int ret;

    memset(&port_conf, 0, sizeof(struct rte_eth_conf));

    printf("\n=== Initializing Port %u ===\n", port);

    ret = rte_eth_dev_configure(port, nb_queues, 1, &port_conf);
    if (ret != 0)
        return ret;

    ret = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (ret != 0)
        return ret;

    /* 设置 RX 队列 */
    for (uint16_t q = 0; q < nb_queues; q++) {
        ret = rte_eth_rx_queue_setup(port, q, nb_rxd,
                                     rte_eth_dev_socket_id(port),
                                     NULL, mbuf_pool);
        if (ret < 0)
            return ret;
    }

    /* 设置 TX 队列 */
    ret = rte_eth_tx_queue_setup(port, 0, nb_txd,
                                 rte_eth_dev_socket_id(port), NULL);
    if (ret < 0)
        return ret;

    /* 启动设备 */
    ret = rte_eth_dev_start(port);
    if (ret < 0)
        return ret;

    /* 开启混杂模式 */
    ret = rte_eth_promiscuous_enable(port);
    if (ret != 0)
        return ret;

    printf("Port %u initialized successfully\n", port);
    return 0;
}

/*
 * 清理所有 flow 规则
 */
static void cleanup_flows(uint16_t port_id)
{
    struct rte_flow_error error;

    printf("\nCleaning up flow rules...\n");

    for (uint32_t i = 0; i < num_flows; i++) {
        if (flow_table[i].active && flow_table[i].flow != NULL) {
            int ret = rte_flow_destroy(port_id, flow_table[i].flow, &error);
            if (ret != 0) {
                printf("Failed to destroy flow %u: %s\n", i, error.message);
            } else {
                printf("✓ Destroyed flow %u: %s\n", i, flow_table[i].description);
            }
            flow_table[i].active = 0;
        }
    }
}

/*
 * 主函数
 */
int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    uint16_t nb_ports;
    uint16_t port_id = 0;
    int ret;
    unsigned lcore_id;
    uint16_t nb_queues = 4;

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化 EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    argc -= ret;
    argv += ret;

    /* 打印欢迎信息 */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║   DPDK rte_flow Flow Control - Lesson 19              ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    /* 检查端口 */
    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");

    printf("\nUsing port: %u\n", port_id);

    /* 创建 mbuf pool */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
                                       MBUF_CACHE_SIZE, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE,
                                       rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    /* 初始化端口 */
    ret = port_init(port_id, mbuf_pool, nb_queues);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %u\n", port_id);

    /* 创建 Flow 规则 */
    printf("\n=== Creating Flow Rules ===\n");

    /* 示例1: HTTP 流量到队列 0 */
    create_tcp_port_flow(port_id, 0, 80, "HTTP traffic (port 80)");

    /* 示例2: HTTPS 流量到队列 1 */
    create_tcp_port_flow(port_id, 1, 443, "HTTPS traffic (port 443)");

    /* 示例3: SSH 流量到队列 2 */
    create_tcp_port_flow(port_id, 2, 22, "SSH traffic (port 22)");

    /* 示例4: 特定 IP 到队列 3 */
    create_ipv4_flow(port_id, 3, 0xC0A80164, "Dest IP 192.168.1.100");

    /* 示例5: 阻止特定源 IP (模拟 DDoS 防护) */
    create_drop_flow(port_id, 0x0A000001, "Block IP 10.0.0.1 (attacker)");

    printf("\nTotal flows created: %u\n", num_flows);

    /* 启动 worker 核心 */
    printf("\n=== Starting Workers ===\n");
    uint16_t queue = 0;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (queue >= nb_queues)
            break;
        rte_eal_remote_launch(worker_main, &port_id, lcore_id);
        queue++;
    }

    /* 主核心监控统计 */
    printf("\n=== Monitoring (Press Ctrl+C to quit) ===\n");

    while (!force_quit) {
        sleep(2);

        if (force_quit)
            break;

        /* 清屏并打印统计 */
        printf("\033[2J\033[H");
        printf("╔════════════════════════════════════════════════════════╗\n");
        printf("║   DPDK rte_flow Monitoring                             ║\n");
        printf("╚════════════════════════════════════════════════════════╝\n");

        print_flow_stats(port_id);
        print_queue_stats();

        printf("\nPress Ctrl+C to quit\n");
    }

    /* 等待所有 worker 完成 */
    printf("\nWaiting for workers to stop...\n");
    rte_eal_mp_wait_lcore();

    /* 最终统计 */
    printf("\n=== Final Statistics ===\n");
    print_flow_stats(port_id);
    print_queue_stats();

    /* 清理 flow 规则 */
    cleanup_flows(port_id);

    /* 停止端口 */
    printf("\nStopping port %u...\n", port_id);
    ret = rte_eth_dev_stop(port_id);
    if (ret != 0)
        printf("Port stop failed: %s\n", rte_strerror(-ret));

    rte_eth_dev_close(port_id);

    /* 清理 */
    rte_eal_cleanup();

    printf("\nProgram exited cleanly.\n");
    return 0;
}
