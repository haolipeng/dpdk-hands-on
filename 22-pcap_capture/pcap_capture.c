/*
 * DPDK PCAP Capture System
 * Lesson 22: High-Performance Packet Recording
 *
 * This example demonstrates:
 * 1. PCAPNG format recording (Wireshark compatible)
 * 2. Multiple capture strategies (full/sampled/conditional/ring-buffer)
 * 3. Asynchronous writing for performance
 * 4. File rotation by size and time
 * 5. Capture statistics and monitoring
 * 6. Filter-based selective recording
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_pcapng.h>
#include <rte_ring.h>

/* 配置参数 */
#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

/* PCAP 配置 */
#define MAX_CAPTURE_SIZE (1024 * 1024 * 1024UL)  /* 1GB 每个文件 */
#define ROTATE_INTERVAL_SEC 3600                  /* 1小时轮转 */
#define WRITE_RING_SIZE 4096                      /* 写入队列大小 */

/* 捕获模式 */
enum capture_mode {
    CAPTURE_ALL,          /* 全量捕获 */
    CAPTURE_SAMPLED,      /* 采样捕获 */
    CAPTURE_CONDITIONAL,  /* 条件捕获 */
};

/* 全局变量 */
static volatile int force_quit = 0;
static enum capture_mode capture_mode = CAPTURE_ALL;
static uint32_t sample_rate = 100;  /* 采样率: 1/100 */

/* 捕获统计 */
struct capture_stats {
    uint64_t total_packets;        /* 总包数 */
    uint64_t captured_packets;     /* 已捕获 */
    uint64_t dropped_packets;      /* 丢弃 (队列满) */
    uint64_t bytes_written;        /* 写入字节数 */
    uint64_t files_created;        /* 创建文件数 */
} __rte_cache_aligned;

static struct capture_stats cap_stats;

/* 写入上下文 */
struct write_context {
    rte_pcapng_t *pcapng;
    struct rte_ring *write_ring;
    pthread_t writer_thread;
    int fd;
    char filename[256];
    uint64_t file_size;
    uint64_t file_start_time;
    int file_index;
} __rte_cache_aligned;

static struct write_context write_ctx;

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
 * 生成文件名
 */
static void generate_filename(char *buf, size_t size, int index)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];

    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);
    snprintf(buf, size, "capture_%s_%03d.pcapng", timestamp, index);
}

/*
 * 创建新的 PCAP 文件
 */
