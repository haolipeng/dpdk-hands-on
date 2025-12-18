/*
 * DPDK Performance Benchmark Framework
 * Lesson 16: Comprehensive Performance Testing
 *
 * This example demonstrates:
 * 1. Performance testing framework design
 * 2. Benchmarking various DPDK operations
 * 3. Measuring throughput and latency
 * 4. Comparing different implementation approaches
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>

#include <rte_eal.h>
#include <rte_cycles.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>

/* 全局标志,用于优雅退出 */
static volatile int force_quit = 0;

/* 性能测试结果结构 */
struct benchmark_result {
    const char *name;           /* 测试名称 */
    uint64_t total_cycles;      /* 总周期数 */
    uint64_t iterations;        /* 迭代次数 */
    double total_time_us;       /* 总时间(微秒) */
    double per_op_ns;           /* 每次操作耗时(纳秒) */
    double ops_per_sec;         /* 每秒操作数 */
};

/* 全局变量 */
static uint64_t g_hz;

/* 信号处理函数 */
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = 1;
    }
}

/*
 * 打印测试结果
 */
static void print_result(struct benchmark_result *result)
{
    printf("  ├─ 测试项: %s\n", result->name);
    printf("  │   ├─ 迭代次数: %lu\n", result->iterations);
    printf("  │   ├─ 总耗时: %.2f 微秒 (%.3f 毫秒)\n",
           result->total_time_us, result->total_time_us / 1000.0);
    printf("  │   ├─ 每次操作: %.2f 纳秒\n", result->per_op_ns);
    printf("  │   └─ 吞吐量: %.2f M ops/sec\n", result->ops_per_sec / 1e6);
    printf("  │\n");
}

/*
 * 计算测试结果
 */
static void calc_result(struct benchmark_result *result,
                        uint64_t start, uint64_t end,
                        uint64_t iterations)
{
    result->total_cycles = end - start;
    result->iterations = iterations;
    result->total_time_us = (double)result->total_cycles * 1e6 / g_hz;
    result->per_op_ns = (double)result->total_cycles * 1e9 / g_hz / iterations;
    result->ops_per_sec = (double)iterations * g_hz / result->total_cycles;
}

/*
 * 测试1: 空循环(测量基准开销)
 */
static void bench_empty_loop(void)
{
    struct benchmark_result result = {.name = "空循环(基准开销)"};
    uint64_t start, end;
    uint64_t iterations = 100000000;  /* 1亿次 */

    printf("  测试1: 空循环 - 测量循环本身的开销\n");
    printf("  迭代次数: %lu\n", iterations);

    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        /* 什么都不做 */
        __asm__ volatile("" ::: "memory");  /* 防止编译器优化掉循环 */
    }
    end = rte_get_timer_cycles();

    calc_result(&result, start, end, iterations);
    print_result(&result);
}

/*
 * 测试2: 读取时钟周期
 */
static void bench_rdtsc(void)
{
    struct benchmark_result result = {.name = "rte_get_timer_cycles()"};
    uint64_t start, end;
    uint64_t iterations = 10000000;  /* 1000万次 */
    volatile uint64_t dummy;

    printf("  测试2: 计时 API 调用开销\n");
    printf("  迭代次数: %lu\n", iterations);

    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        dummy = rte_get_timer_cycles();
    }
    end = rte_get_timer_cycles();
    (void)dummy;

    calc_result(&result, start, end, iterations);
    print_result(&result);
}

/*
 * 测试3: 内存分配和释放
 */
static void bench_malloc(void)
{
    struct benchmark_result result = {.name = "rte_malloc/free (64字节)"};
    uint64_t start, end;
    uint64_t iterations = 1000000;  /* 100万次 */

    printf("  测试3: 内存分配和释放性能\n");
    printf("  迭代次数: %lu\n", iterations);
    printf("  每次分配: 64 字节\n");

    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        void *ptr = rte_malloc(NULL, 64, 0);
        if (ptr)
            rte_free(ptr);
    }
    end = rte_get_timer_cycles();

    calc_result(&result, start, end, iterations);
    print_result(&result);
}

