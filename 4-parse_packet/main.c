#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_cycles.h>
#include <rte_time.h>
#include <rte_ethdev.h>

#define RX_RING_SIZE 1024
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

// 全局变量
static volatile bool force_quit = false;
static struct rte_mempool *mbuf_pool = NULL;

// 时间戳相关变量
static uint64_t tsc_hz = 0; // TSC频率
static uint64_t tsc_base_time = 0;  // tsc基准时间（纳秒）
static uint64_t tsc_start = 0;  // 程序启动时的TSC值

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

// 初始化时间戳系统
static int init_timestamp_system(void)
{
    // 获取TSC频率
    tsc_hz = rte_get_tsc_hz();
    if (tsc_hz == 0) {
        printf("Error: Cannot get TSC frequency\n");
        return -1;
    }
    
    // 记录程序启动时的TSC值
    tsc_start = rte_rdtsc();
    
    // 设置基准时间，转化为纳秒值
    struct timespec base_ts;
    clock_gettime(CLOCK_REALTIME, &base_ts);
    tsc_base_time = (uint64_t)base_ts.tv_sec * 1000000000ULL + base_ts.tv_nsec;

    return 0;
}

// 高性能时间戳获取函数
static void get_packet_timestamp(uint64_t *tsc_cycles, uint64_t *wall_time_ns)
{
    // 获取TSC周期数（最高性能）
    *tsc_cycles = rte_rdtsc();
    
    // 检查TSC频率是否有效
    if (tsc_hz == 0) {
        printf("Warning: TSC frequency is 0, using system time\n");
        struct timespec time;
        clock_gettime(CLOCK_REALTIME, &time);
        *wall_time_ns = (uint64_t)time.tv_sec * 1000000000ULL + time.tv_nsec;
        return;
    }
    
    // 转换为纳秒时间戳
    // 计算从程序启动到现在的TSC差值，然后转换为时间差
    uint64_t tsc_elapsed = *tsc_cycles - tsc_start;
    
    // 使用更高精度的计算方法
    // 先计算秒数，再计算纳秒数，避免大数相乘溢出
    uint64_t elapsed_seconds = tsc_elapsed / tsc_hz;
    uint64_t elapsed_nanoseconds = ((tsc_elapsed % tsc_hz) * 1000000000ULL) / tsc_hz;
    
    *wall_time_ns = tsc_base_time + elapsed_seconds * 1000000000ULL + elapsed_nanoseconds;
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
    //1.从rte_mbuf结构中获取ethernet头
    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
    
    //1.1 获取ether_type
    uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
    printf("ether_type: %04x\n", ether_type);

    //1.2 获取mac address地址，并输出其值
    char buf[RTE_ETHER_ADDR_FMT_SIZE];
    rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &(eth_hdr->src_addr));
    printf("src_mac: %s\n", buf);

    //解析ipv4协议
    if(ether_type == RTE_ETHER_TYPE_IPV4)
    {
        //2.从rte_mbuf结构中获取ipv4头
        struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(pkt, struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));
        
        //2.1 获取源地址和目的地址
        uint32_t src_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);
        uint32_t dst_ip = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
        
        // 以点分十进制格式输出IP地址
        printf("IPv4: %d.%d.%d.%d -> %d.%d.%d.%d\n",
               (src_ip >> 24) & 0xFF,  // 最高字节
               (src_ip >> 16) & 0xFF,  // 第三字节
               (src_ip >> 8) & 0xFF,   // 第二字节
               (src_ip >> 0) & 0xFF,   // 最低字节
               (dst_ip >> 24) & 0xFF,  // 最高字节
               (dst_ip >> 16) & 0xFF,  // 第三字节
               (dst_ip >> 8) & 0xFF,   // 第二字节
               (dst_ip >> 0) & 0xFF);  // 最低字节

        //2.2 获取ip协议的版本号
        uint8_t version = ipv4_hdr->version_ihl >> 4;
        printf("version: %d\n", version);

        //2.3 获取ip协议的头长度
        uint8_t ihl = ipv4_hdr->version_ihl & 0x0F;
        printf("ihl: %d\n", ihl);

        //2.4 解析ip协议的其他字段
        //tos字段
        uint8_t type_of_service = ipv4_hdr->type_of_service;
        printf("type_of_service: %d\n", type_of_service);

        //total_length字段
        uint16_t total_length = rte_be_to_cpu_16(ipv4_hdr->total_length);
        printf("total_length: %d\n", total_length);

        //packet_id字段
        uint16_t packet_id = rte_be_to_cpu_16(ipv4_hdr->packet_id);
        printf("packet_id: %d\n", packet_id);

        //fragment_offset字段 (包含flags和offset)
        uint16_t fragment_offset_raw = rte_be_to_cpu_16(ipv4_hdr->fragment_offset);
        uint16_t flags = (fragment_offset_raw >> 13) & 0x7;  // 高3位是flags
        uint16_t fragment_offset = fragment_offset_raw & 0x1FFF;  // 低13位是offset
        printf("flags: 0x%x, fragment_offset: %d\n", flags, fragment_offset);
        
        //ttl字段
        uint8_t ttl = ipv4_hdr->time_to_live;
        printf("ttl: %d\n", ttl);

        //protocol字段
        uint8_t protocol = ipv4_hdr->next_proto_id;
        printf("protocol: %d\n", protocol);

        //checksum字段
        uint16_t checksum = rte_be_to_cpu_16(ipv4_hdr->hdr_checksum);
        printf("checksum: 0x%04x\n", checksum);

        if(protocol == IPPROTO_TCP)
        {
            //3.从rte_mbuf结构中获取tcp头
            printf("detect packet is tcp protocol!\n");
            uint8_t l2_len = sizeof(struct rte_ether_hdr);
            uint8_t l3_len = sizeof(struct rte_ipv4_hdr);   

            struct rte_tcp_hdr *tcp_hdr = rte_pktmbuf_mtod_offset(pkt, struct rte_tcp_hdr *, l2_len + l3_len);
            uint16_t src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
            uint16_t dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);

            uint32_t seq = rte_be_to_cpu_32(tcp_hdr->sent_seq);
            uint32_t ack = rte_be_to_cpu_32(tcp_hdr->recv_ack);

            uint8_t data_off = (tcp_hdr->data_off >> 4) & 0x0F;
            data_off *= 4;

            uint8_t tcp_flags = tcp_hdr->tcp_flags;
            
            uint16_t rx_win = rte_be_to_cpu_16(tcp_hdr->rx_win);
            uint16_t cksum = rte_be_to_cpu_16(tcp_hdr->cksum);
            uint16_t tcp_urp = rte_be_to_cpu_16(tcp_hdr->tcp_urp);

            printf("src_port: %d, dst_port: %d, seq: %d, ack: %d, data_off: %d, tcp_flags: %d, rx_win: %d, cksum: 0x%04X, tcp_urp: %d\n", src_port, dst_port, seq, ack, data_off, tcp_flags, rx_win, cksum, tcp_urp);
        }
    }
    
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
    
    // 初始化时间戳系统
    if (init_timestamp_system() != 0)
        rte_exit(EXIT_FAILURE, "Error initializing timestamp system\n");
    
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