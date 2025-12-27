/*
 * DPDK Eventdev 基础示例
 *
 * 本示例展示如何使用 DPDK Eventdev 框架构建事件驱动应用
 *
 * 架构:
 *   Producer (lcore 1) -> Eventdev -> Workers (lcore 2-N)
 *
 * 编译运行:
 *   sudo ./bin/eventdev_demo -l 0-3 --vdev=event_sw0 -- -w 2
 *
 * 参数说明:
 *   -w : worker 线程数量（默认 2）
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>

#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_eventdev.h>
#include <rte_event_eth_rx_adapter.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_cycles.h>
#include <rte_atomic.h>

#define MAX_EVENTS 4096
#define EVENT_QUEUE_DEPTH 1024
#define NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 250
#define MAX_WORKERS 16
#define BURST_SIZE 32

/* 应用配置 */
struct app_config {
    uint8_t eventdev_id;
    uint8_t num_workers;
    uint8_t producer_lcore;
    uint8_t worker_lcores[MAX_WORKERS];
} __rte_cache_aligned;

static struct app_config app_cfg;
static volatile bool force_quit = false;
static struct rte_mempool *mbuf_pool = NULL;

/* 统计信息 */
struct stats {
    uint64_t events_produced;
    uint64_t events_consumed;
    uint64_t events_dropped;
} __rte_cache_aligned;

static struct stats producer_stats;
static struct stats worker_stats[MAX_WORKERS];

/* 事件类型 */
enum event_type {
    EVENT_TYPE_NORMAL = 0,
    EVENT_TYPE_CONTROL = 1,
};

/* 信号处理 */
static void
signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = true;
    }
}

/*
 * ============================================================================
 * 事件生产者线程
 * ============================================================================
 */
static int
producer_thread(void *arg)
{
    uint8_t dev_id = app_cfg.eventdev_id;
    uint8_t port_id = 0;  /* Producer 使用 port 0 */
    struct rte_event events[BURST_SIZE];
    struct rte_mbuf *mbufs[BURST_SIZE];
    uint16_t nb_enqueued;
    uint32_t event_count = 0;
    unsigned lcore_id = rte_lcore_id();

    (void)arg;

    printf("Producer thread started on lcore %u\n", lcore_id);

    while (!force_quit) {
        /* 从内存池分配 mbuf */
        if (rte_pktmbuf_alloc_bulk(mbuf_pool, mbufs, BURST_SIZE) != 0) {
            producer_stats.events_dropped += BURST_SIZE;
            rte_pause();
            continue;
        }

        /* 构造事件 */
        for (int i = 0; i < BURST_SIZE; i++) {
            /* 填充 mbuf 数据（序列号） */
            uint32_t *data = rte_pktmbuf_mtod(mbufs[i], uint32_t *);
            *data = event_count++;
            mbufs[i]->data_len = sizeof(uint32_t);
            mbufs[i]->pkt_len = sizeof(uint32_t);

            /* 构造事件 */
            events[i].event = 0;
            events[i].queue_id = 0;  /* 发送到队列 0 */
            events[i].op = RTE_EVENT_OP_NEW;
            events[i].sched_type = RTE_SCHED_TYPE_ATOMIC;  /* Atomic 调度 */
            events[i].event_type = EVENT_TYPE_NORMAL;
            events[i].sub_event_type = 0;
            events[i].priority = RTE_EVENT_DEV_PRIORITY_NORMAL;
            events[i].mbuf = mbufs[i];
        }

        /* 入队事件 */
        nb_enqueued = rte_event_enqueue_burst(dev_id, port_id, events,
                                              BURST_SIZE);

        if (nb_enqueued < BURST_SIZE) {
            /* 释放未入队的 mbuf */
            for (int i = nb_enqueued; i < BURST_SIZE; i++) {
                rte_pktmbuf_free(mbufs[i]);
            }
            producer_stats.events_dropped += (BURST_SIZE - nb_enqueued);
        }

        producer_stats.events_produced += nb_enqueued;

        /* 限速 - 避免生成过快 */
        rte_delay_us(100);
    }

    printf("Producer thread on lcore %u exiting...\n", lcore_id);
    return 0;
}

/*
 * ============================================================================
 * 事件消费者（Worker）线程
 * ============================================================================
 */