/*
 * 测试4: 内存拷贝(固定大小)
 */
static void bench_memcpy_fixed(void)
{
    struct benchmark_result result = {.name = "rte_memcpy (1KB)"};
    uint64_t start, end;
    uint64_t iterations = 10000000;  /* 1000万次 */
    const int size = 1024;

    printf("  测试4: 内存拷贝性能 (1KB)\n");
    printf("  迭代次数: %lu\n", iterations);

    char *src = rte_malloc(NULL, size, 64);
    char *dst = rte_malloc(NULL, size, 64);

    if (!src || !dst) {
        printf("  内存分配失败,跳过测试\n");
        rte_free(src);
        rte_free(dst);
        return;
    }

    memset(src, 0xAA, size);

    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        rte_memcpy(dst, src, size);
    }
    end = rte_get_timer_cycles();

    calc_result(&result, start, end, iterations);
    print_result(&result);

    /* 计算内存带宽 */
    double bandwidth_gbps = (double)iterations * size * 8 /
                           result.total_time_us / 1000;
    printf("  │   └─ 内存带宽: %.2f Gbps\n", bandwidth_gbps);
    printf("  │\n");

    rte_free(src);
    rte_free(dst);
}

/*
 * 测试5: Ring 入队/出队
 */
static void bench_ring(void)
{
    struct benchmark_result result = {.name = "Ring enqueue/dequeue (SP/SC)"};
    uint64_t start, end;
    uint64_t iterations = 10000000;  /* 1000万次 */

    printf("  测试5: Ring 队列操作性能\n");
    printf("  迭代次数: %lu\n", iterations);

    struct rte_ring *ring = rte_ring_create("bench_ring", 1024,
                                            rte_socket_id(),
                                            RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (ring == NULL) {
        printf("  ├─ 创建 Ring 失败,跳过测试\n");
        return;
    }

    void *obj = (void *)0x12345678;

    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        rte_ring_enqueue(ring, obj);
        rte_ring_dequeue(ring, &obj);
    }
    end = rte_get_timer_cycles();

    calc_result(&result, start, end, iterations);
    print_result(&result);

    rte_ring_free(ring);
}

/*
 * 测试6: Ring 批量操作
 */
static void bench_ring_bulk(void)
{
    struct benchmark_result result = {.name = "Ring bulk enqueue/dequeue"};
    uint64_t start, end;
    uint64_t iterations = 1000000;  /* 100万次 */
    const int bulk_size = 32;

    printf("  测试6: Ring 批量操作性能\n");
    printf("  迭代次数: %lu\n", iterations);
    printf("  批量大小: %d\n", bulk_size);

    struct rte_ring *ring = rte_ring_create("bench_ring_bulk", 1024,
                                            rte_socket_id(),
                                            RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (ring == NULL) {
        printf("  ├─ 创建 Ring 失败,跳过测试\n");
        return;
    }

    void *objs[bulk_size];
    for (int i = 0; i < bulk_size; i++) {
        objs[i] = (void *)(uintptr_t)(0x1000 + i);
    }

    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        rte_ring_enqueue_bulk(ring, objs, bulk_size, NULL);
        rte_ring_dequeue_bulk(ring, objs, bulk_size, NULL);
    }
    end = rte_get_timer_cycles();

    calc_result(&result, start, end, iterations * bulk_size);
    print_result(&result);

    rte_ring_free(ring);
}

/*
 * 测试7: 不同大小的 memcpy 性能对比
 */
