/*
 * DPDK IP Fragmentation and Reassembly
 * Lesson 21: Complete Fragment Processing
 *
 * This example demonstrates:
 * 1. IPv4 fragment detection
 * 2. Fragment reassembly using rte_ip_frag
 * 3. Timeout management
 * 4. Fragment statistics and monitoring
 * 5. Complete packet analysis after reassembly
 * 6. Performance optimization techniques
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_ip_frag.h>

/* 配置参数 */
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

/* 分片表参数 */
#define MAX_FRAG_NUM 4                /* 每个数据包最多片段数 */
#define FRAG_TBL_BUCKET_ENTRIES 16    /* 每个桶的条目数 */
#define MAX_FLOW_NUM 1024             /* 最大流数量 */
#define FRAG_TIMEOUT_MS 5000          /* 5秒超时 */

/* 全局变量 */
static volatile int force_quit = 0;

/* 分片统计 */
struct frag_statistics {
    uint64_t total_packets;           /* 总包数 */
    uint64_t total_fragments;         /* 总片段数 */
    uint64_t first_fragments;         /* 第一个片段 */
    uint64_t middle_fragments;        /* 中间片段 */
    uint64_t last_fragments;          /* 最后片段 */
    uint64_t reassembled;             /* 重组成功 */
    uint64_t timeouts;                /* 超时 */
    uint64_t errors;                  /* 错误 */
    uint64_t non_fragments;           /* 非分片包 */

    /* 重组后的包统计 */
    uint64_t reassembled_tcp;
    uint64_t reassembled_udp;
    uint64_t reassembled_other;

    /* 大小分布 */
    uint64_t size_lt_1500;
    uint64_t size_1500_3000;
    uint64_t size_3000_5000;
    uint64_t size_gt_5000;
} __rte_cache_aligned;

static struct frag_statistics frag_stats;

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
 * 检查是否为 IPv4 分片
 */
static inline int is_ipv4_fragment(const struct rte_ipv4_hdr *ip_hdr)
{
    uint16_t frag_off = rte_be_to_cpu_16(ip_hdr->fragment_offset);

    /* MF 标志位 或 偏移量 > 0 */
    return (frag_off & RTE_IPV4_HDR_MF_FLAG) != 0 ||
           (frag_off & RTE_IPV4_HDR_OFFSET_MASK) != 0;
}

/*
 * 获取分片类型
 */
static inline const char* get_frag_type(const struct rte_ipv4_hdr *ip_hdr)
{
    uint16_t frag_off = rte_be_to_cpu_16(ip_hdr->fragment_offset);
    uint16_t offset = (frag_off & RTE_IPV4_HDR_OFFSET_MASK) * 8;
    int mf = (frag_off & RTE_IPV4_HDR_MF_FLAG) != 0;

    if (offset == 0 && mf)
        return "FIRST";
    else if (offset > 0 && mf)
        return "MIDDLE";
    else if (offset > 0 && !mf)
        return "LAST";
    else
        return "UNKNOWN";
}

/*
 * 更新分片统计
 */
static void update_frag_stats(const struct rte_ipv4_hdr *ip_hdr,
                              int is_frag, int is_reassembled)
{
    frag_stats.total_packets++;

    if (!is_frag) {
        frag_stats.non_fragments++;
        return;
    }

    frag_stats.total_fragments++;

    /* 判断片段类型 */
    uint16_t frag_off = rte_be_to_cpu_16(ip_hdr->fragment_offset);
    uint16_t offset = (frag_off & RTE_IPV4_HDR_OFFSET_MASK) * 8;
    int mf = (frag_off & RTE_IPV4_HDR_MF_FLAG) != 0;

    if (offset == 0 && mf)
        frag_stats.first_fragments++;
    else if (offset > 0 && mf)
        frag_stats.middle_fragments++;
    else if (offset > 0 && !mf)
        frag_stats.last_fragments++;

    if (is_reassembled)
        frag_stats.reassembled++;
}

/*
 * 更新重组后包的统计
 */
