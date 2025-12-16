/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <stdint.h>
#include <signal.h>

#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_ring.h>

#define RING_NAME              "mpmc_ring"
#define RING_SIZE              2048
#define NUM_PRODUCERS          2
#define NUM_CONSUMERS          2
#define MESSAGES_PER_PRODUCER  20
#define PRODUCER_START_CORE    1
#define CONSUMER_START_CORE    3

struct message {
    uint16_t producer_id;
    uint16_t consumer_id;
    char     payload[32];
} __rte_cache_aligned;

static struct rte_ring *g_ring;
static volatile uint32_t g_finished_producers;
static volatile int g_stop;
static uint64_t g_total_produced; //记录总共生产了多少条消息
static uint64_t g_total_consumed; //记录总共消费了多少条消息

static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nSignal received, stopping...\n");
        g_stop = 1;
    }
}

static int producer_thread(void *arg)
{
    int producer_id = *(int *)arg;
    uint32_t seq = 0;

    //打印从哪个lcore上启动的producer线程
    printf("[Producer %d] start on lcore %u\n",
           producer_id, rte_lcore_id());

    while (!g_stop && seq < MESSAGES_PER_PRODUCER) {
        //为msg消息申请内存空间
        struct message *msg = rte_zmalloc(NULL, sizeof(*msg), 0);
        int ret;

        if (msg == NULL) {
            printf("[Producer %d] allocation failed\n", producer_id);
            break;
        }

        //为msg字段赋值
        msg->producer_id = producer_id;
        snprintf(msg->payload, sizeof(msg->payload),
                 "P%d-%u", producer_id, seq);

        do {
            //入队操作，如无可用空间，则rte_pause()等待一会重试
            ret = rte_ring_mp_enqueue(g_ring, msg);
            if (ret == -ENOBUFS) {
                rte_pause();
            }
        } while (ret == -ENOBUFS && !g_stop);

        if (ret < 0) {
            rte_free(msg);
            break;
        }

        seq++;
        __atomic_add_fetch(&g_total_produced, 1, __ATOMIC_RELAXED);
    }

    //如果所有生产者都完成了，则设置停止标志
    if (__atomic_add_fetch(&g_finished_producers, 1, __ATOMIC_RELAXED) ==
        NUM_PRODUCERS) {
        g_stop = 1;
    }

    printf("[Producer %d] finished, sent %u messages\n", producer_id, seq);
    return 0;
}

static int consumer_thread(void *arg)
{
    int consumer_id = *(int *)arg;

    printf("[Consumer %d] start on lcore %u\n",
           consumer_id, rte_lcore_id());

    for (;;) {
        struct message *msg = NULL;
        int ret;

        if (g_stop && rte_ring_empty(g_ring))
            break;

        ret = rte_ring_mc_dequeue(g_ring, (void **)&msg);
        if (ret == -ENOENT) {
            rte_pause();
            continue;
        }

        if (ret < 0 || msg == NULL)
            continue;

        msg->consumer_id = consumer_id;
        //记录消费了多少条消息
        __atomic_add_fetch(&g_total_consumed, 1, __ATOMIC_RELAXED);

        rte_free(msg);
    }

    printf("[Consumer %d] finished\n", consumer_id);
    return 0;
}

int main(int argc, char **argv)
{
    static int producer_ids[NUM_PRODUCERS];
    static int consumer_ids[NUM_CONSUMERS];
    int ret;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    //初始化EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Cannot init EAL\n");

    if (rte_lcore_count() < NUM_PRODUCERS + NUM_CONSUMERS + 1) {
        rte_exit(EXIT_FAILURE,
                 "Need at least %d lcores\n",
                 NUM_PRODUCERS + NUM_CONSUMERS + 1);
    }

    //创建MP/MC类型的Ring
    g_ring = rte_ring_create(RING_NAME, RING_SIZE,
                             rte_socket_id(), 0);
    if (g_ring == NULL)
        rte_exit(EXIT_FAILURE, "Failed to create ring\n");

    printf("Ring created: capacity=%u, producers=%d, consumers=%d\n\n",
           rte_ring_get_capacity(g_ring), NUM_PRODUCERS, NUM_CONSUMERS);

    // Producer: 核心 1, 2
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_ids[i] = i;
        rte_eal_remote_launch(producer_thread, &producer_ids[i], PRODUCER_START_CORE + i);
    }

    // Consumer: 核心 3, 4
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        consumer_ids[i] = i;
        rte_eal_remote_launch(consumer_thread, &consumer_ids[i], CONSUMER_START_CORE + i);
    }

    rte_eal_mp_wait_lcore();

    printf("\nSummary:\n");
    printf("  Produced: %lu messages\n", g_total_produced);
    printf("  Consumed: %lu messages\n", g_total_consumed);
    printf("  %s\n",(g_total_produced == g_total_consumed) ? "Result: OK" : "Result: mismatch!");

    rte_ring_free(g_ring);
    rte_eal_cleanup();

    return 0;
}