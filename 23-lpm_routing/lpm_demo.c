/*
 * Lesson 23: LPM (Longest Prefix Match) Routing Demo
 *
 * 本示例演示:
 * 1. LPM 路由表的创建和配置
 * 2. 添加静态路由 (IPv4)
 * 3. 单个和批量路由查找
 * 4. 模拟数据包转发
 * 5. 路由查找性能测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_lpm.h>
#include <rte_cycles.h>
#include <rte_random.h>
#include <rte_ip.h>

/* 配置参数 */
#define MAX_ROUTES          1024
#define NUM_TBL8S           256
#define MBUF_CACHE_SIZE     256
#define NUM_MBUFS           8191
#define BURST_SIZE          32
#define TEST_ITERATIONS     1000000

/* 下一跳类型 */
#define NH_TYPE_DIRECT      0    /* 直连路由 */
#define NH_TYPE_GATEWAY     1    /* 网关路由 */
#define NH_TYPE_BLACKHOLE   2    /* 黑洞路由 */
#define NH_TYPE_REJECT      3    /* 拒绝路由 */

/* 辅助宏 */
#define IPv4(a, b, c, d) ((uint32_t)(((a) << 24) | ((b) << 16) | ((c) << 8) | (d)))

/* 统计信息 */
struct lpm_stats {
    uint64_t lookups;              /* 查找次数 */
    uint64_t hits;                 /* 命中次数 */
    uint64_t misses;               /* 未命中次数 */
    uint64_t packets_forwarded;    /* 转发的包 */
    uint64_t packets_dropped;      /* 丢弃的包 */
    uint64_t total_cycles;         /* 查找总周期数 */
} __rte_cache_aligned;

/* 下一跳信息 */
struct next_hop_info {
    uint8_t type;           /* 下一跳类型 */
    uint8_t port_id;        /* 出端口 */
    uint32_t gateway_ip;    /* 网关 IP (如果是网关路由) */
    char description[64];   /* 描述 */
};

/* 路由条目 */
struct route_entry {
    uint32_t ip;        /* 网络地址 */
    uint8_t depth;      /* 前缀长度 */
    uint32_t next_hop;  /* 下一跳 ID */
    char description[64];
};

/* 全局变量 */
static struct rte_lpm *lpm = NULL;
static struct rte_mempool *mbuf_pool = NULL;
static struct lpm_stats stats;
static struct next_hop_info next_hop_table[256];
static volatile sig_atomic_t force_quit = 0;

/* 信号处理 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = 1;
    }
}

/* 解析 IPv4 地址字符串 */
static uint32_t parse_ipv4(const char *ip_str)
{
    unsigned int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }
    return IPv4(a, b, c, d);
}

