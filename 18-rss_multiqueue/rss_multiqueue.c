/*
 * DPDK RSS and Multi-Queue Processing
 * Lesson 18: High-Performance Packet Processing with RSS
 *
 * This example demonstrates:
 * 1. RSS (Receive Side Scaling) configuration
 * 2. Multi-queue setup and management
 * 3. Worker core pattern
 * 4. Burst processing with prefetch optimization
 * 5. Per-core statistics and monitoring
 * 6. Load balancing analysis
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

/* 配置参数 */
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define PREFETCH_OFFSET 3

/* 最大队列数 */
#define MAX_RX_QUEUE 16
#define MAX_TX_QUEUE 16

/* 统计更新间隔 */
#define STATS_INTERVAL_MS 1000

/* 全局变量 */
static volatile int force_quit = 0;
static uint16_t nb_rxd = RX_RING_SIZE;
static uint16_t nb_txd = TX_RING_SIZE;

/* 每个 worker 的统计信息 */
struct worker_stats {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t tcp_packets;
    uint64_t udp_packets;
    uint64_t other_packets;
    uint64_t errors;
    uint64_t last_rx_packets;  /* 用于计算速率 */
    uint64_t last_timestamp;
} __rte_cache_aligned;

static struct worker_stats worker_stats[RTE_MAX_LCORE];

/* Worker 参数 */
struct worker_params {
    uint16_t port_id;
    uint16_t queue_id;
    unsigned lcore_id;
} __rte_cache_aligned;

/* 端口配置 */
static struct rte_eth_conf port_conf = {
    .rxmode = {
        .mq_mode = RTE_ETH_MQ_RX_RSS,  /* 启用 RSS */
        .mtu = RTE_ETHER_MAX_LEN,
    },
    .rx_adv_conf = {
        .rss_conf = {
            .rss_key = NULL,  /* NULL = 使用默认 RSS key */
            .rss_key_len = 40,
            .rss_hf = RTE_ETH_RSS_IP |      /* 基于 IP 哈希 */
                      RTE_ETH_RSS_TCP |      /* 基于 TCP 哈希 */
                      RTE_ETH_RSS_UDP |      /* 基于 UDP 哈希 */
                      RTE_ETH_RSS_SCTP,      /* 基于 SCTP 哈希 */
        },
    },
    .txmode = {
        .mq_mode = RTE_ETH_MQ_TX_NONE,
    },
};

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
 * 解析包并更新统计
 */
static inline void parse_packet(struct rte_mbuf *m, struct worker_stats *stats)
{
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ipv4_hdr;
    uint16_t ether_type;

    eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

    stats->rx_bytes += rte_pktmbuf_pkt_len(m);

    if (ether_type == RTE_ETHER_TYPE_IPV4) {
        ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
                                           sizeof(struct rte_ether_hdr));

        switch (ipv4_hdr->next_proto_id) {
        case IPPROTO_TCP:
            stats->tcp_packets++;
            break;
        case IPPROTO_UDP:
            stats->udp_packets++;
            break;
        default:
            stats->other_packets++;
            break;
        }
    } else {
        stats->other_packets++;
    }
}

/*
 * Worker 核心主函数
 * 每个 worker 处理一个 RX 队列
 */
static int worker_main(void *arg)
{
    struct worker_params *params = (struct worker_params *)arg;
    uint16_t port_id = params->port_id;
    uint16_t queue_id = params->queue_id;
    unsigned lcore_id = rte_lcore_id();

    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;
    struct worker_stats *stats = &worker_stats[lcore_id];

    printf("Worker core %u started: Port %u Queue %u (Socket %u)\n",
           lcore_id, port_id, queue_id, rte_lcore_to_socket_id(lcore_id));

    /* 初始化时间戳 */
    stats->last_timestamp = rte_get_timer_cycles();

    while (!force_quit) {
        /* Burst 收包 */
        nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0)) {
            continue;
        }

        /* 更新统计 */
        stats->rx_packets += nb_rx;

        /* 处理每个包 */
        for (uint16_t i = 0; i < nb_rx; i++) {
            /* Prefetch 优化: 提前加载后面的包到缓存 */
            if (i + PREFETCH_OFFSET < nb_rx) {
                rte_prefetch0(rte_pktmbuf_mtod(bufs[i + PREFETCH_OFFSET],
                                               void *));
            }

            /* 解析包 */
            parse_packet(bufs[i], stats);

            /* 释放包 */
            rte_pktmbuf_free(bufs[i]);
        }
    }

    printf("Worker core %u stopped\n", lcore_id);
    return 0;
}

/*
 * 打印 RSS 配置信息
 */
