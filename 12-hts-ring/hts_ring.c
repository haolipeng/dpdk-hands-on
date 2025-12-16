/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_malloc.h>

#define RING_SIZE 1024
#define TEST_COUNT 1000000

/* 测试消息结构 */
struct test_msg {
    uint64_t sequence;
    uint64_t timestamp;
    uint32_t priority;  /* 用于Peek API测试 */
} __rte_cache_aligned;

static volatile int g_stop = 0;

static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n[Signal] Received signal, stopping...\n");
        g_stop = 1;
    }
}

/* 测试1：HTS性能测试 */
static void test_hts_performance(void)
{
    struct rte_ring *hts_ring;
    void *objs[32];
    uint64_t start, end, hz;
    unsigned int i;

    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║   Test 1: HTS Mode Performance       ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    /* 创建HTS Ring */
    hts_ring = rte_ring_create("hts_ring", RING_SIZE, rte_socket_id(),
                               RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);
    if (!hts_ring) {
        printf("Failed to create HTS ring\n");
        return;
    }

    printf("✓ Created HTS ring (size=%u)\n", RING_SIZE);

    /* 准备测试数据 */
    for (i = 0; i < 32; i++) {
        objs[i] = (void *)(uintptr_t)(i + 1);
    }

    /* 性能测试 */
    hz = rte_get_tsc_hz();
    start = rte_get_tsc_cycles();

    for (i = 0; i < TEST_COUNT / 32; i++) {
        rte_ring_enqueue_burst(hts_ring, objs, 32, NULL);
        rte_ring_dequeue_burst(hts_ring, objs, 32, NULL);
    }

    end = rte_get_tsc_cycles();

    double elapsed = (double)(end - start) / hz;
    double mpps = TEST_COUNT / elapsed / 1000000.0;

    printf("\nPerformance:\n");
    printf("  Operations: %d\n", TEST_COUNT);
    printf("  Time: %.3f seconds\n", elapsed);
    printf("  Throughput: %.2f Mpps\n", mpps);

    rte_ring_free(hts_ring);
}

/* 测试2：HTS vs MP/MC性能对比 */
static void test_hts_vs_mpmc(void)
{
    struct rte_ring *hts_ring, *mpmc_ring;
    void *objs[32];
    uint64_t start, end, hz;
    unsigned int i;
    double hts_time, mpmc_time, hts_mpps, mpmc_mpps;

    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║   Test 2: HTS vs MP/MC Comparison    ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    /* 创建两种Ring */
    hts_ring = rte_ring_create("hts_cmp", RING_SIZE, rte_socket_id(),
                               RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);
    mpmc_ring = rte_ring_create("mpmc_cmp", RING_SIZE, rte_socket_id(), 0);

    if (!hts_ring || !mpmc_ring) {
        printf("Failed to create rings\n");
        return;
    }

    printf("✓ Created HTS and MP/MC rings\n\n");

    /* 准备测试数据 */
    for (i = 0; i < 32; i++) {
        objs[i] = (void *)(uintptr_t)(i + 1);
    }

    hz = rte_get_tsc_hz();

    /* 测试HTS */
    printf("Testing HTS mode...\n");
    start = rte_get_tsc_cycles();
    for (i = 0; i < TEST_COUNT / 32; i++) {
        rte_ring_enqueue_burst(hts_ring, objs, 32, NULL);
        rte_ring_dequeue_burst(hts_ring, objs, 32, NULL);
    }
    end = rte_get_tsc_cycles();
    hts_time = (double)(end - start) / hz;
    hts_mpps = TEST_COUNT / hts_time / 1000000.0;

    /* 测试MP/MC */
    printf("Testing MP/MC mode...\n");
    start = rte_get_tsc_cycles();
    for (i = 0; i < TEST_COUNT / 32; i++) {
        rte_ring_enqueue_burst(mpmc_ring, objs, 32, NULL);
        rte_ring_dequeue_burst(mpmc_ring, objs, 32, NULL);
    }
    end = rte_get_tsc_cycles();
    mpmc_time = (double)(end - start) / hz;
    mpmc_mpps = TEST_COUNT / mpmc_time / 1000000.0;

    /* 结果对比 */
    printf("\n┌────────────┬──────────┬──────────────┐\n");
    printf("│   Mode     │   Mpps   │  Relative    │\n");
    printf("├────────────┼──────────┼──────────────┤\n");
    printf("│   HTS      │  %6.2f  │    %5.1f%%    │\n",
           hts_mpps, 100.0);
    printf("│   MP/MC    │  %6.2f  │    %5.1f%%    │\n",
           mpmc_mpps, (mpmc_mpps / hts_mpps) * 100);
    printf("└────────────┴──────────┴──────────────┘\n");

    if (hts_mpps < mpmc_mpps) {
        printf("\n💡 HTS is %.1f%% slower (normal on physical machines)\n",
               ((mpmc_mpps - hts_mpps) / mpmc_mpps) * 100);
    } else {
        printf("\n💡 HTS is %.1f%% faster (good for VM/container)\n",
               ((hts_mpps - mpmc_mpps) / mpmc_mpps) * 100);
    }

    rte_ring_free(hts_ring);
    rte_ring_free(mpmc_ring);
}

