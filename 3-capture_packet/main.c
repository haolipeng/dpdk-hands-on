#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>

#define RX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

// 全局变量
static volatile bool force_quit = false;
static struct rte_mempool *mbuf_pool = NULL;

// 统计信息
static uint64_t total_packets = 0;
static uint64_t total_bytes = 0;

// 信号处理函数
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = true;
    }
}

// 简化的端口初始化函数 (仅RX)
static int port_init_rx_only(uint16_t port, struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_conf = {
        .rxmode = {
            .mtu = RTE_ETHER_MAX_LEN - RTE_ETHER_HDR_LEN - RTE_ETHER_CRC_LEN,
        },
    };
    
    int retval;
    uint16_t nb_rxd = RX_RING_SIZE;
    struct rte_eth_dev_info dev_info;

    // 检查端口是否有效
    if (!rte_eth_dev_is_valid_port(port))
        return -1;

    // 获取设备信息
    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) {
        printf("Error getting device info for port %u: %s\n",
                port, strerror(-retval));
        return retval;
    }

    // 配置设备：1个RX队列，0个TX队列
    retval = rte_eth_dev_configure(port, 1, 0, &port_conf);
    if (retval != 0) {
        printf("Error configuring port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    // 调整RX描述符数量
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, NULL);
    if (retval != 0) {
        printf("Error adjusting descriptors for port %u: %s\n", 
               port, strerror(-retval));
        return retval;
    }

    // 设置RX队列
    retval = rte_eth_rx_queue_setup(port, 0, nb_rxd,
            rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0) {
        printf("Error setting up RX queue for port %u: %s\n", 
               port, strerror(-retval));
        return retval;
    }

    // 启动设备
    retval = rte_eth_dev_start(port);
    if (retval < 0) {
        printf("Error starting port %u: %s\n", port, strerror(-retval));
        return retval;
    }

    // 获取并显示MAC地址
    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0) {
        printf("Error getting MAC address for port %u: %s\n", 
               port, strerror(-retval));
        return retval;
    }

    printf("Port %u MAC: %02"PRIx8":%02"PRIx8":%02"PRIx8
           ":%02"PRIx8":%02"PRIx8":%02"PRIx8"\n",
            port,
            addr.addr_bytes[0], addr.addr_bytes[1],
            addr.addr_bytes[2], addr.addr_bytes[3],
            addr.addr_bytes[4], addr.addr_bytes[5]);

    // 启用混杂模式以接收所有数据包
    retval = rte_eth_promiscuous_enable(port);
    if (retval != 0) {
        printf("Error enabling promiscuous mode for port %u: %s\n", 
               port, strerror(-retval));
        return retval;
    }

    printf("Port %u initialized successfully (RX only)\n", port);
    return 0;
}

// 简化的数据包处理函数
static void process_packet(struct rte_mbuf *pkt)
{
    struct rte_ether_hdr *eth_hdr;
    uint16_t ether_type;
    
    // 获取以太网头
    eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
    ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
    
    // 简单打印包信息
    printf("Packet: len=%u, type=0x%04x", pkt->pkt_len, ether_type);
    
    // 可选：解析IPv4
    if (ether_type == RTE_ETHER_TYPE_IPV4) {
        struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(pkt, 
            struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
        
        printf(", IPv4: %d.%d.%d.%d -> %d.%d.%d.%d",
               (ipv4_hdr->src_addr >> 0) & 0xFF,
               (ipv4_hdr->src_addr >> 8) & 0xFF,
               (ipv4_hdr->src_addr >> 16) & 0xFF,
               (ipv4_hdr->src_addr >> 24) & 0xFF,
               (ipv4_hdr->dst_addr >> 0) & 0xFF,
               (ipv4_hdr->dst_addr >> 8) & 0xFF,
               (ipv4_hdr->dst_addr >> 16) & 0xFF,
               (ipv4_hdr->dst_addr >> 24) & 0xFF);
    }
    
    printf("\n");
    
    // 更新统计
    total_packets++;
    total_bytes += pkt->pkt_len;
}

// 主抓包循环
static void capture_loop(void)
{
    uint16_t port;
    
    printf("\nStarting packet capture on %u ports. [Ctrl+C to quit]\n", 
           rte_eth_dev_count_avail());
    
    while (!force_quit) {
        // 遍历所有端口
        RTE_ETH_FOREACH_DEV(port) {
            struct rte_mbuf *bufs[BURST_SIZE];
            
            // 批量接收数据包
            const uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);
            
            if (likely(nb_rx > 0)) {
                for (uint16_t i = 0; i < nb_rx; i++) {
                    // 处理每个数据包
                    process_packet(bufs[i]);
                    rte_pktmbuf_free(bufs[i]);  // 释放mbuf
                }
            }
        }
    }
}

// 打印最终统计
static void print_final_stats(void)
{
    printf("\n=== Final Statistics ===\n");
    printf("Total packets captured: %"PRIu64"\n", total_packets);
    printf("Total bytes captured: %"PRIu64"\n", total_bytes);
    if (total_packets > 0) {
        printf("Average packet size: %.2f bytes\n", 
               (double)total_bytes / total_packets);
    }
    printf("========================\n");
}

// 主函数
int main(int argc, char *argv[])
{
    int ret;
    uint16_t nb_ports;
    uint16_t portid;
    
    // 1. 初始化EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 2. 检查可用端口
    nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports available\n");
    
    printf("Found %u Ethernet ports\n", nb_ports);
    
    // 3. 创建内存池
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", 
        NUM_MBUFS * nb_ports, MBUF_CACHE_SIZE, 0, 
        RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
    
    // 4. 初始化所有端口 (仅RX)
    RTE_ETH_FOREACH_DEV(portid) {
        if (port_init_rx_only(portid, mbuf_pool) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16"\n", portid);
    }
    
    // 5. 开始抓包
    capture_loop();
    
    // 6. 清理工作
    printf("\nShutting down...\n");
    
    RTE_ETH_FOREACH_DEV(portid) {
        printf("Closing port %u...", portid);
        rte_eth_dev_stop(portid);
        rte_eth_dev_close(portid);
        printf(" Done\n");
    }
    
    // 打印统计信息
    print_final_stats();
    
    // 7. 清理EAL
    rte_eal_cleanup();
    
    return 0;
}