static void bench_memcpy_sizes(void)
{
    printf("  测试7: 不同大小的 memcpy 性能对比\n");
    printf("  ┌─────────┬──────────┬──────────┬──────────┐\n");
    printf("  │  大小   │ 每次耗时  │ 吞吐量    │ 带宽     │\n");
    printf("  ├─────────┼──────────┼──────────┼──────────┤\n");

    int sizes[] = {64, 128, 256, 512, 1024, 2048, 4096, 8192};
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int s = 0; s < num_sizes; s++) {
        int size = sizes[s];
        uint64_t iterations = 10000000;
        uint64_t start, end;

        char *src = rte_malloc(NULL, size, 64);
        char *dst = rte_malloc(NULL, size, 64);

        if (!src || !dst) {
            printf("  │ %4d B  │  内存分配失败                  │\n", size);
            rte_free(src);
            rte_free(dst);
            continue;
        }

        memset(src, 0xAA, size);

        start = rte_get_timer_cycles();
        for (uint64_t i = 0; i < iterations; i++) {
            rte_memcpy(dst, src, size);
        }
        end = rte_get_timer_cycles();

        uint64_t elapsed = end - start;
        double per_op_ns = (double)elapsed * 1e9 / g_hz / iterations;
        double ops_per_sec = (double)iterations * g_hz / elapsed;
        double bandwidth_gbps = (double)iterations * size * 8 /
                                ((double)elapsed * 1e6 / g_hz) / 1000;

        printf("  │ %4d B  │ %6.1f ns │ %5.1f M/s │ %5.1f Gb │\n",
               size, per_op_ns, ops_per_sec / 1e6, bandwidth_gbps);

        rte_free(src);
        rte_free(dst);
    }

    printf("  └─────────┴──────────┴──────────┴──────────┘\n");
    printf("  │\n");
}

/*
 * 测试8: Mempool 分配和释放
 */
static void bench_mempool(void)
{
    struct benchmark_result result = {.name = "Mempool alloc/free"};
    uint64_t start, end;
    uint64_t iterations = 10000000;  /* 1000万次 */

    printf("  测试8: Mempool 对象分配和释放\n");
    printf("  迭代次数: %lu\n", iterations);

    struct rte_mempool *mp = rte_mempool_create(
        "bench_pool",
        8191,           /* n */
        64,             /* elt_size */
        256,            /* cache_size */
        0,              /* private_data_size */
        NULL, NULL,     /* mp_init, mp_init_arg */
        NULL, NULL,     /* obj_init, obj_init_arg */
        rte_socket_id(),
        0);

    if (mp == NULL) {
        printf("  ├─ 创建 Mempool 失败,跳过测试\n");
        return;
    }

    void *obj;

    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        if (rte_mempool_get(mp, &obj) == 0) {
            rte_mempool_put(mp, obj);
        }
    }
    end = rte_get_timer_cycles();

    calc_result(&result, start, end, iterations);
    print_result(&result);

    rte_mempool_free(mp);
}

/*
 * 测试9: 标准 memcpy vs rte_memcpy
 */
static void bench_memcpy_comparison(void)
{
    const int size = 1024;
    uint64_t iterations = 10000000;
    uint64_t start, end, elapsed;
    double per_op_ns;

    printf("  测试9: 标准 memcpy vs rte_memcpy 对比\n");
    printf("  大小: %d 字节\n", size);
    printf("  迭代次数: %lu\n", iterations);

    char *src = rte_malloc(NULL, size, 64);
    char *dst = rte_malloc(NULL, size, 64);

    if (!src || !dst) {
        printf("  内存分配失败,跳过测试\n");
        rte_free(src);
        rte_free(dst);
        return;
    }

    memset(src, 0xAA, size);

    /* 测试标准 memcpy */
    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        memcpy(dst, src, size);
    }
    end = rte_get_timer_cycles();
    elapsed = end - start;
    per_op_ns = (double)elapsed * 1e9 / g_hz / iterations;

    printf("  │   ├─ 标准 memcpy: %.2f ns/op\n", per_op_ns);

    /* 测试 rte_memcpy */
    start = rte_get_timer_cycles();
    for (uint64_t i = 0; i < iterations; i++) {
        rte_memcpy(dst, src, size);
    }
    end = rte_get_timer_cycles();
    elapsed = end - start;
    per_op_ns = (double)elapsed * 1e9 / g_hz / iterations;

    printf("  │   └─ rte_memcpy: %.2f ns/op\n", per_op_ns);
    printf("  │\n");

    rte_free(src);
    rte_free(dst);
}

