# 一、介绍

官网文档的阅读

https://doc.dpdk.org/guides-19.05/prog_guide/multi_proc_support.html

注册接收消息

发送消息

发送请求

接收和回复消息

其他考虑



相关的api

rte_mp_disable

rte_mp_action_register

rte_mp_action_unregister

rte_mp_sendmsg

rte_mp_request_sync

rte_mp_request_async

rte_mp_reply



相关结构体

```
struct rte_mp_msg {
    char name[RTE_MP_MAX_NAME_LEN]; //用来指明消息的类型
    int len_param; //指明param的长度
    int num_fds; //指明fds的长度
    uint8_t param[RTE_MP_MAX_PARAM_LEN]; //用来存放消息的相关参数
    int fds[RTE_MP_MAX_FD_NUM]; //用于进程间传递fd
};
```



```
struct rte_mp_reply {
    int nb_sent; //发送出去的消息的个数
    int nb_received; //收到回复的消息的个数
    struct rte_mp_msg *msgs; /* caller to free */ //用于存放收到回复的消息
};
```

消息的类型

```
//消息的四种类型
enum mp_type {
    MP_MSG, /* Share message with peers, will not block */
    MP_REQ, /* Request for information, Will block for a reply */
    MP_REP, /* Response to previously-received request */
    MP_IGN, /* Response telling requester to ignore this response */
};
```



编写demo用例

1、查看



参考链接：

**DPDK : 进程间通信以及在内存管理的应用**

https://zhuanlan.zhihu.com/p/429896550



**DPDK多进程的通信**

https://blog.csdn.net/sinat_38816924/article/details/135005438



这个多进程的通信，在工程实践中，是不是走这块，我还真不是很清楚。



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