static void update_reassembled_stats(struct rte_mbuf *m)
{
    struct rte_ipv4_hdr *ip_hdr;
    uint16_t pkt_len = rte_pktmbuf_pkt_len(m);

    ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
                                     sizeof(struct rte_ether_hdr));

    /* 协议统计 */
    switch (ip_hdr->next_proto_id) {
    case IPPROTO_TCP:
        frag_stats.reassembled_tcp++;
        break;
    case IPPROTO_UDP:
        frag_stats.reassembled_udp++;
        break;
    default:
        frag_stats.reassembled_other++;
        break;
    }

    /* 大小统计 */
    if (pkt_len < 1500)
        frag_stats.size_lt_1500++;
    else if (pkt_len < 3000)
        frag_stats.size_1500_3000++;
    else if (pkt_len < 5000)
        frag_stats.size_3000_5000++;
    else
        frag_stats.size_gt_5000++;
}

/*
 * 打印包详细信息
 */
static void print_packet_info(struct rte_mbuf *m, int is_reassembled)
{
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ip_hdr;
    uint32_t src_ip, dst_ip;
    uint16_t ip_id;

    eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
                                     sizeof(struct rte_ether_hdr));

    src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
    dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
    ip_id = rte_be_to_cpu_16(ip_hdr->packet_id);

    if (is_reassembled) {
        printf("✓ Reassembled packet:\n");
        printf("  IP ID: 0x%04x\n", ip_id);
        printf("  Src: %u.%u.%u.%u → Dst: %u.%u.%u.%u\n",
               (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
               (src_ip >> 8) & 0xFF, src_ip & 0xFF,
               (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
               (dst_ip >> 8) & 0xFF, dst_ip & 0xFF);
        printf("  Protocol: %u\n", ip_hdr->next_proto_id);
        printf("  Total Length: %u bytes\n", rte_pktmbuf_pkt_len(m));

        /* 如果是 TCP/UDP,打印端口信息 */
        if (ip_hdr->next_proto_id == IPPROTO_TCP) {
            struct rte_tcp_hdr *tcp_hdr;
            tcp_hdr = rte_pktmbuf_mtod_offset(m, struct rte_tcp_hdr *,
                                             sizeof(struct rte_ether_hdr) +
                                             sizeof(struct rte_ipv4_hdr));
            printf("  TCP: %u → %u\n",
                   rte_be_to_cpu_16(tcp_hdr->src_port),
                   rte_be_to_cpu_16(tcp_hdr->dst_port));
        } else if (ip_hdr->next_proto_id == IPPROTO_UDP) {
            struct rte_udp_hdr *udp_hdr;
            udp_hdr = rte_pktmbuf_mtod_offset(m, struct rte_udp_hdr *,
                                             sizeof(struct rte_ether_hdr) +
                                             sizeof(struct rte_ipv4_hdr));
            printf("  UDP: %u → %u\n",
                   rte_be_to_cpu_16(udp_hdr->src_port),
                   rte_be_to_cpu_16(udp_hdr->dst_port));
        }
    } else {
        uint16_t frag_off = rte_be_to_cpu_16(ip_hdr->fragment_offset);
        uint16_t offset = (frag_off & RTE_IPV4_HDR_OFFSET_MASK) * 8;

        printf("  Fragment %s: ID=0x%04x, Offset=%u, MF=%d\n",
               get_frag_type(ip_hdr), ip_id, offset,
               (frag_off & RTE_IPV4_HDR_MF_FLAG) != 0);
    }
}

/*
 * 处理 IPv4 数据包 (包括分片重组)
 */
static struct rte_mbuf *
handle_ipv4_packet(struct rte_mbuf *m,
                   struct rte_ip_frag_tbl *frag_tbl,
                   struct rte_ip_frag_death_row *dr,
                   uint64_t cur_tsc,
                   int verbose)
{
    struct rte_ipv4_hdr *ip_hdr;
    int is_frag;

    ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
                                     sizeof(struct rte_ether_hdr));

    /* 检查是否为分片 */
    is_frag = is_ipv4_fragment(ip_hdr);

    if (is_frag) {
        if (verbose) {
            printf("\n→ Received fragment:\n");
            print_packet_info(m, 0);
        }

        /* 尝试重组 */
        struct rte_mbuf *mo;

        mo = rte_ipv4_frag_reassemble_packet(frag_tbl, dr, m,
                                            cur_tsc, ip_hdr);

        if (mo == NULL) {
            /* 片段已缓存,等待其他片段 */
            update_frag_stats(ip_hdr, 1, 0);
            return NULL;
        }

        /* 重组成功 */
        if (verbose) {
            print_packet_info(mo, 1);
        }

        update_frag_stats(ip_hdr, 1, 1);
        update_reassembled_stats(mo);

        return mo;
    } else {
        /* 非分片包 */
        update_frag_stats(ip_hdr, 0, 0);
        return m;
    }
}

