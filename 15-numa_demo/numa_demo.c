/*
 * DPDK NUMA Architecture Demonstration
 * Lesson 15: NUMA Basics
 *
 * This example demonstrates:
 * 1. Querying NUMA topology information
 * 2. Getting lcore and device socket IDs
 * 3. Creating resources on specific NUMA nodes
 * 4. Demonstrating local vs remote memory access patterns
 * 5. Performance comparison between local and cross-NUMA access
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
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_malloc.h>

#define RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define TEST_ITERATIONS 1000000
#define BATCH_SIZE 32

/* 全局标志,用于优雅退出 */
static volatile int force_quit = 0;

/* 性能测试结果 */
struct perf_stats {
    uint64_t local_cycles;      // 本地NUMA访问耗时
    uint64_t remote_cycles;     // 跨NUMA访问耗时
    uint64_t iterations;        // 测试迭代次数
};

/* 信号处理函数 */
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = 1;
    }
}

/*
 * 打印NUMA拓扑信息
 * 显示系统中所有NUMA节点和每个lcore的NUMA归属
 */
static void print_numa_topology(void) {
    unsigned lcore_id;
    unsigned socket_id;
    unsigned num_sockets = 0;
    unsigned socket_lcores[RTE_MAX_NUMA_NODES] = {0};

    printf("\n=== NUMA Topology Information ===\n");

    /* 统计NUMA节点数量 */
    RTE_LCORE_FOREACH(lcore_id) {
        socket_id = rte_lcore_to_socket_id(lcore_id);
        if (socket_id + 1 > num_sockets)
            num_sockets = socket_id + 1;
        socket_lcores[socket_id]++;
    }

    printf("Total NUMA nodes: %u\n\n", num_sockets);

    /* 打印每个NUMA节点的lcore信息 */
    for (socket_id = 0; socket_id < num_sockets; socket_id++) {
        printf("NUMA Node %u:\n", socket_id);
        printf("  Lcores: ");

        RTE_LCORE_FOREACH(lcore_id) {
            if (rte_lcore_to_socket_id(lcore_id) == socket_id) {
                printf("%u ", lcore_id);
            }
        }
        printf("\n");
        printf("  Total: %u lcores\n\n", socket_lcores[socket_id]);
    }
}

/*
 * 打印网卡的NUMA信息
 * 显示每个网卡位于哪个NUMA节点
 */
static void print_port_numa_info(void) {
    uint16_t port_id;
    int socket_id;
    uint16_t nb_ports = rte_eth_dev_count_avail();

    if (nb_ports == 0) {
        printf("=== Network Ports ===\n");
        printf("No Ethernet ports available (use --no-pci for demo)\n\n");
        return;
    }

    printf("=== Network Ports NUMA Information ===\n");

    RTE_ETH_FOREACH_DEV(port_id) {
        socket_id = rte_eth_dev_socket_id(port_id);

        if (socket_id == SOCKET_ID_ANY) {
            printf("Port %u: SOCKET_ID_ANY (virtual device or single NUMA)\n",
                   port_id);
        } else {
            printf("Port %u: NUMA Node %d\n", port_id, socket_id);
        }
    }
    printf("\n");
}

/*
 * 在指定NUMA节点创建测试资源
 * 返回: 成功返回mempool指针,失败返回NULL
 */
static struct rte_mempool* create_numa_resources(unsigned socket_id,
                                                  struct rte_ring **ring) {
    char pool_name[RTE_MEMPOOL_NAMESIZE];
    char ring_name[RTE_RING_NAMESIZE];
    struct rte_mempool *mbuf_pool;

    /* 创建带socket标识的名称 */
    snprintf(pool_name, sizeof(pool_name), "mbuf_pool_socket%u", socket_id);
    snprintf(ring_name, sizeof(ring_name), "ring_socket%u", socket_id);

    printf("Creating resources on NUMA Node %u...\n", socket_id);

    /* 在指定NUMA节点创建mbuf pool */
    mbuf_pool = rte_pktmbuf_pool_create(
        pool_name,
        NUM_MBUFS,
        MBUF_CACHE_SIZE,
        0,
        RTE_MBUF_DEFAULT_BUF_SIZE,
        socket_id  /* 关键: 指定NUMA节点 */
    );

    if (mbuf_pool == NULL) {
        rte_exit(EXIT_FAILURE,
                 "Cannot create mbuf pool on socket %u: %s\n",
                 socket_id, rte_strerror(rte_errno));
    }

    printf("  ✓ Mbuf pool '%s' created on NUMA %u\n", pool_name, socket_id);

    /* 在指定NUMA节点创建ring */
    *ring = rte_ring_create(
        ring_name,
        RING_SIZE,
        socket_id,  /* 关键: 指定NUMA节点 */
        RING_F_SP_ENQ | RING_F_SC_DEQ
    );

    if (*ring == NULL) {
        rte_exit(EXIT_FAILURE,
                 "Cannot create ring on socket %u: %s\n",
                 socket_id, rte_strerror(rte_errno));
    }

    printf("  ✓ Ring '%s' created on NUMA %u\n", ring_name, socket_id);

    return mbuf_pool;
}