/*
 * 打印性能测试提示
 */
static void print_benchmark_tips(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║              性能测试最佳实践                          ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("⚠️  获得准确测试结果的建议:\n");
    printf("\n");
    printf("  1. CPU 频率设置:\n");
    printf("     sudo sh -c 'echo performance > \\\n");
    printf("       /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'\n");
    printf("\n");
    printf("  2. 禁用 C-states (节能模式):\n");
    printf("     在 BIOS 中禁用 C-states\n");
    printf("\n");
    printf("  3. 隔离 CPU 核心:\n");
    printf("     启动参数添加: isolcpus=1-3\n");
    printf("\n");
    printf("  4. 多次运行取中位数\n");
    printf("     避免偶然的系统干扰\n");
    printf("\n");
    printf("  5. 预热代码和数据\n");
    printf("     第一次运行通常较慢(缓存冷启动)\n");
    printf("\n");
    printf("按 Enter 继续运行测试...");
    getchar();
}

/*
 * 主函数
 */
int main(int argc, char *argv[])
{
    int ret;

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化 EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    /* 获取时钟频率 */
    g_hz = rte_get_timer_hz();

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║              DPDK 性能测试工具 - Lesson 16               ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  CPU 频率: %-10.3f GHz                                 ║\n", g_hz / 1e9);
    printf("║  每周期: %-10.3f 纳秒                                   ║\n", 1e9 / g_hz);
    printf("║  Lcore ID: %-2u                                           ║\n", rte_lcore_id());
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    /* 打印提示 */
    print_benchmark_tips();

    printf("\n");
    printf("┌─ 开始性能测试...\n");
    printf("│\n");

    /* 运行所有测试 */
    if (!force_quit) {
        bench_empty_loop();
    }

    if (!force_quit) {
        bench_rdtsc();
    }

    if (!force_quit) {
        bench_malloc();
    }

    if (!force_quit) {
        bench_memcpy_fixed();
    }

    if (!force_quit) {
        bench_ring();
    }

    if (!force_quit) {
        bench_ring_bulk();
    }

    if (!force_quit) {
        bench_memcpy_sizes();
    }

    if (!force_quit) {
        bench_mempool();
    }

    if (!force_quit) {
        bench_memcpy_comparison();
    }

    printf("└─ 测试完成！\n\n");

    /* 总结 */
    printf("═══════════════════════════════════════════════════════════\n");
    printf("                         总结\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n关键发现:\n");
    printf("  • 空循环开销: ~0.4 纳秒/次\n");
    printf("  • rdtsc 调用: ~10-30 纳秒/次\n");
    printf("  • Ring 操作: ~10-50 纳秒/次\n");
    printf("  • Mempool 操作: ~20-60 纳秒/次\n");
    printf("  • 内存拷贝带宽: 取决于数据大小和缓存\n");

    printf("\n性能优化建议:\n");
    printf("  1. 使用批量操作(bulk)而非单个操作\n");
    printf("  2. 避免跨 NUMA 节点访问\n");
    printf("  3. 利用 Mempool 避免频繁的 malloc/free\n");
    printf("  4. 小数据用 rte_memcpy,大数据考虑 DMA\n");
    printf("  5. 预热代码和数据,确保在缓存中\n");

    /* 清理 */
    rte_eal_cleanup();

    printf("\n程序正常退出.\n");
    return 0;
}