/*
 * Worker 核心主函数
 */
static int worker_main(void *arg)
{
    uint16_t port_id = *(uint16_t *)arg;
    unsigned lcore_id = rte_lcore_id();
    uint64_t cur_tsc, last_cleanup = 0;
    uint64_t hz = rte_get_timer_hz();
    uint64_t timeout_cycles = (hz * FRAG_TIMEOUT_MS) / 1000;

    struct rte_mbuf *bufs[BURST_SIZE];
    struct rte_mbuf *output[BURST_SIZE];
    uint16_t nb_rx, nb_out;

    /* 创建分片表 */
    struct rte_ip_frag_tbl *frag_tbl;
    struct rte_ip_frag_death_row death_row;

    uint32_t bucket_num = (MAX_FLOW_NUM + FRAG_TBL_BUCKET_ENTRIES - 1) /
                         FRAG_TBL_BUCKET_ENTRIES;

    frag_tbl = rte_ip_frag_table_create(
        bucket_num,
        FRAG_TBL_BUCKET_ENTRIES,
        MAX_FLOW_NUM,
        65535,  /* Max packet size */
        rte_socket_id()
    );

    if (frag_tbl == NULL) {
        printf("Failed to create fragment table on lcore %u\n", lcore_id);
        return -1;
    }

    printf("Worker core %u started (fragment table created)\n", lcore_id);

    /* 初始化死亡行 */
    death_row.cnt = 0;

    while (!force_quit) {
        cur_tsc = rte_rdtsc();

        /* 定期清理超时的片段 */
        if (cur_tsc - last_cleanup > timeout_cycles) {
            rte_ip_frag_free_death_row(&death_row, BURST_SIZE);
            last_cleanup = cur_tsc;
        }

        /* 收包 */
        nb_rx = rte_eth_rx_burst(port_id, lcore_id - 1, bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0))
            continue;

        nb_out = 0;

        /* 处理每个包 */
        for (uint16_t i = 0; i < nb_rx; i++) {
            struct rte_ether_hdr *eth_hdr;
            uint16_t ether_type;

            eth_hdr = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr *);
            ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

            if (ether_type == RTE_ETHER_TYPE_IPV4) {
                struct rte_mbuf *m;

                m = handle_ipv4_packet(bufs[i], frag_tbl, &death_row,
                                      cur_tsc, 0);

                if (m != NULL) {
                    output[nb_out++] = m;
                }
            } else {
                /* 非 IPv4 包,直接输出 */
                output[nb_out++] = bufs[i];
            }
        }

        /* 释放输出包 */
        for (uint16_t i = 0; i < nb_out; i++) {
            rte_pktmbuf_free(output[i]);
        }
    }

    /* 清理 */
    rte_ip_frag_free_death_row(&death_row, BURST_SIZE);
    rte_ip_frag_table_destroy(frag_tbl);

    printf("Worker core %u stopped\n", lcore_id);
    return 0;
}

/*
 * 打印分片统计
 */
