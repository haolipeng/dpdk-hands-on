#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_launch.h>
#include <rte_malloc.h>

#define RING_SIZE 1024
#define BATCH_SIZE 32
#define TOTAL_MESSAGES (10)  // 10 messages

/* 消息结构 */
struct message {
    uint64_t seq_num;
    char payload[52];
} __rte_cache_aligned;

/* 全局变量 */
static struct rte_ring *g_ring;
static volatile int g_stop = 0;

/* 统计信息 */
struct stats {
    uint64_t messages_sent;
    uint64_t messages_received;
} __rte_cache_aligned;

static struct stats producer_stats;
static struct stats consumer_stats;

/* 信号处理 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\nSignal received, stopping...\n");
        g_stop = 1;
    }
}

/* 生产者线程 */
static int producer_thread(__rte_unused void *arg)
{
    uint64_t seq_num = 0;
    unsigned int lcore_id = rte_lcore_id();

    printf("[Producer] Starting on lcore %u\n", lcore_id);

    while (!g_stop && seq_num < TOTAL_MESSAGES) {
        struct message *msg = rte_malloc(NULL, sizeof(struct message), 0);
        int ret;

        msg->seq_num = seq_num++;
        snprintf(msg->payload, sizeof(msg->payload), "Message %lu", msg->seq_num);

        ret = rte_ring_sp_enqueue(g_ring, msg);
        if (ret < 0) {
            rte_free(msg);
            break;
        }

        producer_stats.messages_sent++;
    }

    printf("[Producer] Finished: sent %lu messages\n",
           producer_stats.messages_sent);

    return 0;
}

/* 消费者线程 */
static int consumer_thread(__rte_unused void *arg)
{
    unsigned int lcore_id = rte_lcore_id();

    printf("[Consumer] Starting on lcore %u\n", lcore_id);

    for (;;) {
        if (g_stop)
		{
			printf("Ring is empty and stop is set, breaking\n");
			break;
		}

        struct message *msg = NULL;
        int ret = rte_ring_sc_dequeue(g_ring, (void **)&msg);

        if (ret == -ENOENT) {
            rte_pause();
            continue;
        }
        if (ret < 0 || msg == NULL)
            continue;

        /* 打印接收的信息并释放资源*/
		printf("Consumer received message: %s\n", msg->payload);
        rte_free(msg);

        consumer_stats.messages_received++;
    }

    printf("[Consumer] Finished: received %lu messages\n",
           consumer_stats.messages_received);

    return 0;
}

/* 打印统计信息 */
static void print_stats(void)
{
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║   SP/SC Performance Statistics         ║\n");
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Producer: %lu messages                 ║\n", producer_stats.messages_sent);
    printf("╠════════════════════════════════════════╣\n");
    printf("║ Consumer: %lu messages                 ║\n", consumer_stats.messages_received);
    printf("╚════════════════════════════════════════╝\n\n");
	rte_delay_us_sleep(1000000);
}

int main(int argc, char **argv)
{
    int ret;
    unsigned int producer_lcore, consumer_lcore;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    if (rte_lcore_count() < 2) {
        printf("Error: Need at least 2 lcores\n");
        return -1;
    }

    /* 创建SP/SC Ring */
    g_ring = rte_ring_create("spsc_ring", RING_SIZE, rte_socket_id(),
                             RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (g_ring == NULL) {
        printf("Failed to create ring\n");
        return -1;
    }

    printf("✓ Created SP/SC ring (capacity: %u)\n", rte_ring_get_capacity(g_ring));

    /* 分配lcores */
    producer_lcore = rte_get_next_lcore(-1, 1, 0);
    consumer_lcore = rte_get_next_lcore(producer_lcore, 1, 0);

    printf("  Producer on lcore %u\n", producer_lcore);
    printf("  Consumer on lcore %u\n\n", consumer_lcore);

    /* 启动线程 */
    rte_eal_remote_launch(producer_thread, NULL, producer_lcore);
    rte_eal_remote_launch(consumer_thread, NULL, consumer_lcore);

    /* 等待完成 */
    rte_eal_mp_wait_lcore();

    /* 打印统计 */
    print_stats();

    rte_ring_free(g_ring);
    rte_eal_cleanup();

    return 0;
}