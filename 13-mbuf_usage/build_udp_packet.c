/**
 * build_udp_packet.c - 构造 UDP 数据包并保存为 pcap 文件
 *
 * 本程序演示如何从零构建一个完整的 UDP 数据包：
 * 1. 添加 Payload (应用层数据)
 * 2. 添加 UDP 头 (传输层)
 * 3. 添加 IPv4 头 (网络层)
 * 4. 添加以太网头 (链路层)
 * 5. 使用 libpcap 将数据包保存到 pcap 文件
 *
 * 封装顺序：从内到外，使用 prepend 添加各层协议头
 *
 * 编译: make build_udp_packet
 * 运行: sudo ./bin/build_udp_packet -l 0 --no-pci
 * 查看: tcpdump -r udp_packet.pcap -XX
 *       wireshark udp_packet.pcap
 */

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <pcap/pcap.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

/* pcap 文件输出路径 */
#define PCAP_OUTPUT_FILE "udp_packet.pcap"

/* 打印 MAC 地址 */
static void print_mac(const char *label, uint8_t *mac)
{
    printf("  %s: %02x:%02x:%02x:%02x:%02x:%02x\n",
           label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* 打印 IPv4 地址 */
static void print_ipv4(const char *label, uint32_t ip)
{
    uint8_t *bytes = (uint8_t *)&ip;
    printf("  %s: %u.%u.%u.%u\n", label,
           bytes[0], bytes[1], bytes[2], bytes[3]);
}

/**
 * 将 mbuf 数据写入 pcap 文件
 *
 * @param mbuf   要保存的数据包
 * @param filename  输出文件名
 * @return 0 成功，-1 失败
 */
static int save_packet_to_pcap(struct rte_mbuf *mbuf, const char *filename)
{
    pcap_t *pcap_handle;
    pcap_dumper_t *pcap_dumper;
    struct pcap_pkthdr pcap_hdr;

    /*
     * 创建一个 "dead" pcap 句柄，用于写入文件
     * DLT_EN10MB 表示以太网链路层类型
     * 65535 是快照长度（最大捕获长度）
     */
    pcap_handle = pcap_open_dead(DLT_EN10MB, 65535);
    if (pcap_handle == NULL) {
        printf("ERROR: pcap_open_dead failed\n");
        return -1;
    }

    /* 打开 pcap 文件进行写入 */
    pcap_dumper = pcap_dump_open(pcap_handle, filename);
    if (pcap_dumper == NULL) {
        printf("ERROR: pcap_dump_open failed: %s\n", pcap_geterr(pcap_handle));
        pcap_close(pcap_handle);
        return -1;
    }

    /* 填充 pcap 包头 */
    gettimeofday(&pcap_hdr.ts, NULL);    /* 当前时间戳 */
    pcap_hdr.caplen = mbuf->data_len;    /* 实际捕获的长度 */
    pcap_hdr.len = mbuf->data_len;       /* 数据包原始长度 */

    /* 获取 mbuf 数据指针并写入 pcap 文件 */
    uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);
    pcap_dump((u_char *)pcap_dumper, &pcap_hdr, pkt_data);

    /* 关闭文件和句柄 */
    pcap_dump_close(pcap_dumper);
    pcap_close(pcap_handle);

    return 0;
}

int main(int argc, char *argv[])
{
    struct rte_mempool *pool;
    struct rte_mbuf *mbuf;
    int ret;

    /* 初始化 */
    ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        printf("EAL init failed\n");
        return -1;
    }

    printf("\n");
    printf("========================================\n");
    printf("   Build UDP Packet & Save to PCAP\n");
    printf("========================================\n\n");

    /* 创建内存池 */
    pool = rte_pktmbuf_pool_create("UDP_PKT_POOL", 1024, 256, 0,
                                    RTE_MBUF_DEFAULT_BUF_SIZE,
                                    rte_socket_id());
    if (pool == NULL) {
        printf("Create pool failed\n");
        rte_eal_cleanup();
        return -1;
    }

    mbuf = rte_pktmbuf_alloc(pool);
    if (mbuf == NULL) {
        printf("Alloc mbuf failed\n");
        rte_eal_cleanup();
        return -1;
    }

    /* 准备参数 */
    const char *payload_data = "Hello UDP! This is a DPDK mbuf demo packet.";
    size_t payload_len = strlen(payload_data) + 1;

    uint8_t src_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t dst_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint32_t src_ip = 0xC0A80101;  /* 192.168.1.1 */
    uint32_t dst_ip = 0xC0A80102;  /* 192.168.1.2 */
    uint16_t src_port = 12345;
    uint16_t dst_port = 80;

    /*
     * 封装过程：从内到外
     *
     *  Step 1: [Payload]
     *  Step 2: [UDP Header][Payload]
     *  Step 3: [IP Header][UDP Header][Payload]
     *  Step 4: [Eth Header][IP Header][UDP Header][Payload]
     */

    /* ===== Step 1: 添加 Payload ===== */
    printf("[Step 1] Add Payload (Application Layer)\n");
    char *payload = (char *)rte_pktmbuf_append(mbuf, payload_len);
    if (payload == NULL) {
        printf("Append payload failed\n");
        goto cleanup;
    }
    strcpy(payload, payload_data);
    printf("  Payload: %zu bytes, data_len now: %u\n\n", payload_len, mbuf->data_len);

    /* ===== Step 2: 添加 UDP 头 ===== */
    printf("[Step 2] Add UDP Header (Transport Layer)\n");
    struct rte_udp_hdr *udp;
    udp = (struct rte_udp_hdr *)rte_pktmbuf_prepend(mbuf, sizeof(*udp));
    if (udp == NULL) {
        printf("Prepend UDP header failed\n");
        goto cleanup;
    }

    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->dgram_len = htons(sizeof(*udp) + payload_len);
    udp->dgram_cksum = 0;  /* 简化：不计算校验和 */

    printf("  UDP header: %zu bytes\n", sizeof(*udp));
    printf("  UDP length field: %u\n", ntohs(udp->dgram_len));
    printf("  data_len now: %u\n\n", mbuf->data_len);

    /* ===== Step 3: 添加 IPv4 头 ===== */
    printf("[Step 3] Add IPv4 Header (Network Layer)\n");
    struct rte_ipv4_hdr *ip;
    ip = (struct rte_ipv4_hdr *)rte_pktmbuf_prepend(mbuf, sizeof(*ip));
    if (ip == NULL) {
        printf("Prepend IP header failed\n");
        goto cleanup;
    }

    ip->version_ihl = 0x45;              /* IPv4, 5*4=20 bytes header */
    ip->type_of_service = 0;
    ip->total_length = htons(mbuf->data_len);
    ip->packet_id = htons(1);
    ip->fragment_offset = 0;
    ip->time_to_live = 64;
    ip->next_proto_id = IPPROTO_UDP;     /* 17 */
    ip->hdr_checksum = 0;                /* 简化：不计算校验和 */
    ip->src_addr = htonl(src_ip);
    ip->dst_addr = htonl(dst_ip);

    printf("  IP header: %zu bytes\n", sizeof(*ip));
    printf("  IP total length: %u\n", ntohs(ip->total_length));
    printf("  data_len now: %u\n\n", mbuf->data_len);

    /* ===== Step 4: 添加以太网头 ===== */
    printf("[Step 4] Add Ethernet Header (Link Layer)\n");
    struct rte_ether_hdr *eth;
    eth = (struct rte_ether_hdr *)rte_pktmbuf_prepend(mbuf, sizeof(*eth));
    if (eth == NULL) {
        printf("Prepend Ethernet header failed\n");
        goto cleanup;
    }

    memcpy(eth->src_addr.addr_bytes, src_mac, 6);
    memcpy(eth->dst_addr.addr_bytes, dst_mac, 6);
    eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);  /* 0x0800 */

    printf("  Ethernet header: %zu bytes\n", sizeof(*eth));
    printf("  EtherType: 0x%04x (IPv4)\n", RTE_ETHER_TYPE_IPV4);
    printf("  data_len now: %u\n\n", mbuf->data_len);

    /* ===== Step 5: 保存到 pcap 文件 ===== */
    printf("\n");
    printf("========================================\n");
    printf("        Save to PCAP File\n");
    printf("========================================\n");
    printf("\n");

    printf("Packet Parameters:\n");
    printf("-----------------------------------------\n");
    print_mac("Src MAC", src_mac);
    print_mac("Dst MAC", dst_mac);
    print_ipv4("Src IP", htonl(src_ip));
    print_ipv4("Dst IP", htonl(dst_ip));
    printf("  Src Port: %u\n", src_port);
    printf("  Dst Port: %u\n", dst_port);
    printf("  Payload: \"%s\" (%zu bytes)\n\n", payload_data, payload_len);
    ret = save_packet_to_pcap(mbuf, PCAP_OUTPUT_FILE);
    if (ret == 0) {
        printf("  [OK] Packet saved to: %s\n\n", PCAP_OUTPUT_FILE);
    } else {
        printf("  [FAILED] Could not save packet to pcap file\n");
    }

    printf("\n");

cleanup:
    rte_pktmbuf_free(mbuf);
    rte_eal_cleanup();

    return 0;
}