static void print_rss_config(uint16_t port_id)
{
    struct rte_eth_rss_conf rss_conf;
    uint8_t rss_key[40];
    int ret;

    printf("\n=== RSS Configuration ===\n");

    rss_conf.rss_key = rss_key;
    rss_conf.rss_key_len = sizeof(rss_key);

    ret = rte_eth_dev_rss_hash_conf_get(port_id, &rss_conf);
    if (ret != 0) {
        printf("Failed to get RSS configuration\n");
        return;
    }

    printf("RSS Hash Functions: 0x%016lx\n", rss_conf.rss_hf);

    /* 解析哈希类型 */
    printf("Enabled hash types:\n");
    if (rss_conf.rss_hf & RTE_ETH_RSS_IPV4)
        printf("  - IPv4\n");
    if (rss_conf.rss_hf & RTE_ETH_RSS_TCP)
        printf("  - TCP\n");
    if (rss_conf.rss_hf & RTE_ETH_RSS_UDP)
        printf("  - UDP\n");
    if (rss_conf.rss_hf & RTE_ETH_RSS_SCTP)
        printf("  - SCTP\n");

    printf("RSS Key Length: %u bytes\n", rss_conf.rss_key_len);
    printf("RSS Key: ");
    for (uint32_t i = 0; i < rss_conf.rss_key_len; i++) {
        printf("%02x", rss_key[i]);
        if ((i + 1) % 4 == 0)
            printf(" ");
    }
    printf("\n");
}

/*
 * 打印端口统计信息
 */
static void print_port_stats(uint16_t port_id, uint16_t nb_queues)
{
    struct rte_eth_stats eth_stats;
    int ret;

    ret = rte_eth_stats_get(port_id, &eth_stats);
    if (ret != 0) {
        printf("Failed to get port statistics\n");
        return;
    }

    printf("\n=== Port %u Statistics ===\n", port_id);
    printf("RX Packets: %"PRIu64"  RX Bytes: %"PRIu64"\n",
           eth_stats.ipackets, eth_stats.ibytes);
    printf("TX Packets: %"PRIu64"  TX Bytes: %"PRIu64"\n",
           eth_stats.opackets, eth_stats.obytes);
    printf("RX Errors:  %"PRIu64"  TX Errors:  %"PRIu64"\n",
           eth_stats.ierrors, eth_stats.oerrors);
    printf("RX Missed:  %"PRIu64"  RX No Mbuf: %"PRIu64"\n",
           eth_stats.imissed, eth_stats.rx_nombuf);

    /* 每个队列的统计 */
    printf("\nPer-Queue Statistics:\n");
    printf("┌────────┬──────────────┬──────────────┬──────────────┐\n");
    printf("│ Queue  │ RX Packets   │ RX Bytes     │ RX Errors    │\n");
    printf("├────────┼──────────────┼──────────────┼──────────────┤\n");
    for (uint16_t q = 0; q < nb_queues; q++) {
        printf("│ %6u │ %12"PRIu64" │ %12"PRIu64" │ %12"PRIu64" │\n",
               q, eth_stats.q_ipackets[q],
               eth_stats.q_ibytes[q],
               eth_stats.q_errors[q]);
    }
    printf("└────────┴──────────────┴──────────────┴──────────────┘\n");
}

/*
 * 打印 Worker 统计信息
 */
static void print_worker_stats(void)
{
    unsigned lcore_id;
    uint64_t total_packets = 0;
    uint64_t total_bytes = 0;
    uint64_t hz = rte_get_timer_hz();

    printf("\n=== Worker Core Statistics ===\n");
    printf("┌───────┬──────┬──────────────┬──────────────┬──────────┬──────────┬──────────┬──────────┐\n");
    printf("│ Lcore │ Queue│ RX Packets   │ RX Bytes     │ TCP      │ UDP      │ Other    │ Rate(pps)│\n");
    printf("├───────┼──────┼──────────────┼──────────────┼──────────┼──────────┼──────────┼──────────┤\n");

    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        struct worker_stats *stats = &worker_stats[lcore_id];

        if (stats->rx_packets == 0)
            continue;

        /* 计算速率 */
        uint64_t current_time = rte_get_timer_cycles();
        uint64_t time_diff = current_time - stats->last_timestamp;
        double time_sec = (double)time_diff / hz;
        uint64_t pkt_diff = stats->rx_packets - stats->last_rx_packets;
        uint64_t pps = (time_sec > 0) ? (uint64_t)(pkt_diff / time_sec) : 0;

        printf("│ %5u │ %4u │ %12"PRIu64" │ %12"PRIu64" │ %8"PRIu64" │ %8"PRIu64" │ %8"PRIu64" │ %8"PRIu64" │\n",
               lcore_id,
               lcore_id - 1,  /* 假设 queue_id = lcore_id - 1 */
               stats->rx_packets,
               stats->rx_bytes,
               stats->tcp_packets,
               stats->udp_packets,
               stats->other_packets,
               pps);

        /* 更新用于下次计算速率 */
        stats->last_rx_packets = stats->rx_packets;
        stats->last_timestamp = current_time;

        total_packets += stats->rx_packets;
        total_bytes += stats->rx_bytes;
    }

    printf("├───────┴──────┼──────────────┼──────────────┴──────────┴──────────┴──────────┴──────────┤\n");
    printf("│ Total        │ %12"PRIu64" │ %12"PRIu64"                                            │\n",
           total_packets, total_bytes);
    printf("└──────────────┴──────────────┴─────────────────────────────────────────────────────────┘\n");
}

