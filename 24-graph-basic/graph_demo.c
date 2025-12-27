/*
 * DPDK Graph Framework 基础示例
 *
 * 本示例展示如何使用 DPDK Graph Framework 构建包处理流水线
 *
 * Graph 拓扑结构:
 *   source_node -> process_node -> sink_node
 *
 * 编译运行:
 *   sudo ./bin/graph_demo -l 0-1 --no-pci
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>

#include <rte_eal.h>
#include <rte_lcore.h>
#include <rte_graph.h>
#include <rte_graph_worker.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_cycles.h>

#define NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define MAX_PATTERN_NUM 4

static volatile bool force_quit = false;
static struct rte_mempool *mbuf_pool = NULL;

/* 统计信息 */
struct node_stats {
    uint64_t pkts_processed;
    uint64_t pkts_dropped;
} __rte_cache_aligned;

static struct node_stats source_stats;
static struct node_stats process_stats;
static struct node_stats sink_stats;

/* 信号处理 */
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = true;
    }
}

/*
 * ============================================================================
 * Source Node - 生成测试数据包
 * ============================================================================
 */
static uint16_t
source_node_process(struct rte_graph *graph, struct rte_node *node,
                    void **objs, uint16_t nb_objs)
{
    struct rte_mbuf *mbufs[BURST_SIZE];
    uint16_t count = 0;
    uint16_t i;

    (void)objs;  /* 未使用 */
    (void)nb_objs;  /* 未使用 */

    /* 如果收到退出信号，停止生成新数据包 */
    if (force_quit)
        return 0;

    /* 从内存池分配 mbuf */
    if (rte_pktmbuf_alloc_bulk(mbuf_pool, mbufs, BURST_SIZE) != 0) {
        return 0;
    }

    /* 模拟数据包内容 - 设置简单的序列号 */
    for (i = 0; i < BURST_SIZE; i++) {
        uint32_t *data = rte_pktmbuf_mtod(mbufs[i], uint32_t *);
        *data = source_stats.pkts_processed + i;
        mbufs[i]->data_len = sizeof(uint32_t);
        mbufs[i]->pkt_len = sizeof(uint32_t);
    }

    /* 将数据包传递到下一个节点 */
    rte_node_enqueue(graph, node, 0, (void **)mbufs, BURST_SIZE);

    source_stats.pkts_processed += BURST_SIZE;
    count = BURST_SIZE;

    return count;
}

/* Source Node 注册 */
static struct rte_node_register source_node = {
    .name = "source_node",
    .process = source_node_process,
    .nb_edges = 1,
    .next_nodes = {
        [0] = "process_node",
    },
};

RTE_NODE_REGISTER(source_node);

/*
 * ============================================================================
 * Process Node - 处理数据包（过滤偶数包）
 * ============================================================================
 */
static uint16_t
process_node_process(struct rte_graph *graph, struct rte_node *node,
                     void **objs, uint16_t nb_objs)
{
    struct rte_mbuf *mbufs[BURST_SIZE];
    uint16_t valid_count = 0;
    uint16_t i;

    /* 处理接收到的数据包 */
    for (i = 0; i < nb_objs; i++) {
        struct rte_mbuf *mbuf = (struct rte_mbuf *)objs[i];
        uint32_t *data = rte_pktmbuf_mtod(mbuf, uint32_t *);

        /* 只保留奇数序列号的包，丢弃偶数序列号的包 */
        if (*data % 2 == 1) {
            mbufs[valid_count++] = mbuf;
            process_stats.pkts_processed++;
        } else {
            rte_pktmbuf_free(mbuf);
            process_stats.pkts_dropped++;
        }
    }

    /* 将有效的数据包传递到下一个节点 */
    if (valid_count > 0) {
        rte_node_enqueue(graph, node, 0, (void **)mbufs, valid_count);
    }

    return nb_objs;
}

/* Process Node 注册 */
static struct rte_node_register process_node = {
    .name = "process_node",
    .process = process_node_process,
    .nb_edges = 1,
    .next_nodes = {
        [0] = "sink_node",
    },
};

RTE_NODE_REGISTER(process_node);

/*
 * ============================================================================
 * Sink Node - 接收最终数据包
 * ============================================================================
 */
static uint16_t
sink_node_process(struct rte_graph *graph, struct rte_node *node,
                  void **objs, uint16_t nb_objs)
{
    uint16_t i;

    (void)graph;
    (void)node;

    /* 处理接收到的数据包 */
    for (i = 0; i < nb_objs; i++) {
        struct rte_mbuf *mbuf = (struct rte_mbuf *)objs[i];

        /* 在实际应用中，这里可以发送数据包或做其他处理 */
        sink_stats.pkts_processed++;

        /* 释放 mbuf */
        rte_pktmbuf_free(mbuf);
    }

    return nb_objs;
}

