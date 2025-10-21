## 七、进阶主题

### 7.1 进程间控制消息（rte_mp）

除了Ring队列传递数据包，DPDK还提供了`rte_mp`机制用于控制消息：

```c
// Server注册消息处理函数
static int handle_client_request(const struct rte_mp_msg *msg,
                                  const void *peer)
{
    printf("Received request from client\n");

    // 处理请求
    struct rte_mp_msg reply;
    strlcpy(reply.name, "server_reply", sizeof(reply.name));
    reply.len_param = snprintf(reply.param, sizeof(reply.param),
                               "Stats: RX=%lu", server_rx_pkts);

    // 发送回复
    return rte_mp_reply(&reply, peer);
}

// 注册
rte_mp_action_register("client_request", handle_client_request);

// Client发送请求
struct rte_mp_msg msg;
struct rte_mp_reply reply;

strlcpy(msg.name, "client_request", sizeof(msg.name));
msg.len_param = 0;

// 同步请求
ret = rte_mp_request_sync(&msg, &reply, NULL);
if (ret == 0) {
    printf("Server reply: %s\n", reply.msgs[0].param);
}
```

### 7.2 Multi-Producer/Multi-Consumer场景

如果多个进程同时写入Ring，需要使用MP模式：

```c
// 创建支持多生产者的Ring
ring = rte_ring_create("mp_ring", RING_SIZE, socket_id,
                       RING_F_MP_ENQ | RING_F_MC_DEQ);

// 多个进程可以并发入队，DPDK内部使用CAS保证原子性
```

**性能对比：**

| 模式  | 性能 | 适用场景               |
| ----- | ---- | ---------------------- |
| SP-SC | 最高 | 单一生产者和消费者     |
| MP-SC | 中等 | 多个生产者，单一消费者 |
| SP-MC | 中等 | 单一生产者，多个消费者 |
| MP-MC | 较低 | 多个生产者和消费者     |

### 7.3 NUMA感知优化

```c
// 查询端口所在的NUMA节点
int socket_id = rte_eth_dev_socket_id(port_id);

// 在同一NUMA节点上分配内存
struct rte_mempool *pool = rte_pktmbuf_pool_create(
    "mbuf_pool", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
    RTE_MBUF_DEFAULT_BUF_SIZE,
    socket_id  // 关键：指定NUMA节点
);

// 将进程绑定到同一NUMA节点的CPU
// 启动命令：numactl --cpunodebind=0 --membind=0 ./app
```