/* 测试3：Peek API（HTS独有功能） */
static void test_peek_api(void)
{
    struct rte_ring *hts_ring;
    struct test_msg *messages[20];
    struct test_msg *msg;
    unsigned int i, ret;
    unsigned int peeked = 0, accepted = 0, rejected = 0;

    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║   Test 3: Peek API (HTS Only)        ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    /* 创建HTS Ring */
    hts_ring = rte_ring_create("peek_ring", 256, rte_socket_id(),
                               RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);
    if (!hts_ring) {
        printf("Failed to create HTS ring\n");
        return;
    }

    printf("✓ Created HTS ring for Peek API test\n\n");

    /* 准备测试消息（不同优先级） */
    for (i = 0; i < 20; i++) {
        messages[i] = rte_zmalloc(NULL, sizeof(struct test_msg), 0);
        if (!messages[i]) {
            printf("Failed to allocate message\n");
            goto cleanup;
        }
        messages[i]->sequence = i;
        messages[i]->timestamp = rte_get_tsc_cycles();
        messages[i]->priority = (i % 3);  /* 优先级: 0=高, 1=中, 2=低 */
    }

    /* 入队所有消息 */
    ret = rte_ring_enqueue_bulk(hts_ring, (void **)messages, 20, NULL);
    printf("✓ Enqueued %u messages with different priorities\n\n", ret);

    printf("Using Peek API to filter messages (only accept priority 0 and 1):\n");
    printf("──────────────────────────────────────────────────────────\n");

    /* 使用Peek API条件式出队 */
    while (!rte_ring_empty(hts_ring)) {
        /* 阶段1：Peek - 查看队头元素 */
        ret = rte_ring_dequeue_bulk_start(hts_ring, (void **)&msg, 1, NULL);
        if (ret == 0)
            break;

        peeked++;

        /* 阶段2：根据优先级决定是否取出 */
        if (msg->priority <= 1) {
            /* 接受：高优先级和中优先级 */
            printf("  [Peek #%u] Seq=%lu, Priority=%u → ✓ Accept\n",
                   peeked, msg->sequence, msg->priority);
            rte_ring_dequeue_finish(hts_ring, 1);
            accepted++;
            rte_free(msg);
        } else {
            /* 拒绝：低优先级 */
            printf("  [Peek #%u] Seq=%lu, Priority=%u → ✗ Reject (stop)\n",
                   peeked, msg->sequence, msg->priority);
            rte_ring_dequeue_finish(hts_ring, 0);
            rejected++;
            break;  /* 遇到低优先级就停止处理 */
        }
    }

    printf("──────────────────────────────────────────────────────────\n");
    printf("\nPeek API Results:\n");
    printf("  Peeked:   %u messages\n", peeked);
    printf("  Accepted: %u messages (priority 0-1)\n", accepted);
    printf("  Rejected: %u messages (priority 2)\n", rejected);
    printf("  Remaining in ring: %u messages\n", rte_ring_count(hts_ring));

    printf("\n💡 Peek API allows conditional dequeue:\n");
    printf("   - Look at the message first\n");
    printf("   - Decide whether to take it or leave it\n");
    printf("   - Only supported by HTS and SP/SC modes\n");

cleanup:
    /* 清理剩余消息 */
    while (rte_ring_dequeue(hts_ring, (void **)&msg) == 0) {
        rte_free(msg);
    }

    rte_ring_free(hts_ring);
}