static int create_new_pcap_file(struct write_context *ctx, uint16_t port_id)
{
    char ifname[RTE_ETH_NAME_MAX_LEN];
    char ifdescr[256];

    /* 关闭旧文件 */
    if (ctx->pcapng != NULL) {
        printf("Closing previous capture file: %s\n", ctx->filename);
        rte_pcapng_write_stats(ctx->pcapng, port_id, 0, 0, NULL);
        rte_pcapng_close(ctx->pcapng);
        close(ctx->fd);
        ctx->file_size = 0;
    }

    /* 生成新文件名 */
    ctx->file_index++;
    generate_filename(ctx->filename, sizeof(ctx->filename), ctx->file_index);

    printf("Creating new capture file: %s\n", ctx->filename);

    /* 打开文件 */
    ctx->fd = open(ctx->filename,
                   O_WRONLY | O_CREAT | O_TRUNC,
                   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (ctx->fd < 0) {
        perror("Failed to create capture file");
        return -1;
    }

    /* 创建 PCAPNG 实例 */
    ctx->pcapng = rte_pcapng_fdopen(ctx->fd,
                                    "Linux",
                                    "x86_64",
                                    "DPDK Packet Capture",
                                    "High-performance capture");

    if (ctx->pcapng == NULL) {
        printf("Failed to create PCAPNG instance\n");
        close(ctx->fd);
        return -1;
    }

    /* 添加接口 */
    rte_eth_dev_get_name_by_port(port_id, ifname);
    snprintf(ifdescr, sizeof(ifdescr), "DPDK port %u", port_id);

    int ret = rte_pcapng_add_interface(ctx->pcapng, port_id,
                                      ifname, ifdescr, NULL);
    if (ret < 0) {
        printf("Failed to add interface\n");
        rte_pcapng_close(ctx->pcapng);
        close(ctx->fd);
        return -1;
    }

    ctx->file_start_time = rte_rdtsc();
    cap_stats.files_created++;

    return 0;
}

/*
 * 检查是否需要轮转文件
 */
static void check_file_rotation(struct write_context *ctx, uint16_t port_id)
{
    int need_rotate = 0;

    /* 按大小轮转 */
    if (ctx->file_size >= MAX_CAPTURE_SIZE) {
        printf("File size limit reached, rotating...\n");
        need_rotate = 1;
    }

    /* 按时间轮转 */
    uint64_t elapsed = rte_rdtsc() - ctx->file_start_time;
    double elapsed_sec = (double)elapsed / rte_get_timer_hz();
    if (elapsed_sec >= ROTATE_INTERVAL_SEC) {
        printf("Time limit reached, rotating...\n");
        need_rotate = 1;
    }

    if (need_rotate) {
        create_new_pcap_file(ctx, port_id);
    }
}

/*
 * 异步写入线程
 */
static void* writer_thread_func(void *arg)
{
    struct write_context *ctx = (struct write_context *)arg;
    uint16_t port_id = 0;
    struct rte_mbuf *bufs[BURST_SIZE];
    unsigned nb_deq;

    printf("Writer thread started\n");

    /* 创建初始文件 */
    if (create_new_pcap_file(ctx, port_id) < 0) {
        return NULL;
    }

    while (!force_quit) {
        /* 从队列取包 */
        nb_deq = rte_ring_dequeue_burst(ctx->write_ring, (void **)bufs,
                                       BURST_SIZE, NULL);

        if (nb_deq == 0) {
            usleep(1000);  /* 1ms */
            continue;
        }

        /* 写入包 */
        for (unsigned i = 0; i < nb_deq; i++) {
            struct rte_mbuf *pkt_arr[1];
            pkt_arr[0] = bufs[i];

            ssize_t bytes = rte_pcapng_write_packets(ctx->pcapng, pkt_arr, 1);

            if (bytes > 0) {
                ctx->file_size += bytes;
                cap_stats.bytes_written += bytes;
                cap_stats.captured_packets++;
            }

            rte_pktmbuf_free(bufs[i]);
        }

        /* 检查文件轮转 */
        check_file_rotation(ctx, port_id);
    }

    /* 清理 */
    printf("Writer thread stopping, flushing remaining packets...\n");

    /* 刷新剩余的包 */
    while ((nb_deq = rte_ring_dequeue_burst(ctx->write_ring, (void **)bufs,
                                           BURST_SIZE, NULL)) > 0) {
        rte_pcapng_write_packets(ctx->pcapng, bufs, nb_deq);
        for (unsigned i = 0; i < nb_deq; i++) {
            rte_pktmbuf_free(bufs[i]);
        }
    }

    /* 关闭文件 */
    if (ctx->pcapng != NULL) {
        rte_pcapng_write_stats(ctx->pcapng, port_id, 0, 0, NULL);
        rte_pcapng_close(ctx->pcapng);
        close(ctx->fd);
    }

    printf("Writer thread stopped\n");
    return NULL;
}

/*
 * 检查是否应该捕获这个包 (条件过滤)
 */
static int should_capture(struct rte_mbuf *m)
{
    struct rte_ether_hdr *eth_hdr;
    struct rte_ipv4_hdr *ipv4_hdr;
    uint16_t ether_type;

    eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);

    if (ether_type == RTE_ETHER_TYPE_IPV4) {
        ipv4_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,
                                          sizeof(struct rte_ether_hdr));

        /* 示例: 只捕获 TCP SYN 包 */
        if (ipv4_hdr->next_proto_id == IPPROTO_TCP) {
            struct rte_tcp_hdr *tcp_hdr;
            tcp_hdr = rte_pktmbuf_mtod_offset(m, struct rte_tcp_hdr *,
                                             sizeof(struct rte_ether_hdr) +
                                             sizeof(struct rte_ipv4_hdr));

            /* TCP SYN 包 */
            if (tcp_hdr->tcp_flags & RTE_TCP_SYN_FLAG) {
                return 1;
            }
        }

        /* 示例: 捕获 ICMP 包 */
        if (ipv4_hdr->next_proto_id == IPPROTO_ICMP) {
            return 1;
        }
    }

    return 0;
}

/*
 * 捕获包
 */
static void capture_packet(struct rte_mbuf *m)
{
    struct rte_mbuf *clone;

    /* 根据捕获模式决定是否捕获 */
    switch (capture_mode) {
    case CAPTURE_ALL:
        /* 全量捕获 */
        break;

    case CAPTURE_SAMPLED:
        /* 采样捕获 */
        if ((cap_stats.total_packets % sample_rate) != 0) {
            return;
        }
        break;

    case CAPTURE_CONDITIONAL:
        /* 条件捕获 */
        if (!should_capture(m)) {
            return;
        }
        break;
    }

    /* 克隆包 (原始包继续处理) */
    clone = rte_pktmbuf_clone(m, m->pool);
    if (clone == NULL) {
        cap_stats.dropped_packets++;
        return;
    }

    /* 入队到写入队列 */
    int ret = rte_ring_enqueue(write_ctx.write_ring, clone);
    if (ret != 0) {
        /* 队列满,丢弃 */
        rte_pktmbuf_free(clone);
        cap_stats.dropped_packets++;
    }
}

