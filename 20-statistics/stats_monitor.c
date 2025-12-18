/*
 * DPDK Statistics and Monitoring System
 * Lesson 20: Comprehensive Performance Monitoring
 *
 * This example demonstrates:
 * 1. Port and queue statistics collection
 * 2. Extended statistics (xstats)
 * 3. Per-core performance metrics
 * 4. Real-time monitoring dashboard
 * 5. Traffic analysis (packet rate, bandwidth, protocol distribution)
 * 6. Alert system for anomaly detection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_metrics.h>

/* 配置参数 */
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define STATS_INTERVAL_SEC 1

/* 全局变量 */
static volatile int force_quit = 0;

/* 性能指标 */
struct perf_metrics {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;

    /* 协议统计 */
    uint64_t tcp_packets;
    uint64_t udp_packets;
    uint64_t icmp_packets;
    uint64_t other_packets;

    /* 包大小分布 */
    uint64_t size_64;
    uint64_t size_65_127;
    uint64_t size_128_255;
    uint64_t size_256_511;
    uint64_t size_512_1023;
    uint64_t size_1024_1518;
    uint64_t size_jumbo;

    /* 速率 */
    uint64_t pps;          /* Packets per second */
    uint64_t bps;          /* Bits per second */
    double mbps;           /* Megabits per second */

    /* 时间戳 */
    uint64_t timestamp;
    uint64_t last_timestamp;
} __rte_cache_aligned;

static struct perf_metrics port_metrics[RTE_MAX_ETHPORTS];
static struct perf_metrics lcore_metrics[RTE_MAX_LCORE];

/* 告警阈值 */
struct alert_thresholds {
    uint64_t max_pps;           /* 最大包速率 */
    uint64_t max_bps;           /* 最大比特率 */
    double max_error_rate;      /* 最大错误率 (%) */
    double max_drop_rate;       /* 最大丢包率 (%) */
};

static struct alert_thresholds thresholds = {
    .max_pps = 1000000,         /* 1M pps */
    .max_bps = 1000000000,      /* 1 Gbps */
    .max_error_rate = 0.01,     /* 0.01% */
    .max_drop_rate = 0.1,       /* 0.1% */
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
 * 计算包大小类别
 */
static inline void update_size_stats(struct perf_metrics *stats, uint16_t pkt_len)
{
    if (pkt_len <= 64)
        stats->size_64++;
    else if (pkt_len <= 127)
        stats->size_65_127++;
    else if (pkt_len <= 255)
        stats->size_128_255++;
    else if (pkt_len <= 511)
        stats->size_256_511++;
    else if (pkt_len <= 1023)
        stats->size_512_1023++;
    else if (pkt_len <= 1518)
        stats->size_1024_1518++;
    else
        stats->size_jumbo++;
}

/*
 * 解析包并更新协议统计
 */
static inline void parse_and_update_stats(struct rte_mbuf *m,
                                         struct perf_metrics *stats)
{
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ipv4_hdr;
    uint16_t ether_type;
    uint16_t pkt_len = rte_pktmbuf_pkt_len(m);

    eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

    /* 更新包大小统计 */
    update_size_stats(stats, pkt_len);

    /* 协议统计 */
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
        case IPPROTO_ICMP:
            stats->icmp_packets++;
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
 * Worker 核心处理函数
 */
static int worker_main(void *arg)
{
    uint16_t port_id = *(uint16_t *)arg;
    uint16_t queue_id = rte_lcore_id() - 1;
    unsigned lcore_id = rte_lcore_id();

    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;
    struct perf_metrics *stats = &lcore_metrics[lcore_id];

    printf("Worker core %u started on queue %u\n", lcore_id, queue_id);

    stats->last_timestamp = rte_get_timer_cycles();

    while (!force_quit) {
        nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0))
            continue;

        stats->rx_packets += nb_rx;