/* 测试4：多线程HTS场景（简单示例） */
static int worker_thread(void *arg)
{
    struct rte_ring *ring = (struct rte_ring *)arg;
    unsigned int lcore_id = rte_lcore_id();
    unsigned int count = 0;
    void *obj = (void *)(uintptr_t)lcore_id;

    printf("  [Lcore %u] Worker started\n", lcore_id);

    /* 每个线程尝试入队和出队 */
    for (int i = 0; i < 100 && !g_stop; i++) {
        if (rte_ring_enqueue(ring, obj) == 0) {
            count++;
        }

        if (rte_ring_dequeue(ring, &obj) == 0) {
            /* Successfully dequeued */
        }

        rte_pause();
    }

    printf("  [Lcore %u] Worker finished (enqueued %u)\n", lcore_id, count);
    return 0;
}

static void test_multithread_hts(void)
{
    struct rte_ring *hts_ring;
    unsigned int lcore_id;
    unsigned int worker_count = 0;

    printf("\n╔═══════════════════════════════════════╗\n");
    printf("║   Test 4: Multi-thread HTS Test      ║\n");
    printf("╚═══════════════════════════════════════╝\n\n");

    if (rte_lcore_count() < 2) {
        printf("⚠ Need at least 2 lcores for this test (use -l 0-1)\n");
        return;
    }

    /* 创建HTS Ring */
    hts_ring = rte_ring_create("mt_hts", 512, rte_socket_id(),
                               RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ);
    if (!hts_ring) {
        printf("Failed to create HTS ring\n");
        return;
    }

    printf("✓ Created HTS ring for multi-thread test\n");
    printf("  Available lcores: %u\n\n", rte_lcore_count());

    /* 在worker核心上启动线程 */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        rte_eal_remote_launch(worker_thread, hts_ring, lcore_id);
        worker_count++;
    }

    /* 等待所有worker完成 */
    rte_eal_mp_wait_lcore();

    printf("\n✓ All %u workers completed\n", worker_count);
    printf("  Final ring count: %u\n", rte_ring_count(hts_ring));

    rte_ring_free(hts_ring);
}

int main(int argc, char **argv)
{
    int ret;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

    printf("\n");
    printf("╔════════════════════════════════════════════════╗\n");
    printf("║   DPDK Ring HTS Mode Demo                     ║\n");
    printf("║   (Head-Tail Sync Mode)                       ║\n");
    printf("╚════════════════════════════════════════════════╝\n");

    /* 运行所有测试 */
    test_hts_performance();

    if (!g_stop)
        test_hts_vs_mpmc();

    if (!g_stop)
        test_peek_api();

    if (!g_stop)
        test_multithread_hts();

    printf("\n");
    printf("╔════════════════════════════════════════════════╗\n");
    printf("║   All Tests Completed                          ║\n");
    printf("╚════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Key Takeaways:\n");
    printf("  1. HTS is 10-20%% slower than MP/MC on physical machines\n");
    printf("  2. HTS is faster in VM/container environments (overcommit)\n");
    printf("  3. Peek API is unique to HTS and SP/SC modes\n");
    printf("  4. HTS provides more predictable latency\n");
    printf("\n");

    rte_eal_cleanup();

    return 0;
}