/* 将 IPv4 地址转换为字符串 */
static void ipv4_to_string(uint32_t ip, char *buf, size_t len)
{
    snprintf(buf, len, "%u.%u.%u.%u",
             (ip >> 24) & 0xFF,
             (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF,
             ip & 0xFF);
}

/* 初始化下一跳表 */
static void init_next_hop_table(void)
{
    /* 下一跳 0: 直连网络 - 端口 0 */
    next_hop_table[0].type = NH_TYPE_DIRECT;
    next_hop_table[0].port_id = 0;
    snprintf(next_hop_table[0].description, sizeof(next_hop_table[0].description),
             "Direct - Port 0");

    /* 下一跳 1: 直连网络 - 端口 1 */
    next_hop_table[1].type = NH_TYPE_DIRECT;
    next_hop_table[1].port_id = 1;
    snprintf(next_hop_table[1].description, sizeof(next_hop_table[1].description),
             "Direct - Port 1");

    /* 下一跳 10: 默认网关 */
    next_hop_table[10].type = NH_TYPE_GATEWAY;
    next_hop_table[10].port_id = 0;
    next_hop_table[10].gateway_ip = IPv4(10, 0, 0, 1);
    snprintf(next_hop_table[10].description, sizeof(next_hop_table[10].description),
             "Gateway 10.0.0.1");

    /* 下一跳 11: ISP 网关 */
    next_hop_table[11].type = NH_TYPE_GATEWAY;
    next_hop_table[11].port_id = 1;
    next_hop_table[11].gateway_ip = IPv4(172, 16, 0, 1);
    snprintf(next_hop_table[11].description, sizeof(next_hop_table[11].description),
             "ISP Gateway 172.16.0.1");

    /* 下一跳 254: 黑洞路由 */
    next_hop_table[254].type = NH_TYPE_BLACKHOLE;
    snprintf(next_hop_table[254].description, sizeof(next_hop_table[254].description),
             "Blackhole");

    /* 下一跳 255: 拒绝路由 */
    next_hop_table[255].type = NH_TYPE_REJECT;
    snprintf(next_hop_table[255].description, sizeof(next_hop_table[255].description),
             "Reject");
}

/* 添加路由到 LPM 表 */
static int add_route(uint32_t ip, uint8_t depth, uint32_t next_hop, const char *description)
{
    int ret;
    char ip_str[32];

    ret = rte_lpm_add(lpm, ip, depth, next_hop);
    if (ret < 0) {
        printf("Failed to add route: %s/%u -> %u\n",
               ip_str, depth, next_hop);
        return ret;
    }

    ipv4_to_string(ip, ip_str, sizeof(ip_str));
    printf("Added route: %-18s/%2u -> NH %3u (%s)\n",
           ip_str, depth, next_hop, description);

    return 0;
}

/* 初始化路由表 */
static int init_routing_table(void)
{
    printf("\n=== Initializing Routing Table ===\n\n");

    /* 直连网络 */
    add_route(IPv4(10, 0, 0, 0), 24, 0, "Local LAN");
    add_route(IPv4(192, 168, 1, 0), 24, 0, "Local Subnet");

    /* 企业网络 */
    add_route(IPv4(172, 16, 0, 0), 12, 1, "Enterprise Network");
    add_route(IPv4(172, 16, 10, 0), 24, 1, "Engineering");
    add_route(IPv4(172, 16, 20, 0), 24, 1, "Sales");

    /* 公网路由 */
    add_route(IPv4(8, 8, 8, 0), 24, 10, "Google DNS");
    add_route(IPv4(1, 1, 1, 0), 24, 10, "Cloudflare DNS");

    /* 私有网络 */
    add_route(IPv4(10, 0, 0, 0), 8, 10, "Private 10/8");

    /* 黑洞路由 (阻止某些子网) */
    add_route(IPv4(192, 0, 2, 0), 24, 254, "TEST-NET-1 (RFC 5737)");
    add_route(IPv4(198, 51, 100, 0), 24, 254, "TEST-NET-2 (RFC 5737)");
    add_route(IPv4(203, 0, 113, 0), 24, 254, "TEST-NET-3 (RFC 5737)");

    /* 默认路由 (通过 ISP 网关) */
    add_route(IPv4(0, 0, 0, 0), 0, 11, "Default Route (Internet)");

    printf("\n");
    return 0;
}

/* 单个 IP 查找 */
static int lookup_single(uint32_t ip, uint32_t *next_hop)
{
    uint64_t start, end;

    start = rte_rdtsc();
    int ret = rte_lpm_lookup(lpm, ip, next_hop);
    end = rte_rdtsc();

    stats.lookups++;
    stats.total_cycles += (end - start);

    if (ret == 0) {
        stats.hits++;
        return 0;
    } else {
        stats.misses++;
        return -1;
    }
}

/* 批量 IP 查找 */
static int lookup_bulk(const uint32_t *ips, uint32_t *next_hops, unsigned int num)
{
    uint64_t start, end;
    int ret;

    start = rte_rdtsc();
    ret = rte_lpm_lookup_bulk(lpm, ips, next_hops, num);
    end = rte_rdtsc();

    stats.lookups += num;
    stats.total_cycles += (end - start);

    /* 统计命中和未命中 */
    for (unsigned int i = 0; i < num; i++) {
        if (next_hops[i] & RTE_LPM_LOOKUP_SUCCESS) {
            stats.hits++;
            next_hops[i] &= ~RTE_LPM_LOOKUP_SUCCESS;  /* 清除标志位 */
        } else {
            stats.misses++;
        }
    }

    return ret;
}

/* 处理数据包 (模拟) */
static void process_packet(uint32_t dst_ip)
{
    uint32_t next_hop;
    char ip_str[32];

    if (lookup_single(dst_ip, &next_hop) == 0) {
        struct next_hop_info *nh = &next_hop_table[next_hop];

        switch (nh->type) {
        case NH_TYPE_DIRECT:
        case NH_TYPE_GATEWAY:
            stats.packets_forwarded++;
            break;
        case NH_TYPE_BLACKHOLE:
        case NH_TYPE_REJECT:
            stats.packets_dropped++;
            break;
        }
    } else {
        /* 没有匹配的路由,丢弃 */
        ipv4_to_string(dst_ip, ip_str, sizeof(ip_str));
        printf("No route to host: %s\n", ip_str);
        stats.packets_dropped++;
    }
}

/* 演示路由查找 */
static void demo_routing_lookups(void)
{
    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║           Routing Lookup Demonstrations               ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");

    /* 测试用例 */
    struct {
        const char *ip_str;
        const char *description;
    } test_cases[] = {
        {"10.0.0.100", "Local LAN"},
        {"192.168.1.50", "Local Subnet"},
        {"172.16.10.5", "Engineering"},
        {"172.16.20.10", "Sales"},
        {"8.8.8.8", "Google DNS"},
        {"1.1.1.1", "Cloudflare DNS"},
        {"192.0.2.1", "TEST-NET (should drop)"},
        {"93.184.216.34", "Internet (default route)"},
        {"127.0.0.1", "Localhost (no route)"},
    };

    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        uint32_t ip = parse_ipv4(test_cases[i].ip_str);
        uint32_t next_hop;
        char gw_str[32];

        printf("%-30s %-20s", test_cases[i].description, test_cases[i].ip_str);

        if (lookup_single(ip, &next_hop) == 0) {
            struct next_hop_info *nh = &next_hop_table[next_hop];

            printf(" -> NH %3u: %-20s", next_hop, nh->description);

            if (nh->type == NH_TYPE_GATEWAY) {
                ipv4_to_string(nh->gateway_ip, gw_str, sizeof(gw_str));
                printf(" [via %s]", gw_str);
            }

            if (nh->type == NH_TYPE_BLACKHOLE || nh->type == NH_TYPE_REJECT) {
                printf(" ⛔");
            } else {
                printf(" ✓");
            }
        } else {
            printf(" -> No route ❌");
        }

        printf("\n");
    }

    printf("\n");
}