        for (uint16_t i = 0; i < nb_rx; i++) {
            stats->rx_bytes += rte_pktmbuf_pkt_len(bufs[i]);
            parse_and_update_stats(bufs[i], stats);
            rte_pktmbuf_free(bufs[i]);
        }

        stats->timestamp = rte_get_timer_cycles();
    }

    printf("Worker core %u stopped\n", lcore_id);
    return 0;
}

/*
 * 收集端口统计
 */
static void collect_port_stats(uint16_t port_id)
{
    struct rte_eth_stats eth_stats;
    struct perf_metrics *stats = &port_metrics[port_id];
    uint64_t hz = rte_get_timer_hz();

    /* 获取端口统计 */
    int ret = rte_eth_stats_get(port_id, &eth_stats);
    if (ret != 0)
        return;

    uint64_t current_time = rte_get_timer_cycles();
    uint64_t time_diff = current_time - stats->last_timestamp;
    double time_sec = (double)time_diff / hz;

    /* 计算速率 */
    if (time_sec > 0) {
        uint64_t pkt_diff = eth_stats.ipackets - stats->rx_packets;
        uint64_t byte_diff = eth_stats.ibytes - stats->rx_bytes;

        stats->pps = (uint64_t)(pkt_diff / time_sec);
        stats->bps = (uint64_t)((byte_diff * 8) / time_sec);
        stats->mbps = stats->bps / 1000000.0;
    }

    /* 更新统计 */
    stats->rx_packets = eth_stats.ipackets;
    stats->rx_bytes = eth_stats.ibytes;
    stats->tx_packets = eth_stats.opackets;
    stats->tx_bytes = eth_stats.obytes;
    stats->rx_errors = eth_stats.ierrors;
    stats->tx_errors = eth_stats.oerrors;
    stats->rx_dropped = eth_stats.imissed + eth_stats.rx_nombuf;
    stats->timestamp = current_time;
    stats->last_timestamp = current_time;
}

/*
 * 聚合所有 lcore 统计
 */
static void aggregate_lcore_stats(struct perf_metrics *total)
{
    unsigned lcore_id;

    memset(total, 0, sizeof(struct perf_metrics));

    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        struct perf_metrics *stats = &lcore_metrics[lcore_id];

        total->rx_packets += stats->rx_packets;
        total->rx_bytes += stats->rx_bytes;
        total->tcp_packets += stats->tcp_packets;
        total->udp_packets += stats->udp_packets;
        total->icmp_packets += stats->icmp_packets;
        total->other_packets += stats->other_packets;

        total->size_64 += stats->size_64;
        total->size_65_127 += stats->size_65_127;
        total->size_128_255 += stats->size_128_255;
        total->size_256_511 += stats->size_256_511;
        total->size_512_1023 += stats->size_512_1023;
        total->size_1024_1518 += stats->size_1024_1518;
        total->size_jumbo += stats->size_jumbo;
    }
}

/*
 * 打印端口统计
 */
static void print_port_stats(uint16_t port_id)
{
    struct perf_metrics *stats = &port_metrics[port_id];

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║              Port %u Statistics                        ║\n", port_id);
    printf("╚════════════════════════════════════════════════════════╝\n");

    printf("\nTraffic Overview:\n");
    printf("  RX Packets: %15"PRIu64"    TX Packets: %15"PRIu64"\n",
           stats->rx_packets, stats->tx_packets);
    printf("  RX Bytes:   %15"PRIu64"    TX Bytes:   %15"PRIu64"\n",
           stats->rx_bytes, stats->tx_bytes);
    printf("  RX Errors:  %15"PRIu64"    TX Errors:  %15"PRIu64"\n",
           stats->rx_errors, stats->tx_errors);
    printf("  RX Dropped: %15"PRIu64"\n", stats->rx_dropped);

    printf("\nCurrent Rates:\n");
    printf("  Packet Rate: %12"PRIu64" pps\n", stats->pps);
    printf("  Bit Rate:    %12"PRIu64" bps (%.2f Mbps)\n",
           stats->bps, stats->mbps);

    double error_rate = stats->rx_packets > 0 ?
                       (double)stats->rx_errors * 100.0 / stats->rx_packets : 0;
    double drop_rate = stats->rx_packets > 0 ?
                      (double)stats->rx_dropped * 100.0 / stats->rx_packets : 0;

    printf("\nError Rates:\n");
    printf("  Error Rate: %.4f%%\n", error_rate);
    printf("  Drop Rate:  %.4f%%\n", drop_rate);
}