static void print_frag_statistics(void)
{
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║         IP Fragmentation Statistics                    ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    printf("\nOverall:\n");
    printf("  Total Packets:    %15"PRIu64"\n", frag_stats.total_packets);
    printf("  Non-fragments:    %15"PRIu64" (%.1f%%)\n",
           frag_stats.non_fragments,
           frag_stats.total_packets > 0 ?
           (double)frag_stats.non_fragments * 100.0 / frag_stats.total_packets : 0);
    printf("  Total Fragments:  %15"PRIu64" (%.1f%%)\n",
           frag_stats.total_fragments,
           frag_stats.total_packets > 0 ?
           (double)frag_stats.total_fragments * 100.0 / frag_stats.total_packets : 0);

    printf("\nFragment Types:\n");
    printf("  First Fragments:  %15"PRIu64"\n", frag_stats.first_fragments);
    printf("  Middle Fragments: %15"PRIu64"\n", frag_stats.middle_fragments);
    printf("  Last Fragments:   %15"PRIu64"\n", frag_stats.last_fragments);

    printf("\nReassembly:\n");
    printf("  Reassembled:      %15"PRIu64"\n", frag_stats.reassembled);
    printf("  Timeouts:         %15"PRIu64"\n", frag_stats.timeouts);
    printf("  Errors:           %15"PRIu64"\n", frag_stats.errors);

    if (frag_stats.reassembled > 0) {
        printf("\nReassembled Packet Protocols:\n");
        printf("  TCP:              %15"PRIu64" (%.1f%%)\n",
               frag_stats.reassembled_tcp,
               (double)frag_stats.reassembled_tcp * 100.0 / frag_stats.reassembled);
        printf("  UDP:              %15"PRIu64" (%.1f%%)\n",
               frag_stats.reassembled_udp,
               (double)frag_stats.reassembled_udp * 100.0 / frag_stats.reassembled);
        printf("  Other:            %15"PRIu64" (%.1f%%)\n",
               frag_stats.reassembled_other,
               (double)frag_stats.reassembled_other * 100.0 / frag_stats.reassembled);

        printf("\nReassembled Packet Sizes:\n");
        printf("  < 1500:           %15"PRIu64"\n", frag_stats.size_lt_1500);
        printf("  1500-3000:        %15"PRIu64"\n", frag_stats.size_1500_3000);
        printf("  3000-5000:        %15"PRIu64"\n", frag_stats.size_3000_5000);
        printf("  > 5000:           %15"PRIu64"\n", frag_stats.size_gt_5000);
    }

    /* 计算重组率 */
    if (frag_stats.first_fragments > 0) {
        double reassembly_rate = (double)frag_stats.reassembled * 100.0 /
                                frag_stats.first_fragments;
        printf("\nReassembly Success Rate: %.1f%%\n", reassembly_rate);
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
    uint16_t nb_queues;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║   DPDK IP Fragmentation & Reassembly - Lesson 21      ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    if (rte_eth_dev_count_avail() == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");

    nb_queues = rte_lcore_count() - 1;
    if (nb_queues == 0)
        rte_exit(EXIT_FAILURE, "Need at least 2 lcores\n");

    printf("\nConfiguration:\n");
    printf("  Port: %u\n", port_id);
    printf("  Queues: %u\n", nb_queues);
    printf("  Max flows: %u\n", MAX_FLOW_NUM);
    printf("  Fragment timeout: %u ms\n", FRAG_TIMEOUT_MS);

    /* 创建 mbuf pool (需要支持大包) */
    mbuf_pool = rte_pktmbuf_pool_create(
        "MBUF_POOL",
        NUM_MBUFS * nb_queues,
        MBUF_CACHE_SIZE,
        0,
        RTE_MBUF_DEFAULT_BUF_SIZE + 2048,  /* 支持更大的重组包 */
        rte_socket_id()
    );

    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    ret = port_init(port_id, mbuf_pool, nb_queues);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %u\n", port_id);

    /* 启动 worker 核心 */
    printf("\n=== Starting Workers ===\n");
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        rte_eal_remote_launch(worker_main, &port_id, lcore_id);
    }

    printf("\n=== Monitoring (Press Ctrl+C to quit) ===\n");

    /* 主核心监控 */
    while (!force_quit) {
        sleep(2);

        if (force_quit)
            break;

        printf("\033[2J\033[H");
        printf("╔════════════════════════════════════════════════════════╗\n");
        printf("║   DPDK IP Fragmentation Monitoring                     ║\n");
        printf("╚════════════════════════════════════════════════════════╝\n");

        print_frag_statistics();

        printf("\nPress Ctrl+C to quit\n");
    }

    printf("\nWaiting for workers to stop...\n");
    rte_eal_mp_wait_lcore();

    /* 最终统计 */
    printf("\n=== Final Statistics ===\n");
    print_frag_statistics();

    ret = rte_eth_dev_stop(port_id);
    if (ret != 0)
        printf("Port stop failed: %s\n", rte_strerror(-ret));

    rte_eth_dev_close(port_id);
    rte_eal_cleanup();

    printf("\nProgram exited cleanly.\n");
    return 0;
}
