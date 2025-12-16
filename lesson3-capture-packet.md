

在dpdk程序中，是分为三个小组件

core：cpu核心

queue：网口有多个队列

port：网口

RTE_ETH_FOREACH_DEV 遍历当前端口列表的宏



# 一、EAL环境初始化

调用rte_eal_init函数

```
// 初始化 EAL(Environment Abstraction Layer)
ret = rte_eal_init(argc, argv);
```



获取可用网口数量

rte_eth_dev_count_avail()



# 二、创建mbuf内存池

目的：mbuf内存池用于接收数据包

```
struct rte_mempool *rte_pktmbuf_pool_create(
    const char *name,         // 内存池名称
    unsigned n,              // mbuf 数量
    unsigned cache_size,     // 每个核心的缓存大小
    uint16_t priv_size,     // 私有数据大小
    uint16_t data_room_size, // 数据缓冲区大小
    int socket_id           // NUMA socket ID
);
```

**name:** 内存池唯一标识符
**n:** 池中 mbuf 总数，通常为 NUM_MBUFS * 端口数
**cache_size:** 每个核心的本地缓存大小，建议值为 RTE_MEMPOOL_CACHE_MAX_SIZE 或 256
**priv_size:** 每个 mbuf 的私有数据大小，通常为 0
**data_room_size:** 每个 mbuf 的数据缓冲区大小，通常为 RTE_MBUF_DEFAULT_BUF_SIZE
**socket_id:** NUMA socket ID，通常使用 rte_socket_id()
**返回值：**成功返回内存池指针，失败返回 NULL



# 三、端口配置和初始化

```
int rte_eth_dev_configure(
    uint16_t port_id,        // 端口 ID
    uint16_t nb_rx_queue,    // 接收队列数量
    uint16_t nb_tx_queue,    // 发送队列数量
    const struct rte_eth_conf *eth_conf // 端口配置结构
);
```



端口配置结构示例：

```
struct rte_eth_conf port_conf = {
    .rxmode = {
        .max_rx_pkt_len = RTE_ETHER_MAX_LEN,    // 最大接收包长度
        .split_hdr_size = 0,                    // 头部分割大小
        .offloads = DEV_RX_OFFLOAD_CHECKSUM,    // 接收卸载功能
    },
    .txmode = {
        .mq_mode = ETH_MQ_TX_NONE,             // 多队列模式
        .offloads = DEV_TX_OFFLOAD_IPV4_CKSUM, // 发送卸载功能
    },
};
```



# 四、接收队列和发送队列设置

**接收队列设置：**

```
int rte_eth_rx_queue_setup(
    uint16_t port_id,        // 端口 ID
    uint16_t rx_queue_id,    // 接收队列 ID
    uint16_t nb_rx_desc,     // 接收描述符数量
    unsigned int socket_id,   // NUMA socket ID
    const struct rte_eth_rxconf *rx_conf, // 接收队列配置
    struct rte_mempool *mb_pool  // mbuf 池
);
```

`port_id`: 要配置的端口号

`rx_queue_id`: 接收队列号

`nb_rx_desc`: 接收环大小（描述符数量）

`socket_id`: 内存分配的 NUMA 节点

`rx_conf`: 接收队列配置

`mb_pool`: 之前创建的 mbuf 池



**发送队列设置：**

```
int rte_eth_tx_queue_setup(
    uint16_t port_id,        // 端口 ID
    uint16_t tx_queue_id,    // 发送队列 ID
    uint16_t nb_tx_desc,     // 发送描述符数量
    unsigned int socket_id,   // NUMA socket ID
    const struct rte_eth_txconf *tx_conf // 发送队列配置
);
```

`port_id`: 要配置的端口号

`tx_queue_id`: 发送队列号

`nb_tx_desc`: 发送环大小（描述符数量）

`socket_id`: 内存分配的 NUMA 节点

`tx_conf`: 发送队列配置



# 五、启动端口、开启混杂模式

```
int rte_eth_dev_start(uint16_t port_id);
```

开启混杂模式

```
void rte_eth_promiscuous_enable(uint16_t port_id);
```



两个函数都是传递port_id，一个端口就是0，以此类推。

# 六、运行数据包接收

```
// 将线程绑定到指定 CPU 核心
unsigned lcore_id;
RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    rte_eal_remote_launch(lcore_function, NULL, lcore_id);
}
```



```
// 接收数据包的主循环
static int lcore_function(void *arg) {
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
    
    while (!force_quit) {
        // 从接收队列接收数据包
        const uint16_t nb_rx = rte_eth_rx_burst(
            port_id,          // 端口 ID
            queue_id,        // 队列 ID
            pkts_burst,      // mbuf 数组
            MAX_PKT_BURST    // 最大接收包数
        );

        // 处理接收到的数据包
        for (uint16_t i = 0; i < nb_rx; i++) {
            // 处理数据包
            process_packet(pkts_burst[i]);
            
            // 发送数据包（如果需要）
            rte_eth_tx_burst(
                port_id,      // 端口 ID
                queue_id,    // 队列 ID
                &pkts_burst[i], // mbuf 指针
                1            // 发送包数
            );
        }
    }
    return 0;
}
```

下节课带大家一起看下mbuf的结构吧。



# 八、清理和退出

```
// 停止端口
rte_eth_dev_stop(port_id);

// 关闭端口
rte_eth_dev_close(port_id);

// 清理 EAL
rte_eal_cleanup();
```





# 完整的流程如下所示：

```
int main(int argc, char *argv[]) {
    // 1. 初始化 EAL
    ret = rte_eal_init(argc, argv);
    
    // 2. 创建内存池
    mbuf_pool = rte_pktmbuf_pool_create(...);
    
    // 3. 初始化所有端口
    RTE_ETH_FOREACH_DEV(port_id) {
        // 3.1 配置端口
        ret = rte_eth_dev_configure(...);
        
        // 3.2 设置每个接收队列
        for (q = 0; q < rx_rings; q++) {
            ret = rte_eth_rx_queue_setup(...);
        }
        
        // 3.3 设置每个发送队列（可选）
        for (q = 0; q < tx_rings; q++) {
            ret = rte_eth_tx_queue_setup(...);
        }
        
        // 3.4 启动端口
        ret = rte_eth_dev_start(port_id);
        
        // 3.5 启用混杂模式（可选）
        rte_eth_promiscuous_enable(port_id);
    }
    
    // 4. 启动工作线程
    rte_eal_mp_remote_launch(lcore_function, NULL, CALL_MAIN);
    
    // 5. 等待所有线程完成
    rte_eal_mp_wait_lcore();
    
    // 6. 清理
    cleanup();
    
    return 0;
}
```



# 遇到问题

在我的ubuntu 21.10实体机器里面使用dpdk的uio驱动绑定网卡，会遇到下面的错误

Error: IOMMU support is disabled, use --noiommu-mode for binding in noiommu mode