/* 批量查找性能测试 */
static void benchmark_bulk_lookup(void)
{
    const unsigned int batch_sizes[] = {1, 8, 16, 32, 64};
    uint32_t test_ips[64];
    uint32_t next_hops[64];
    uint64_t start, end;
    double lookups_per_sec;

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║         Bulk Lookup Performance Benchmark             ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");

    /* 生成测试 IP 地址 */
    for (unsigned int i = 0; i < 64; i++) {
        /* 随机生成 IP (主要在已知网段) */
        uint32_t r = rte_rand();
        if (r % 3 == 0) {
            test_ips[i] = IPv4(10, 0, 0, r % 256);
        } else if (r % 3 == 1) {
            test_ips[i] = IPv4(172, 16, (r >> 8) % 256, r % 256);
        } else {
            test_ips[i] = IPv4(r >> 24, (r >> 16) & 0xFF,
                               (r >> 8) & 0xFF, r & 0xFF);
        }
    }

    printf("Batch Size    Lookups/sec      Cycles/Lookup    Time/Lookup\n");
    printf("──────────────────────────────────────────────────────────\n");

    for (size_t i = 0; i < sizeof(batch_sizes) / sizeof(batch_sizes[0]); i++) {
        unsigned int batch_size = batch_sizes[i];
        uint64_t total_cycles = 0;
        uint64_t total_lookups = 0;

        start = rte_rdtsc();

        /* 执行多次批量查找 */
        for (unsigned int iter = 0; iter < TEST_ITERATIONS / batch_size; iter++) {
            uint64_t batch_start = rte_rdtsc();
            lookup_bulk(test_ips, next_hops, batch_size);
            uint64_t batch_end = rte_rdtsc();

            total_cycles += (batch_end - batch_start);
            total_lookups += batch_size;
        }

        end = rte_rdtsc();

        double elapsed_sec = (double)(end - start) / rte_get_timer_hz();
        lookups_per_sec = total_lookups / elapsed_sec;
        double cycles_per_lookup = (double)total_cycles / total_lookups;
        double time_per_lookup_ns = cycles_per_lookup * 1e9 / rte_get_timer_hz();

        printf("%4u          %12.2f M   %10.2f       %8.2f ns\n",
               batch_size,
               lookups_per_sec / 1e6,
               cycles_per_lookup,
               time_per_lookup_ns);
    }

    printf("\n");
}