/*
 * 打印协议分布
 */
static void print_protocol_distribution(void)
{
    struct perf_metrics total;
    aggregate_lcore_stats(&total);

    if (total.rx_packets == 0)
        return;

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║           Protocol Distribution                        ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    printf("\n┌──────────┬──────────────┬────────────┐\n");
    printf("│ Protocol │ Packets      │ Percentage │\n");
    printf("├──────────┼──────────────┼────────────┤\n");

    double tcp_pct = (double)total.tcp_packets * 100.0 / total.rx_packets;
    double udp_pct = (double)total.udp_packets * 100.0 / total.rx_packets;
    double icmp_pct = (double)total.icmp_packets * 100.0 / total.rx_packets;
    double other_pct = (double)total.other_packets * 100.0 / total.rx_packets;

    printf("│ TCP      │ %12"PRIu64" │ %8.2f%% │\n", total.tcp_packets, tcp_pct);
    printf("│ UDP      │ %12"PRIu64" │ %8.2f%% │\n", total.udp_packets, udp_pct);
    printf("│ ICMP     │ %12"PRIu64" │ %8.2f%% │\n", total.icmp_packets, icmp_pct);
    printf("│ Other    │ %12"PRIu64" │ %8.2f%% │\n", total.other_packets, other_pct);
    printf("└──────────┴──────────────┴────────────┘\n");
}

/*
 * 打印包大小分布
 */
static void print_size_distribution(void)
{
    struct perf_metrics total;
    aggregate_lcore_stats(&total);

    if (total.rx_packets == 0)
        return;

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║          Packet Size Distribution                      ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    printf("\n┌──────────────┬──────────────┬────────────┐\n");
    printf("│ Size (bytes) │ Packets      │ Percentage │\n");
    printf("├──────────────┼──────────────┼────────────┤\n");

    struct {
        const char *label;
        uint64_t count;
    } size_stats[] = {
        {"≤ 64",         total.size_64},
        {"65-127",       total.size_65_127},
        {"128-255",      total.size_128_255},
        {"256-511",      total.size_256_511},
        {"512-1023",     total.size_512_1023},
        {"1024-1518",    total.size_1024_1518},
        {"> 1518",       total.size_jumbo},
    };

    for (int i = 0; i < 7; i++) {
        double pct = (double)size_stats[i].count * 100.0 / total.rx_packets;
        printf("│ %-12s │ %12"PRIu64" │ %8.2f%% │\n",
               size_stats[i].label, size_stats[i].count, pct);
    }

    printf("└──────────────┴──────────────┴────────────┘\n");
}

/*
 * 检查告警
 */