/*
 * 性能测试: 本地NUMA访问
 * 测试在同一NUMA节点上分配和访问内存的性能
 */
static uint64_t test_local_numa_access(struct rte_mempool *mbuf_pool,
                                         unsigned iterations) {
    struct rte_mbuf *mbufs[BATCH_SIZE];
    uint64_t start_cycles, end_cycles;
    unsigned i, j;

    start_cycles = rte_rdtsc();

    for (i = 0; i < iterations / BATCH_SIZE; i++) {
        /* 从本地NUMA的mempool分配mbuf */
        if (rte_pktmbuf_alloc_bulk(mbuf_pool, mbufs, BATCH_SIZE) != 0) {
            printf("Failed to allocate mbufs\n");
            return 0;
        }

        /* 模拟数据访问 - 写入数据 */
        for (j = 0; j < BATCH_SIZE; j++) {
            char *data = rte_pktmbuf_mtod(mbufs[j], char *);
            memset(data, 0xAA, 64);  /* 写入64字节 */
        }

        /* 释放mbuf回pool */
        for (j = 0; j < BATCH_SIZE; j++) {
            rte_pktmbuf_free(mbufs[j]);
        }
    }

    end_cycles = rte_rdtsc();
    return end_cycles - start_cycles;
}

/*
 * 性能测试: 跨NUMA访问
 * 测试从另一个NUMA节点的内存池分配和访问的性能
 */
static uint64_t test_remote_numa_access(struct rte_mempool *remote_pool,
                                          unsigned iterations) {
    struct rte_mbuf *mbufs[BATCH_SIZE];
    uint64_t start_cycles, end_cycles;
    unsigned i, j;

    start_cycles = rte_rdtsc();

    for (i = 0; i < iterations / BATCH_SIZE; i++) {
        /* 从远程NUMA的mempool分配mbuf */
        if (rte_pktmbuf_alloc_bulk(remote_pool, mbufs, BATCH_SIZE) != 0) {
            printf("Failed to allocate mbufs\n");
            return 0;
        }

        /* 模拟数据访问 - 跨NUMA访问内存 */
        for (j = 0; j < BATCH_SIZE; j++) {
            char *data = rte_pktmbuf_mtod(mbufs[j], char *);
            memset(data, 0xBB, 64);  /* 跨NUMA写入 */
        }

        /* 释放mbuf回pool */
        for (j = 0; j < BATCH_SIZE; j++) {
            rte_pktmbuf_free(mbufs[j]);
        }
    }

    end_cycles = rte_rdtsc();
    return end_cycles - start_cycles;
}

/*
 * 运行NUMA性能对比测试
 */
static void run_numa_performance_test(void) {
    unsigned current_socket = rte_socket_id();
    unsigned remote_socket;
    struct rte_mempool *local_pool, *remote_pool;
    struct rte_ring *local_ring, *remote_ring;
    struct perf_stats stats;
    uint64_t hz = rte_get_timer_hz();

    printf("\n=== NUMA Performance Test ===\n");
    printf("Current lcore %u running on NUMA Node %u\n",
           rte_lcore_id(), current_socket);

    /* 确定远程NUMA节点 */
    if (rte_socket_count() < 2) {
        printf("⚠ Warning: System has only %u NUMA node(s)\n",
               rte_socket_count());
        printf("Cross-NUMA test will use same node (no performance difference expected)\n");
        remote_socket = current_socket;
    } else {
        remote_socket = (current_socket == 0) ? 1 : 0;
        printf("Using NUMA Node %u as remote node for comparison\n\n",
               remote_socket);
    }

    /* 创建本地和远程资源 */
    local_pool = create_numa_resources(current_socket, &local_ring);
    remote_pool = create_numa_resources(remote_socket, &remote_ring);

    printf("\n--- Running Performance Tests ---\n");
    printf("Testing %u iterations with batch size %u...\n\n",
           TEST_ITERATIONS, BATCH_SIZE);

    /* 测试本地NUMA访问 */
    printf("Test 1: Local NUMA access (Node %u → Node %u)...\n",
           current_socket, current_socket);
    stats.local_cycles = test_local_numa_access(local_pool, TEST_ITERATIONS);

    /* 测试跨NUMA访问 */
    printf("Test 2: Remote NUMA access (Node %u → Node %u)...\n",
           current_socket, remote_socket);
    stats.remote_cycles = test_remote_numa_access(remote_pool, TEST_ITERATIONS);

    stats.iterations = TEST_ITERATIONS;

    /* 打印结果 */
    printf("\n=== Performance Results ===\n");
    printf("Test iterations: %"PRIu64"\n", stats.iterations);
    printf("Batch size: %u\n\n", BATCH_SIZE);

    printf("Local NUMA access:\n");
    printf("  Total cycles: %"PRIu64"\n", stats.local_cycles);
    printf("  Cycles per op: %"PRIu64"\n",
           stats.local_cycles / stats.iterations);
    printf("  Time: %.3f ms\n",
           (double)stats.local_cycles * 1000.0 / hz);

    printf("\nRemote NUMA access:\n");
    printf("  Total cycles: %"PRIu64"\n", stats.remote_cycles);
    printf("  Cycles per op: %"PRIu64"\n",
           stats.remote_cycles / stats.iterations);
    printf("  Time: %.3f ms\n",
           (double)stats.remote_cycles * 1000.0 / hz);

    if (stats.local_cycles > 0) {
        double overhead = ((double)stats.remote_cycles / stats.local_cycles - 1.0) * 100.0;
        printf("\n📊 Performance Impact:\n");
        printf("  Remote access overhead: %.1f%%\n", overhead);

        if (overhead > 5.0 && remote_socket != current_socket) {
            printf("  ⚠ Significant cross-NUMA penalty detected!\n");
        } else if (remote_socket == current_socket) {
            printf("  ℹ Single NUMA system - no cross-NUMA penalty expected\n");
        } else {
            printf("  ✓ Low cross-NUMA penalty (good cache locality)\n");
        }
    }
}