/* 模拟数据包处理 */
static void simulate_packet_forwarding(void)
{
    const unsigned int num_packets = 10000;
    uint32_t test_ips[BURST_SIZE];
    uint32_t next_hops[BURST_SIZE];

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║         Simulating Packet Forwarding                  ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");

    printf("Processing %u packets...\n", num_packets);

    uint64_t forwarded = 0;
    uint64_t dropped = 0;
    uint64_t start = rte_rdtsc();

    for (unsigned int i = 0; i < num_packets; i += BURST_SIZE) {
        unsigned int batch = RTE_MIN(BURST_SIZE, num_packets - i);

        /* 生成随机目标 IP */
        for (unsigned int j = 0; j < batch; j++) {
            uint32_t r = rte_rand();
            if (r % 3 == 0) {
                test_ips[j] = IPv4(10, 0, 0, r % 256);
            } else if (r % 3 == 1) {
                test_ips[j] = IPv4(172, 16, (r >> 8) % 256, r % 256);
            } else {
                test_ips[j] = IPv4(r >> 24, (r >> 16) & 0xFF,
                                   (r >> 8) & 0xFF, r & 0xFF);
            }
        }

        /* 批量查找 */
        lookup_bulk(test_ips, next_hops, batch);

        /* 处理结果 */
        for (unsigned int j = 0; j < batch; j++) {
            uint32_t nh = next_hops[j];

            if (nh < 256) {  /* 有效的下一跳 */
                struct next_hop_info *nh_info = &next_hop_table[nh];

                if (nh_info->type == NH_TYPE_BLACKHOLE ||
                    nh_info->type == NH_TYPE_REJECT) {
                    dropped++;
                } else {
                    forwarded++;
                }
            } else {
                dropped++;  /* 未找到路由 */
            }
        }
    }

    uint64_t end = rte_rdtsc();
    double elapsed_sec = (double)(end - start) / rte_get_timer_hz();
    double pps = num_packets / elapsed_sec;

    printf("\nResults:\n");
    printf("  Total Packets:        %u\n", num_packets);
    printf("  Forwarded:            %" PRIu64 " (%.1f%%)\n",
           forwarded, 100.0 * forwarded / num_packets);
    printf("  Dropped:              %" PRIu64 " (%.1f%%)\n",
           dropped, 100.0 * dropped / num_packets);
    printf("  Processing Time:      %.3f ms\n", elapsed_sec * 1000);
    printf("  Throughput:           %.2f Mpps\n\n", pps / 1e6);
}

/* 打印统计信息 */
static void print_statistics(void)
{
    double avg_cycles = stats.lookups > 0 ?
                        (double)stats.total_cycles / stats.lookups : 0;
    double avg_time_ns = avg_cycles * 1e9 / rte_get_timer_hz();
    double hit_rate = stats.lookups > 0 ?
                      100.0 * stats.hits / stats.lookups : 0;

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║                LPM Statistics                          ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");

    printf("Lookup Statistics:\n");
    printf("  Total Lookups:        %" PRIu64 "\n", stats.lookups);
    printf("  Hits:                 %" PRIu64 " (%.2f%%)\n",
           stats.hits, hit_rate);
    printf("  Misses:               %" PRIu64 " (%.2f%%)\n",
           stats.misses, 100.0 - hit_rate);

    printf("\nPerformance:\n");
    printf("  Avg Cycles/Lookup:    %.2f\n", avg_cycles);
    printf("  Avg Time/Lookup:      %.2f ns\n", avg_time_ns);
    printf("  Estimated Throughput: %.2f Mlookups/s\n",
           rte_get_timer_hz() / avg_cycles / 1e6);

    printf("\nPacket Statistics:\n");
    printf("  Forwarded:            %" PRIu64 "\n", stats.packets_forwarded);
    printf("  Dropped:              %" PRIu64 "\n", stats.packets_dropped);

    printf("\n");
}

/* 主函数 */
int main(int argc, char *argv[])
{
    int ret;
    unsigned int socket_id;

    /* 初始化 EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

    argc -= ret;
    argv += ret;

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 获取 NUMA socket */
    socket_id = rte_socket_id();

    printf("\n╔════════════════════════════════════════════════════════╗\n");
    printf("║   DPDK LPM (Longest Prefix Match) Routing Demo        ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n\n");

    printf("Configuration:\n");
    printf("  NUMA Socket:          %u\n", socket_id);
    printf("  Max Routes:           %u\n", MAX_ROUTES);
    printf("  Number of TBL8s:      %u\n", NUM_TBL8S);
    printf("\n");

    /* 创建内存池 */
    mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NUM_MBUFS,
                                        MBUF_CACHE_SIZE, 0,
                                        RTE_MBUF_DEFAULT_BUF_SIZE,
                                        socket_id);
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    /* 创建 LPM 表 */
    struct rte_lpm_config config = {
        .max_rules = MAX_ROUTES,
        .number_tbl8s = NUM_TBL8S,
        .flags = 0
    };

    lpm = rte_lpm_create("IPv4_LPM", socket_id, &config);
    if (lpm == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create LPM table: %s\n",
                 rte_strerror(rte_errno));

    printf("LPM table created successfully\n");

    /* 初始化下一跳表 */
    init_next_hop_table();

    /* 初始化路由表 */
    init_routing_table();

    /* 演示路由查找 */
    demo_routing_lookups();

    /* 性能测试 */
    benchmark_bulk_lookup();

    /* 模拟数据包转发 */
    simulate_packet_forwarding();

    /* 打印统计信息 */
    print_statistics();

    /* 清理资源 */
    printf("Cleaning up...\n");
    rte_lpm_free(lpm);
    rte_mempool_free(mbuf_pool);

    /* 清理 EAL */
    rte_eal_cleanup();

    printf("Demo completed successfully!\n\n");

    return 0;
}