static void check_alerts(uint16_t port_id)
{
    struct perf_metrics *stats = &port_metrics[port_id];
    int alert = 0;

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║                  Alert System                          ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");

    /* 检查包速率 */
    if (stats->pps > thresholds.max_pps) {
        printf("⚠ HIGH PACKET RATE: %"PRIu64" pps (threshold: %"PRIu64" pps)\n",
               stats->pps, thresholds.max_pps);
        alert = 1;
    }

    /* 检查比特率 */
    if (stats->bps > thresholds.max_bps) {
        printf("⚠ HIGH BIT RATE: %.2f Mbps (threshold: %.2f Mbps)\n",
               stats->mbps, thresholds.max_bps / 1000000.0);
        alert = 1;
    }

    /* 检查错误率 */
    if (stats->rx_packets > 0) {
        double error_rate = (double)stats->rx_errors * 100.0 / stats->rx_packets;
        if (error_rate > thresholds.max_error_rate) {
            printf("⚠ HIGH ERROR RATE: %.4f%% (threshold: %.4f%%)\n",
                   error_rate, thresholds.max_error_rate);
            alert = 1;
        }
    }

    /* 检查丢包率 */
    if (stats->rx_packets > 0) {
        double drop_rate = (double)stats->rx_dropped * 100.0 / stats->rx_packets;
        if (drop_rate > thresholds.max_drop_rate) {
            printf("⚠ HIGH DROP RATE: %.4f%% (threshold: %.4f%%)\n",
                   drop_rate, thresholds.max_drop_rate);
            alert = 1;
        }
    }

    if (!alert) {
        printf("✓ All metrics within normal range\n");
    }
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
    port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_RSS;
    port_conf.rx_adv_conf.rss_conf.rss_hf =
        RTE_ETH_RSS_IP | RTE_ETH_RSS_TCP | RTE_ETH_RSS_UDP;

    ret = rte_eth_dev_configure(port, nb_queues, 1, &port_conf);
    if (ret != 0)
        return ret;

    for (uint16_t q = 0; q < nb_queues; q++) {
        ret = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE,
                                     rte_eth_dev_socket_id(port),
                                     NULL, mbuf_pool);
        if (ret < 0)
            return ret;
    }

    ret = rte_eth_tx_queue_setup(port, 0, TX_RING_SIZE,
                                 rte_eth_dev_socket_id(port), NULL);
    if (ret < 0)
        return ret;

    ret = rte_eth_dev_start(port);
    if (ret < 0)
        return ret;

    ret = rte_eth_promiscuous_enable(port);
    return ret;
}

/*
 * 主函数
 */
int main(int argc, char *argv[])
{
    struct rte_mempool *mbuf_pool;
    uint16_t port_id = 0;
    unsigned lcore_id;
    int ret;
    uint16_t nb_queues = 4;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║   DPDK Statistics and Monitoring - Lesson 20           ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    if (rte_eth_dev_count_avail() == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_queues,
                                       MBUF_CACHE_SIZE, 0,
                                       RTE_MBUF_DEFAULT_BUF_SIZE,
                                       rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    ret = port_init(port_id, mbuf_pool, nb_queues);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %u\n", port_id);

    /* 初始化时间戳 */
    port_metrics[port_id].last_timestamp = rte_get_timer_cycles();

    printf("\n=== Starting Workers ===\n");
    uint16_t queue = 0;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (queue >= nb_queues)
            break;
        rte_eal_remote_launch(worker_main, &port_id, lcore_id);
        queue++;
    }

    printf("\n=== Monitoring Started ===\n");
    printf("Statistics will be updated every %d second(s)\n", STATS_INTERVAL_SEC);

    while (!force_quit) {
        sleep(STATS_INTERVAL_SEC);

        if (force_quit)
            break;

        /* 清屏 */
        printf("\033[2J\033[H");

        /* 收集并打印统计 */
        collect_port_stats(port_id);
        print_port_stats(port_id);
        print_protocol_distribution();
        print_size_distribution();
        check_alerts(port_id);

        printf("\nPress Ctrl+C to quit\n");
    }

    printf("\nWaiting for workers to stop...\n");
    rte_eal_mp_wait_lcore();

    /* 最终统计 */
    printf("\n=== Final Statistics ===\n");
    collect_port_stats(port_id);
    print_port_stats(port_id);
    print_protocol_distribution();
    print_size_distribution();

    ret = rte_eth_dev_stop(port_id);
    if (ret != 0)
        printf("Port stop failed: %s\n", rte_strerror(-ret));

    rte_eth_dev_close(port_id);
    rte_eal_cleanup();

    printf("\nProgram exited cleanly.\n");
    return 0;
}