static int
worker_thread(void *arg)
{
    uint8_t dev_id = app_cfg.eventdev_id;
    uint8_t port_id = *((uint8_t *)arg);
    struct rte_event events[BURST_SIZE];
    uint16_t nb_dequeued;
    unsigned lcore_id = rte_lcore_id();
    int worker_id = port_id - 1;  /* Worker ID (port_id 从 1 开始) */

    printf("Worker %d thread started on lcore %u (port %u)\n",
           worker_id, lcore_id, port_id);

    while (!force_quit) {
        /* 从 eventdev 出队事件 */
        nb_dequeued = rte_event_dequeue_burst(dev_id, port_id, events,
                                              BURST_SIZE, 0);

        if (nb_dequeued == 0) {
            rte_pause();
            continue;
        }

        /* 处理事件 */
        for (int i = 0; i < nb_dequeued; i++) {
            struct rte_event *ev = &events[i];

            /* 验证事件类型 */
            if (ev->event_type == EVENT_TYPE_NORMAL) {
                struct rte_mbuf *m = ev->mbuf;
                uint32_t *data = rte_pktmbuf_mtod(m, uint32_t *);

                /* 模拟处理工作 */
                (void)data;  /* 实际应用中会处理数据 */

                /* 释放 mbuf */
                rte_pktmbuf_free(m);

                worker_stats[worker_id].events_consumed++;
            }
        }
    }

    printf("Worker %d thread on lcore %u exiting...\n", worker_id, lcore_id);
    return 0;
}

/*
 * ============================================================================
 * 统计信息显示
 * ============================================================================
 */
static void
print_stats(void)
{
    uint64_t total_consumed = 0;

    printf("\n");
    printf("============================================\n");
    printf("       Eventdev Statistics\n");
    printf("============================================\n");
    printf("Producer:\n");
    printf("  Events Produced : %"PRIu64"\n", producer_stats.events_produced);
    printf("  Events Dropped  : %"PRIu64"\n", producer_stats.events_dropped);
    printf("\n");

    for (int i = 0; i < app_cfg.num_workers; i++) {
        printf("Worker %d:\n", i);
        printf("  Events Consumed : %"PRIu64"\n", worker_stats[i].events_consumed);
        total_consumed += worker_stats[i].events_consumed;
    }

    printf("\n");
    printf("Total Consumed    : %"PRIu64"\n", total_consumed);
    printf("============================================\n");
}

/*
 * ============================================================================
 * Eventdev 配置
 * ============================================================================
 */
static int
setup_eventdev(void)
{
    uint8_t dev_id = app_cfg.eventdev_id;
    struct rte_event_dev_config dev_conf = {0};
    struct rte_event_queue_conf queue_conf = {0};
    struct rte_event_port_conf port_conf = {0};
    int ret;

    /* 获取 eventdev 信息 */
    struct rte_event_dev_info dev_info;
    ret = rte_event_dev_info_get(dev_id, &dev_info);
    if (ret < 0) {
        printf("Failed to get event dev info\n");
        return -1;
    }

    printf("Event device %d info:\n", dev_id);
    printf("  Max event queues   : %u\n", dev_info.max_event_queues);
    printf("  Max event ports    : %u\n", dev_info.max_event_ports);
    printf("  Max events         : %d\n", dev_info.max_num_events);

    /* 配置 eventdev */
    dev_conf.nb_events_limit = MAX_EVENTS;
    dev_conf.nb_event_queues = 1;  /* 使用 1 个队列 */
    dev_conf.nb_event_ports = 1 + app_cfg.num_workers;  /* 1 producer + N workers */
    dev_conf.nb_event_queue_flows = 1024;
    dev_conf.nb_event_port_dequeue_depth = 32;
    dev_conf.nb_event_port_enqueue_depth = 32;

    ret = rte_event_dev_configure(dev_id, &dev_conf);
    if (ret < 0) {
        printf("Failed to configure event dev: %s\n", rte_strerror(-ret));
        return -1;
    }

    /* 配置事件队列 */
    queue_conf.nb_atomic_flows = 1024;
    queue_conf.nb_atomic_order_sequences = 1024;
    queue_conf.schedule_type = RTE_SCHED_TYPE_ATOMIC;
    queue_conf.priority = RTE_EVENT_DEV_PRIORITY_NORMAL;

    ret = rte_event_queue_setup(dev_id, 0, &queue_conf);
    if (ret < 0) {
        printf("Failed to setup event queue: %s\n", rte_strerror(-ret));
        return -1;
    }

    /* 配置事件端口 */
    port_conf.dequeue_depth = 32;
    port_conf.enqueue_depth = 32;
    port_conf.new_event_threshold = MAX_EVENTS;

    /* 配置 producer port (port 0) */
    ret = rte_event_port_setup(dev_id, 0, &port_conf);
    if (ret < 0) {
        printf("Failed to setup producer port: %s\n", rte_strerror(-ret));
        return -1;
    }

    /* 配置 worker ports (port 1-N) */
    for (int i = 0; i < app_cfg.num_workers; i++) {
        ret = rte_event_port_setup(dev_id, i + 1, &port_conf);
        if (ret < 0) {
            printf("Failed to setup worker port %d: %s\n", i,
                   rte_strerror(-ret));
            return -1;
        }
    }

    /* 将队列链接到所有端口 */
    for (int port_id = 0; port_id < (1 + app_cfg.num_workers); port_id++) {
        ret = rte_event_port_link(dev_id, port_id, NULL, NULL, 0);
        if (ret < 0) {
            printf("Failed to link port %d: %s\n", port_id,
                   rte_strerror(-ret));
            return -1;
        }
    }

    /* 启动 eventdev */
    ret = rte_event_dev_start(dev_id);
    if (ret < 0) {
        printf("Failed to start event dev: %s\n", rte_strerror(-ret));
        return -1;
    }

    printf("Eventdev %d configured successfully\n", dev_id);
    printf("  Event queues : 1\n");
    printf("  Event ports  : %d (1 producer + %d workers)\n",
           1 + app_cfg.num_workers, app_cfg.num_workers);

    return 0;
}