/* Sink Node 注册 */
static struct rte_node_register sink_node = {
    .name = "sink_node",
    .process = sink_node_process,
    .nb_edges = 0,  /* 没有下一个节点 */
};

RTE_NODE_REGISTER(sink_node);

/*
 * ============================================================================
 * Graph Worker Thread
 * ============================================================================
 */
static int
graph_main_loop(void *arg)
{
    struct rte_graph *graph = arg;
    unsigned int lcore_id = rte_lcore_id();

    printf("Graph worker started on lcore %u\n", lcore_id);

    /* 主循环 - 驱动 Graph 执行 */
    while (!force_quit) {
        rte_graph_walk(graph);
    }

    printf("Graph worker on lcore %u exiting...\n", lcore_id);
    return 0;
}

/*
 * ============================================================================
 * Statistics Display
 * ============================================================================
 */
static void
print_stats(void)
{
    printf("\n");
    printf("============================================\n");
    printf("          Graph Statistics\n");
    printf("============================================\n");
    printf("Source Node:\n");
    printf("  Packets Generated : %"PRIu64"\n", source_stats.pkts_processed);
    printf("\n");
    printf("Process Node:\n");
    printf("  Packets Processed : %"PRIu64"\n", process_stats.pkts_processed);
    printf("  Packets Dropped   : %"PRIu64"\n", process_stats.pkts_dropped);
    printf("\n");
    printf("Sink Node:\n");
    printf("  Packets Received  : %"PRIu64"\n", sink_stats.pkts_processed);
    printf("============================================\n");
}

/*
 * ============================================================================
 * Main Function
 * ============================================================================
 */
int
main(int argc, char **argv)
{
    struct rte_graph *graph;
    rte_graph_t graph_id;
    int ret;
    unsigned int lcore_id;
    uint16_t nb_patterns;

    /* 初始化统计信息 */
    memset(&source_stats, 0, sizeof(source_stats));
    memset(&process_stats, 0, sizeof(process_stats));
    memset(&sink_stats, 0, sizeof(sink_stats));

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化 EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    argc -= ret;
    argv += ret;

    /* 检查 lcore 数量 */
    if (rte_lcore_count() < 2) {
        rte_panic("This application requires at least 2 lcores\n"
                  "Usage: %s -l 0-1 --no-pci\n", argv[0]);
    }

    /* 创建内存池 */
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
                                        MBUF_CACHE_SIZE, 0,
                                        RTE_MBUF_DEFAULT_BUF_SIZE,
                                        rte_socket_id());
    if (mbuf_pool == NULL)
        rte_panic("Cannot create mbuf pool\n");

    printf("Mbuf pool created: %u mbufs\n", NUM_MBUFS);

    /*
     * 创建 Graph
     *
     * Graph 模式使用字符串数组定义节点连接关系
     * 格式: "node_name-next_node_name-next_node_name"
     */
    const char *patterns[] = {
        "source_node-process_node-sink_node",
    };
    nb_patterns = RTE_DIM(patterns);

    /* 创建 Graph 实例 */
    struct rte_graph_param graph_conf = {
        .socket_id = rte_socket_id(),
        .nb_node_patterns = nb_patterns,
        .node_patterns = patterns,
    };

    graph_id = rte_graph_create("demo_graph", &graph_conf);
    if (graph_id == RTE_GRAPH_ID_INVALID)
        rte_panic("Failed to create graph: %s\n", rte_strerror(rte_errno));

    printf("Graph created successfully: %s\n", rte_graph_id_to_name(graph_id));

    /* 获取 graph 对象指针 */
    graph = rte_graph_lookup("demo_graph");
    if (graph == NULL)
        rte_panic("Failed to lookup graph\n");

    /* 打印 Graph 拓扑信息 */
    printf("\nGraph topology:\n");
    rte_graph_dump(stdout, graph_id);

    /* 在第二个 lcore 上启动 Graph worker */
    lcore_id = rte_get_next_lcore(-1, 1, 0);
    if (lcore_id == RTE_MAX_LCORE)
        rte_panic("No worker lcore available\n");

    printf("\nLaunching graph worker on lcore %u\n", lcore_id);
    rte_eal_remote_launch(graph_main_loop, graph, lcore_id);

    /* 主线程定期打印统计信息 */
    printf("\nPress Ctrl+C to stop...\n\n");
    while (!force_quit) {
        sleep(2);
        print_stats();
    }

    /* 等待 worker 线程退出 */
    rte_eal_mp_wait_lcore();

    /* 打印最终统计信息 */
    print_stats();

    /* 清理资源 */
    rte_graph_destroy(graph_id);
    rte_mempool_free(mbuf_pool);

    /* 清理 EAL */
    rte_eal_cleanup();

    printf("\nApplication exited successfully\n");
    return 0;
}