/*
 * 负载均衡分析
 */
static void print_load_balance_analysis(void)
{
    unsigned lcore_id;
    uint64_t total_packets = 0;
    uint64_t max_packets = 0;
    uint64_t min_packets = UINT64_MAX;
    int num_workers = 0;

    /* 统计 */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        struct worker_stats *stats = &worker_stats[lcore_id];
        if (stats->rx_packets == 0)
            continue;

        total_packets += stats->rx_packets;
        if (stats->rx_packets > max_packets)
            max_packets = stats->rx_packets;
        if (stats->rx_packets < min_packets)
            min_packets = stats->rx_packets;
        num_workers++;
    }

    if (num_workers == 0 || total_packets == 0)
        return;

    uint64_t avg_packets = total_packets / num_workers;
    double imbalance = (max_packets - min_packets) * 100.0 / avg_packets;

    printf("\n=== Load Balance Analysis ===\n");
    printf("Number of Workers: %d\n", num_workers);
    printf("Total Packets:     %"PRIu64"\n", total_packets);
    printf("Average per Core:  %"PRIu64"\n", avg_packets);
    printf("Max per Core:      %"PRIu64" (%.1f%%)\n",
           max_packets, max_packets * 100.0 / total_packets);
    printf("Min per Core:      %"PRIu64" (%.1f%%)\n",
           min_packets, min_packets * 100.0 / total_packets);
    printf("Imbalance:         %.1f%%\n", imbalance);

    if (imbalance < 10.0) {
        printf("✓ Load is well balanced\n");
    } else if (imbalance < 30.0) {
        printf("⚠ Load is moderately imbalanced\n");
    } else {
        printf("✗ Load is heavily imbalanced\n");
    }
}

/*
 * 统计线程 - 定期打印统计信息
 */
static int stats_thread(void *arg)
{
    uint16_t port_id = *(uint16_t *)arg;
    uint16_t nb_queues = rte_lcore_count() - 1;  /* 减去主核心 */

    printf("Statistics thread started on lcore %u\n", rte_lcore_id());

    while (!force_quit) {
        sleep(STATS_INTERVAL_MS / 1000);

        if (force_quit)
            break;

        /* 清屏 */
        printf("\033[2J\033[H");

        /* 打印各种统计 */
        print_port_stats(port_id, nb_queues);
        print_worker_stats();
        print_load_balance_analysis();

        printf("\nPress Ctrl+C to quit\n");
    }

    return 0;
}

/*
 * 初始化端口
 */