/*
 * Worker 核心主函数
 */
static int worker_main(void *arg)
{
    uint16_t port_id = *(uint16_t *)arg;
    unsigned lcore_id = rte_lcore_id();
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx;

    printf("Worker core %u started\n", lcore_id);

    while (!force_quit) {
        nb_rx = rte_eth_rx_burst(port_id, lcore_id - 1, bufs, BURST_SIZE);

        if (unlikely(nb_rx == 0))
            continue;

        cap_stats.total_packets += nb_rx;

        /* 处理每个包 */
        for (uint16_t i = 0; i < nb_rx; i++) {
            /* 捕获包 */
            capture_packet(bufs[i]);

            /* 释放原始包 */
            rte_pktmbuf_free(bufs[i]);
        }
    }

    printf("Worker core %u stopped\n", lcore_id);
    return 0;
}

/*
 * 打印捕获统计
 */
static void print_capture_stats(void)
{
    double capture_rate = cap_stats.total_packets > 0 ?
        (double)cap_stats.captured_packets * 100.0 / cap_stats.total_packets : 0;

    double drop_rate = cap_stats.total_packets > 0 ?
        (double)cap_stats.dropped_packets * 100.0 / cap_stats.total_packets : 0;

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║         PCAP Capture Statistics                        ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    printf("\nPacket Counts:\n");
    printf("  Total Received:   %15"PRIu64"\n", cap_stats.total_packets);
    printf("  Captured:         %15"PRIu64" (%.1f%%)\n",
           cap_stats.captured_packets, capture_rate);
    printf("  Dropped:          %15"PRIu64" (%.1f%%)\n",
           cap_stats.dropped_packets, drop_rate);

    printf("\nFile Information:\n");
    printf("  Current File:     %s\n", write_ctx.filename);
    printf("  Current Size:     %.2f MB\n",
           (double)write_ctx.file_size / (1024 * 1024));
    printf("  Total Written:    %.2f GB\n",
           (double)cap_stats.bytes_written / (1024 * 1024 * 1024));
    printf("  Files Created:    %"PRIu64"\n", cap_stats.files_created);

    printf("\nCapture Mode:\n");
    switch (capture_mode) {
    case CAPTURE_ALL:
        printf("  Mode: Full Capture\n");
        break;
    case CAPTURE_SAMPLED:
        printf("  Mode: Sampled (1/%u)\n", sample_rate);
        break;
    case CAPTURE_CONDITIONAL:
        printf("  Mode: Conditional (TCP SYN + ICMP)\n");
        break;
    }

    /* Ring 队列状态 */
    unsigned ring_count = rte_ring_count(write_ctx.write_ring);
    unsigned ring_free = rte_ring_free_count(write_ctx.write_ring);

    printf("\nWrite Queue:\n");
    printf("  Pending:          %u\n", ring_count);
    printf("  Free Space:       %u\n", ring_free);

    if (ring_count > WRITE_RING_SIZE * 0.8) {
        printf("  ⚠ Warning: Write queue nearly full!\n");
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
 * 打印使用说明
 */
static void print_usage(const char *prgname)
{
    printf("\nUsage: %s [EAL options] -- [options]\n\n", prgname);
    printf("Options:\n");
    printf("  -m MODE    Capture mode:\n");
    printf("               0 = Full capture (default)\n");
    printf("               1 = Sampled capture\n");
    printf("               2 = Conditional capture (TCP SYN + ICMP)\n");
    printf("  -s RATE    Sample rate (default: 100, means 1/100)\n");
    printf("\nExamples:\n");
    printf("  %s -l 0-2 -- -m 0          # Full capture\n", prgname);
    printf("  %s -l 0-2 -- -m 1 -s 100   # Sample 1%%\n", prgname);
    printf("  %s -l 0-2 -- -m 2          # Conditional capture\n\n", prgname);
}

/*
 * 解析命令行参数
 */
static int parse_args(int argc, char **argv)
{
    int opt;

    while ((opt = getopt(argc, argv, "m:s:h")) != -1) {
        switch (opt) {
        case 'm':
            capture_mode = atoi(optarg);
            if (capture_mode > CAPTURE_CONDITIONAL) {
                printf("Invalid capture mode\n");
                return -1;
            }
            break;
        case 's':
            sample_rate = atoi(optarg);
            if (sample_rate == 0) {
                printf("Invalid sample rate\n");
                return -1;
            }
            break;
        case 'h':
            print_usage(argv[0]);
            exit(0);
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    return 0;
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

    argc -= ret;
    argv += ret;

    /* 解析程序参数 */
    ret = parse_args(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid arguments\n");

    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║   DPDK PCAP Capture System - Lesson 22                ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    if (rte_eth_dev_count_avail() == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");

    nb_queues = rte_lcore_count() - 1;
    if (nb_queues == 0)
        rte_exit(EXIT_FAILURE, "Need at least 2 lcores\n");

    printf("\nConfiguration:\n");
    printf("  Port: %u\n", port_id);
    printf("  Queues: %u\n", nb_queues);
    printf("  Capture mode: ");
    switch (capture_mode) {
    case CAPTURE_ALL:
        printf("Full\n");
        break;
    case CAPTURE_SAMPLED:
        printf("Sampled (1/%u)\n", sample_rate);
        break;
    case CAPTURE_CONDITIONAL:
        printf("Conditional\n");
        break;
    }
    printf("  Max file size: %lu MB\n", MAX_CAPTURE_SIZE / (1024 * 1024));
    printf("  Rotate interval: %u seconds\n", ROTATE_INTERVAL_SEC);

    /* 创建 mbuf pool */
    mbuf_pool = rte_pktmbuf_pool_create(
        "MBUF_POOL",
        NUM_MBUFS * nb_queues * 2,  /* 需要更多 mbuf 用于克隆 */
        MBUF_CACHE_SIZE,
        0,
        RTE_MBUF_DEFAULT_BUF_SIZE,
        rte_socket_id()
    );

    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    /* 初始化端口 */
    ret = port_init(port_id, mbuf_pool, nb_queues);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot init port %u\n", port_id);

    /* 创建写入队列 */
    write_ctx.write_ring = rte_ring_create(
        "write_ring",
        WRITE_RING_SIZE,
        rte_socket_id(),
        RING_F_SP_ENQ | RING_F_SC_DEQ
    );

    if (write_ctx.write_ring == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create write ring\n");

    write_ctx.pcapng = NULL;
    write_ctx.file_size = 0;
    write_ctx.file_index = 0;

    /* 启动写入线程 */
    printf("\n=== Starting Writer Thread ===\n");
    ret = pthread_create(&write_ctx.writer_thread, NULL,
                        writer_thread_func, &write_ctx);
    if (ret != 0)
        rte_exit(EXIT_FAILURE, "Cannot create writer thread\n");

    /* 启动 worker 核心 */
    printf("\n=== Starting Workers ===\n");
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        rte_eal_remote_launch(worker_main, &port_id, lcore_id);
    }

    printf("\n=== Capturing (Press Ctrl+C to stop) ===\n");

    /* 主核心监控 */
    while (!force_quit) {
        sleep(2);

        if (force_quit)
            break;

        printf("\033[2J\033[H");
        printf("╔════════════════════════════════════════════════════════╗\n");
        printf("║   DPDK PCAP Capture Monitoring                         ║\n");
        printf("╚════════════════════════════════════════════════════════╝\n");

        print_capture_stats();

        printf("\nPress Ctrl+C to stop capture\n");
    }

    printf("\nWaiting for workers to stop...\n");
    rte_eal_mp_wait_lcore();

    printf("Waiting for writer thread to finish...\n");
    pthread_join(write_ctx.writer_thread, NULL);

    /* 最终统计 */
    printf("\n=== Final Statistics ===\n");
    print_capture_stats();

    printf("\nCapture files created:\n");
    for (int i = 1; i <= write_ctx.file_index; i++) {
        char fname[256];
        generate_filename(fname, sizeof(fname), i);
        struct stat st;
        if (stat(fname, &st) == 0) {
            printf("  %s - %.2f MB\n", fname,
                   (double)st.st_size / (1024 * 1024));
        }
    }

    printf("\nYou can analyze the captures with:\n");
    printf("  wireshark %s\n", write_ctx.filename);
    printf("  tshark -r %s\n", write_ctx.filename);
    printf("  tcpdump -r %s\n", write_ctx.filename);

    ret = rte_eth_dev_stop(port_id);
    if (ret != 0)
        printf("Port stop failed: %s\n", rte_strerror(-ret));

    rte_eth_dev_close(port_id);
    rte_ring_free(write_ctx.write_ring);
    rte_eal_cleanup();

    printf("\nProgram exited cleanly.\n");
    return 0;
}