/*
 * ============================================================================
 * 命令行参数解析
 * ============================================================================
 */
static void
print_usage(const char *prgname)
{
    printf("Usage: %s [EAL options] -- [APP options]\n", prgname);
    printf("APP options:\n");
    printf("  -w NUM : Number of worker threads (default 2)\n");
}

static int
parse_args(int argc, char **argv)
{
    int opt;
    char *prgname = argv[0];

    /* 默认配置 */
    app_cfg.num_workers = 2;

    while ((opt = getopt(argc, argv, "w:h")) != -1) {
        switch (opt) {
        case 'w':
            app_cfg.num_workers = atoi(optarg);
            if (app_cfg.num_workers == 0 || app_cfg.num_workers > MAX_WORKERS) {
                printf("Invalid number of workers: %s\n", optarg);
                return -1;
            }
            break;
        case 'h':
            print_usage(prgname);
            return -1;
        default:
            print_usage(prgname);
            return -1;
        }
    }

    return 0;
}

/*
 * ============================================================================
 * Main Function
 * ============================================================================
 */
int
main(int argc, char **argv)
{
    int ret;
    unsigned lcore_id;
    unsigned nb_lcores;
    int worker_id = 0;

    /* 初始化统计信息 */
    memset(&producer_stats, 0, sizeof(producer_stats));
    memset(worker_stats, 0, sizeof(worker_stats));
    memset(&app_cfg, 0, sizeof(app_cfg));

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化 EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    argc -= ret;
    argv += ret;

    /* 解析应用参数 */
    ret = parse_args(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid arguments\n");

    /* 检查 lcore 数量 */
    nb_lcores = rte_lcore_count();
    if (nb_lcores < (2 + app_cfg.num_workers)) {
        rte_panic("Insufficient lcores. Required: %d (1 main + 1 producer + %d workers)\n",
                  2 + app_cfg.num_workers, app_cfg.num_workers);
    }

    /* 检查 eventdev 数量 */
    if (rte_event_dev_count() == 0) {
        rte_panic("No event devices found. Please use --vdev=event_sw0\n");
    }

    app_cfg.eventdev_id = 0;

    /* 创建内存池 */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
                                        MBUF_CACHE_SIZE, 0,
                                        RTE_MBUF_DEFAULT_BUF_SIZE,
                                        rte_socket_id());
    if (mbuf_pool == NULL)
        rte_panic("Cannot create mbuf pool\n");

    printf("Mbuf pool created: %u mbufs\n", NUM_MBUFS);

    /* 配置 eventdev */
    ret = setup_eventdev();
    if (ret < 0)
        rte_panic("Failed to setup eventdev\n");

    /* 分配 lcore 角色 */
    lcore_id = rte_get_next_lcore(-1, 1, 0);  /* 跳过 main lcore */
    app_cfg.producer_lcore = lcore_id;

    for (int i = 0; i < app_cfg.num_workers; i++) {
        lcore_id = rte_get_next_lcore(lcore_id, 1, 0);
        app_cfg.worker_lcores[i] = lcore_id;
    }

    /* 启动 producer 线程 */
    printf("\nLaunching producer on lcore %u\n", app_cfg.producer_lcore);
    rte_eal_remote_launch(producer_thread, NULL, app_cfg.producer_lcore);

    /* 启动 worker 线程 */
    for (int i = 0; i < app_cfg.num_workers; i++) {
        static uint8_t port_ids[MAX_WORKERS];
        port_ids[i] = i + 1;  /* Worker port ID 从 1 开始 */

        printf("Launching worker %d on lcore %u\n", i, app_cfg.worker_lcores[i]);
        rte_eal_remote_launch(worker_thread, &port_ids[i],
                             app_cfg.worker_lcores[i]);
    }

    /* 主线程定期打印统计信息 */
    printf("\nPress Ctrl+C to stop...\n");
    while (!force_quit) {
        sleep(2);
        print_stats();
    }

    /* 等待所有线程退出 */
    rte_eal_mp_wait_lcore();

    /* 打印最终统计信息 */
    print_stats();

    /* 停止 eventdev */
    rte_event_dev_stop(app_cfg.eventdev_id);

    /* 清理资源 */
    rte_mempool_free(mbuf_pool);

    /* 清理 EAL */
    rte_eal_cleanup();

    printf("\nApplication exited successfully\n");
    return 0;
}