/*
 * 演示正确和错误的NUMA用法
 */
static void demonstrate_numa_best_practices(void) {
    unsigned current_socket = rte_socket_id();
    unsigned wrong_socket = (current_socket == 0) ? 1 : 0;

    printf("\n=== NUMA Best Practices ===\n\n");

    printf("✅ CORRECT: Create resources on local NUMA node\n");
    printf("   unsigned socket_id = rte_socket_id();  // Get current socket\n");
    printf("   struct rte_ring *ring = rte_ring_create(\n");
    printf("       \"my_ring\", 1024, socket_id, 0);  // ← Use local socket\n");
    printf("   Current socket: %u ✓\n\n", current_socket);

    printf("❌ WRONG: Create on wrong NUMA node\n");
    printf("   struct rte_ring *ring = rte_ring_create(\n");
    printf("       \"my_ring\", 1024, %u, 0);  // ← Wrong socket!\n", wrong_socket);
    printf("   This causes cross-NUMA access penalty\n\n");

    printf("✅ CORRECT: Bind mempool to NIC socket\n");
    printf("   uint16_t port_id = 0;\n");
    printf("   int port_socket = rte_eth_dev_socket_id(port_id);\n");
    printf("   struct rte_mempool *pool = rte_pktmbuf_pool_create(\n");
    printf("       \"mbuf_pool\", 8192, 250, 0, 2048, port_socket);\n\n");

    printf("❌ WRONG: Use SOCKET_ID_ANY (unpredictable)\n");
    printf("   struct rte_mempool *pool = rte_pktmbuf_pool_create(\n");
    printf("       \"mbuf_pool\", 8192, 250, 0, 2048, SOCKET_ID_ANY);\n");
    printf("   Don't rely on system to choose!\n\n");

    printf("💡 Pro Tips:\n");
    printf("   1. Use 'numactl --hardware' to check system topology\n");
    printf("   2. Use 'cat /sys/class/net/ethX/device/numa_node' for NIC location\n");
    printf("   3. Launch app with: --socket-mem=1024,0 to limit memory per node\n");
    printf("   4. Use 'numastat -p <pid>' to monitor NUMA memory usage\n");
}

/*
 * 主函数
 */
int main(int argc, char **argv) {
    int ret;
    unsigned lcore_id;

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    /* 打印欢迎信息 */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║   DPDK NUMA Architecture Demonstration - Lesson 15    ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    /* 检查lcore数量 */
    if (rte_lcore_count() < 1) {
        rte_exit(EXIT_FAILURE, "Need at least 1 lcore\n");
    }

    lcore_id = rte_lcore_id();
    printf("Running on lcore %u (NUMA Node %u)\n",
           lcore_id, rte_socket_id());
    printf("Total system NUMA nodes: %u\n", rte_socket_count());
    printf("Total available lcores: %u\n", rte_lcore_count());

    /* 1. 打印NUMA拓扑 */
    print_numa_topology();

    /* 2. 打印网卡NUMA信息 */
    print_port_numa_info();

    /* 3. 演示最佳实践 */
    demonstrate_numa_best_practices();

    /* 4. 运行性能测试 */
    if (!force_quit) {
        run_numa_performance_test();
    }

    /* 总结 */
    printf("\n=== Summary ===\n");
    printf("Key takeaways:\n");
    printf("  1. Always check NUMA topology with rte_socket_id()\n");
    printf("  2. Create resources on the same NUMA node as the worker lcore\n");
    printf("  3. Bind mempool to the same NUMA node as the NIC\n");
    printf("  4. Cross-NUMA access can cause 30-50%% performance penalty\n");
    printf("  5. Use numactl and numastat for monitoring\n");

    printf("\n📚 For multi-NUMA systems, run with:\n");
    printf("   sudo ./numa_demo -l 0-3 --socket-mem=1024,1024\n");
    printf("   (Allocates memory on both NUMA nodes)\n");

    /* 清理EAL */
    rte_eal_cleanup();

    printf("\nProgram exited cleanly.\n");
    return 0;
}