static int port_init(uint16_t port, struct rte_mempool *mbuf_pool,
                     uint16_t nb_rx_queues, uint16_t nb_tx_queues)
{
    struct rte_eth_conf local_port_conf = port_conf;
    struct rte_eth_dev_info dev_info;
    int ret;
    uint16_t q;

    printf("\n=== Initializing Port %u ===\n", port);

    /* 获取设备信息 */
    ret = rte_eth_dev_info_get(port, &dev_info);
    if (ret != 0) {
        printf("Error getting device info: %s\n", rte_strerror(-ret));
        return ret;
    }

    printf("Device: %s\n", dev_info.driver_name);
    printf("Max RX queues: %u\n", dev_info.max_rx_queues);
    printf("Max TX queues: %u\n", dev_info.max_tx_queues);

    /* 检查队列数量 */
    if (nb_rx_queues > dev_info.max_rx_queues) {
        printf("Requested RX queues (%u) exceeds maximum (%u)\n",
               nb_rx_queues, dev_info.max_rx_queues);
        nb_rx_queues = dev_info.max_rx_queues;
    }

    if (nb_tx_queues > dev_info.max_tx_queues) {
        printf("Requested TX queues (%u) exceeds maximum (%u)\n",
               nb_tx_queues, dev_info.max_tx_queues);
        nb_tx_queues = dev_info.max_tx_queues;
    }

    printf("Configuring with %u RX queues and %u TX queues\n",
           nb_rx_queues, nb_tx_queues);

    /* 配置端口 */
    ret = rte_eth_dev_configure(port, nb_rx_queues, nb_tx_queues,
                                &local_port_conf);
    if (ret != 0) {
        printf("Port configuration failed: %s\n", rte_strerror(-ret));
        return ret;
    }

    /* 调整描述符数量 */
    ret = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (ret != 0) {
        printf("Failed to adjust descriptors: %s\n", rte_strerror(-ret));
        return ret;
    }

    printf("RX descriptors: %u, TX descriptors: %u\n", nb_rxd, nb_txd);

    /* 设置 RX 队列 */
    for (q = 0; q < nb_rx_queues; q++) {
        ret = rte_eth_rx_queue_setup(port, q, nb_rxd,
                                     rte_eth_dev_socket_id(port),
                                     NULL, mbuf_pool);
        if (ret < 0) {
            printf("RX queue %u setup failed: %s\n", q, rte_strerror(-ret));
            return ret;
        }
    }

    /* 设置 TX 队列 */
    for (q = 0; q < nb_tx_queues; q++) {
        ret = rte_eth_tx_queue_setup(port, q, nb_txd,
                                     rte_eth_dev_socket_id(port),
                                     NULL);
        if (ret < 0) {
            printf("TX queue %u setup failed: %s\n", q, rte_strerror(-ret));
            return ret;
        }
    }

    /* 启动设备 */
    ret = rte_eth_dev_start(port);
    if (ret < 0) {
        printf("Port start failed: %s\n", rte_strerror(-ret));
        return ret;
    }

    /* 开启混杂模式 */
    ret = rte_eth_promiscuous_enable(port);
    if (ret != 0) {
        printf("Promiscuous mode enable failed: %s\n", rte_strerror(-ret));
        return ret;
    }

    /* 打印 RSS 配置 */
    print_rss_config(port);

    printf("Port %u initialized successfully\n", port);

    return 0;
}

/*
 * 打印使用说明
 */
static void print_usage(const char *prgname)
{
    printf("\nUsage: %s [EAL options]\n\n", prgname);
    printf("Example:\n");
    printf("  sudo %s -l 0-4 -- \n", prgname);
    printf("    (Use 1 main core + 4 worker cores for 4 RX queues)\n\n");
}

/*
 * 主函数
 */
int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    uint16_t nb_ports;
    uint16_t port_id = 0;
    unsigned lcore_id;
    int ret;
    struct worker_params params[RTE_MAX_LCORE];
    uint16_t queue_id = 0;
    uint16_t nb_workers;

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
    printf("║   DPDK RSS and Multi-Queue Processing - Lesson 18     ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    /* 检查端口 */
    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");

    printf("\nAvailable ports: %u\n", nb_ports);
    printf("Using port: %u\n", port_id);

    /* 计算 worker 核心数量 */
    nb_workers = rte_lcore_count() - 1;  /* 减去主核心 */
    if (nb_workers == 0)
        rte_exit(EXIT_FAILURE, "Need at least 2 lcores (1 main + 1 worker)\n");

    printf("Main lcore: %u\n", rte_lcore_id());
    printf("Worker lcores: %u\n", nb_workers);
    printf("RX Queues: %u (one per worker)\n", nb_workers);

    /* 创建 mbuf pool */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_workers,
                                       MBUF_CACHE_SIZE, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE,
                                       rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    /* 初始化端口 */
    ret = port_init(port_id, mbuf_pool, nb_workers, nb_workers);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %u\n", port_id);

    /* 启动 worker 核心 */
    printf("\n=== Starting Workers ===\n");
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (queue_id >= nb_workers)
            break;

        params[lcore_id].port_id = port_id;
        params[lcore_id].queue_id = queue_id;
        params[lcore_id].lcore_id = lcore_id;

        printf("Launching worker on lcore %u for queue %u\n",
               lcore_id, queue_id);

        rte_eal_remote_launch(worker_main, &params[lcore_id], lcore_id);
        queue_id++;
    }

    /* 主核心运行统计线程 */
    printf("\n=== Starting Statistics ===\n");
    printf("Statistics will be updated every %u ms\n", STATS_INTERVAL_MS);

    stats_thread(&port_id);

    /* 等待所有 worker 完成 */
    printf("\nWaiting for workers to stop...\n");
    rte_eal_mp_wait_lcore();

    /* 最终统计 */
    printf("\n=== Final Statistics ===\n");
    print_port_stats(port_id, nb_workers);
    print_worker_stats();
    print_load_balance_analysis();

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
