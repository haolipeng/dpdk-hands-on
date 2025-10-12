# DPDK多进程开发教程（第7课）

## 课程概述

本课程将介绍DPDK多进程架构的核心概念和实践应用，通过学习DPDK官方的`client_server_mp`示例，掌握多进程通信和数据共享机制。

**课程时长：** 90分钟
**难度级别：** 中级
**前置知识：** 完成第1-6课，熟悉DPDK基础API、mempool、mbuf等概念

---

## 一、DPDK多进程基础理论

### 1.1 为什么需要多进程架构？

在DPDK应用开发中，多进程架构相比多线程具有以下优势：

| 特性 | 多进程 | 多线程 |
|------|--------|--------|
| **故障隔离** | ✅ 进程崩溃不影响其他进程 | ❌ 线程崩溃导致整个进程退出 |
| **模块化部署** | ✅ 可独立启停不同模块 | ❌ 线程强耦合 |
| **开发调试** | ✅ 可独立编译、调试各模块 | ⚠️ 调试相对复杂 |
| **性能开销** | ⚠️ 进程间通信有一定开销 | ✅ 共享地址空间，开销小 |
| **资源管理** | ✅ 操作系统级别隔离 | ⚠️ 需要应用层管理 |

**典型应用场景：**
- 流水线处理架构：收包、解析、转发分离
- 热插拔模块：在线更新处理逻辑而不中断数据平面

### 1.2 DPDK多进程的两种角色

#### Primary进程（主进程）
- **职责：**
  - 初始化EAL环境
  - 配置和管理网络端口
  - 创建共享内存对象（mempool、ring等）
  - 负责hugepage的分配和管理
- **启动参数：** `--proc-type=primary`（默认）
- **数量限制：** 一个系统中只能有一个Primary进程

#### Secondary进程（从进程）
- **职责：**
  - Attach到Primary进程创建的共享内存
  - 查找并使用已存在的mempool、ring等对象
  - 处理数据包或执行特定业务逻辑
  - 不能创建新的设备端口
- **启动参数：** `--proc-type=secondary` 或 `--proc-type=auto`
- **数量限制：** 可以有多个Secondary进程

### 1.3 共享内存机制

DPDK多进程架构的核心是**Hugepage共享内存**：

```
┌─────────────────────────────────────────────────┐
│              Hugepage Memory                     │
│  (映射到所有进程的相同虚拟地址空间)              │
├─────────────┬──────────────┬────────────────────┤
│  Mempool    │  Ring Queue  │  Packet Buffers    │
│  (mbuf池)   │  (进程间队列) │  (数据包内存)       │
└─────────────┴──────────────┴────────────────────┘
       ↑              ↑                ↑
       │              │                │
   ┌───┴──┐      ┌───┴──┐        ┌───┴──┐
   │Primary│      │Secondary1│    │Secondary2│
   └──────┘      └─────────┘    └─────────┘
```

**关键特性：**
- Primary进程负责创建共享对象
- Secondary进程通过名字查找对象
- 所有进程看到相同的内存布局
- 使用无锁数据结构（ring）实现高性能通信

---

## 二、官方示例：Client-Server架构

### 2.1 示例概述

DPDK官方提供的`client_server_mp`示例展示了一个经典的多进程架构：

**架构图：**
```
                    Network Ports
                         ↓
        ┌────────────────────────────────┐
        │      Server (Primary)          │
        │  - 接收网络数据包                │
        │  - 分发给Client进程             │
        └────────┬───────────────┬────────┘
                 │               │
         ┌───────┴──┐        ┌──┴────────┐
         │ Client 0 │        │ Client 1  │
         │(Secondary)│       │(Secondary) │
         │ - 处理包  │        │ - 处理包   │
         │ - L2转发  │        │ - L2转发   │
         └──────────┘        └───────────┘
                 │               │
                 └───────┬───────┘
                         ↓
                   Network Ports
```

### 2.2 目录结构

```
examples/multi_process/client_server_mp/
├── mp_server/          # Server进程源码
│   ├── main.c          # Server主程序
│   ├── init.c          # 初始化函数
│   └── args.c          # 参数解析
├── mp_client/          # Client进程源码
│   ├── client.c        # Client主程序
│   └── ...
├── shared/             # 共享代码
│   ├── common.h        # 共享数据结构和宏
│   └── ...
└── Makefile
```

### 2.3 工作流程

1. **Server进程启动流程：**
   ```
   初始化EAL
     ↓
   创建mempool
     ↓
   初始化网络端口（RX+TX队列）
     ↓
   为每个Client创建Ring队列
     ↓
   循环接收数据包
     ↓
   根据策略分发包到不同Client的Ring
   ```

2. **Client进程启动流程：**
   ```
   初始化EAL (--proc-type=secondary)
     ↓
   查找Server创建的mempool
     ↓
   查找属于自己的Ring队列
     ↓
   从Ring中取出数据包
     ↓
   处理并转发数据包
   ```

3. **数据流转：**
   ```
   Port 0 RX → Server → Ring[client_id] → Client → Port 1 TX
   ```

### 2.4 示例代码运行指南

#### 编译示例

```bash
# 进入DPDK源码目录
cd dpdk

# 使用meson编译
meson build
cd build
ninja

# 示例程序位置
# Server: build/examples/dpdk-mp_server
# Client: build/examples/dpdk-mp_client
```

#### 运行Server进程

```bash
# 启动Server（Primary进程）
sudo ./build/examples/dpdk-mp_server \
    -l 1-2 \              # 使用核心1-2
    -n 4 \                # 内存通道数
    -- \
    -p 0x3 \              # 端口掩码（使用端口0和1）
    -n 2                  # 支持2个Client进程
```

**参数说明：**
- `-l 1-2`: 使用CPU核心1和2
- `-p 0x3`: 端口掩码，二进制`11`表示使用端口0和1
- `-n 2`: 创建2个Client的Ring队列

#### 运行Client进程

**启动Client 0：**
```bash
sudo ./build/examples/dpdk-mp_client \
    -l 3 \                      # 使用核心3
    --proc-type=auto \          # 自动识别为Secondary
    -- \
    -n 0                        # Client ID为0
```

**启动Client 1：**
```bash
sudo ./build/examples/dpdk-mp_client \
    -l 4 \                      # 使用核心4
    --proc-type=auto \          # 自动识别为Secondary
    -- \
    -n 1                        # Client ID为1
```

#### 查看运行状态

Server输出示例：
```
EAL: Detected 8 lcore(s)
EAL: Probing VFIO support...
PORT 0: 00:11:22:33:44:55
PORT 1: 00:11:22:33:44:66

Creating ring 'MProc_Client_RX_0'
Creating ring 'MProc_Client_RX_1'

Client process 0 connected
Client process 1 connected

[Port 0] RX: 1000 packets, distributed to clients
```

Client输出示例：
```
Client process 0 starting...
Looking up mempool: mbuf_pool
Looking up ring: MProc_Client_RX_0

Received 500 packets from server
Forwarded 500 packets to port 1
```

---

## 三、核心API详解

### 3.1 进程类型相关API

#### 初始化时指定进程类型
```c
// 命令行参数
int main(int argc, char **argv)
{
    // Primary进程（默认）
    ret = rte_eal_init(argc, argv);

    // Secondary进程需要显式指定
    // 启动命令：./app -l 0-1 --proc-type=secondary -- ...
}
```

#### 查询当前进程类型
```c
enum rte_proc_type_t {
    RTE_PROC_AUTO = -1,     // 自动检测
    RTE_PROC_PRIMARY = 0,   // Primary进程
    RTE_PROC_SECONDARY,     // Secondary进程
    RTE_PROC_INVALID
};

enum rte_proc_type_t proc_type = rte_eal_process_type();
if (proc_type == RTE_PROC_PRIMARY) {
    // Primary进程的初始化逻辑
} else if (proc_type == RTE_PROC_SECONDARY) {
    // Secondary进程的初始化逻辑
}
```

### 3.2 Ring队列API（进程间通信核心）

#### Primary进程：创建Ring
```c
#define RING_SIZE 4096

struct rte_ring *ring;

// 创建共享Ring队列
ring = rte_ring_create(
    "client_ring_0",              // 名称（必须唯一）
    RING_SIZE,                    // 队列大小（必须是2的幂）
    rte_socket_id(),              // NUMA节点
    RING_F_SP_ENQ | RING_F_SC_DEQ // 标志位
);

if (ring == NULL) {
    rte_exit(EXIT_FAILURE, "Cannot create ring\n");
}
```

**Ring标志位说明：**
| 标志位 | 含义 | 使用场景 |
|--------|------|----------|
| `RING_F_SP_ENQ` | Single Producer（单生产者） | 只有一个线程/进程写入 |
| `RING_F_MP_ENQ` | Multi Producer（多生产者） | 多个线程/进程写入 |
| `RING_F_SC_DEQ` | Single Consumer（单消费者） | 只有一个线程/进程读取 |
| `RING_F_MC_DEQ` | Multi Consumer（多消费者） | 多个线程/进程读取 |

#### Secondary进程：查找Ring
```c
struct rte_ring *ring;

// 通过名称查找已存在的Ring
ring = rte_ring_lookup("client_ring_0");

if (ring == NULL) {
    rte_exit(EXIT_FAILURE, "Cannot find ring\n");
}
```

#### 发送数据到Ring（Server → Client）
```c
struct rte_mbuf *tx_bufs[BURST_SIZE];
unsigned int nb_tx;

// 批量入队（返回实际入队的数量）
nb_tx = rte_ring_enqueue_burst(
    ring,                  // Ring指针
    (void **)tx_bufs,      // mbuf指针数组
    nb_pkts,               // 要发送的包数量
    NULL                   // 可选：返回剩余空间
);

// 处理入队失败的包
if (unlikely(nb_tx < nb_pkts)) {
    for (i = nb_tx; i < nb_pkts; i++) {
        rte_pktmbuf_free(tx_bufs[i]);  // 释放未发送的包
    }
}
```

#### 从Ring接收数据（Client）
```c
struct rte_mbuf *rx_bufs[BURST_SIZE];
unsigned int nb_rx;

// 批量出队
nb_rx = rte_ring_dequeue_burst(
    ring,                  // Ring指针
    (void **)rx_bufs,      // 接收mbuf的数组
    BURST_SIZE,            // 最多接收的包数量
    NULL                   // 可选：返回剩余数量
);

// 处理接收到的包
for (i = 0; i < nb_rx; i++) {
    process_packet(rx_bufs[i]);
}
```

### 3.3 Mempool共享

#### Primary进程：创建Mempool
```c
struct rte_mempool *pktmbuf_pool;

pktmbuf_pool = rte_pktmbuf_pool_create(
    "mbuf_pool",           // 名称
    NUM_MBUFS,             // mbuf数量
    MBUF_CACHE_SIZE,       // 每个核心的缓存大小
    0,                     // priv_size
    RTE_MBUF_DEFAULT_BUF_SIZE,  // mbuf数据区大小
    rte_socket_id()        // NUMA节点
);
```

#### Secondary进程：查找Mempool
```c
struct rte_mempool *pktmbuf_pool;

// 通过名称查找
pktmbuf_pool = rte_mempool_lookup("mbuf_pool");

if (pktmbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "Cannot find mbuf pool\n");
}
```

### 3.4 端口访问规则

#### Primary进程：配置端口
```c
// Primary进程可以配置端口
ret = rte_eth_dev_configure(port_id, nb_rx_q, nb_tx_q, &port_conf);
ret = rte_eth_rx_queue_setup(port_id, 0, ...);
ret = rte_eth_tx_queue_setup(port_id, 0, ...);
ret = rte_eth_dev_start(port_id);
```

#### Secondary进程：使用端口
```c
// Secondary进程不能配置端口，但可以收发包
// 前提：Primary已经启动了端口

// 接收包（如果Primary配置了RX队列共享）
nb_rx = rte_eth_rx_burst(port_id, queue_id, bufs, BURST_SIZE);

// 发送包
nb_tx = rte_eth_tx_burst(port_id, queue_id, bufs, nb_pkts);
```

---

## 四、代码分析：关键实现

### 4.1 Server端核心代码结构

```c
// mp_server/main.c

int main(int argc, char **argv)
{
    // 1. 初始化EAL（默认为Primary）
    ret = rte_eal_init(argc, argv);

    // 2. 解析应用参数
    parse_app_args(argc, argv);  // 获取端口掩码、Client数量

    // 3. 创建共享内存池
    pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", ...);

    // 4. 初始化网络端口
    for (each port) {
        init_port(port_id, pktmbuf_pool);
    }

    // 5. 为每个Client创建Ring队列
    for (i = 0; i < num_clients; i++) {
        char ring_name[32];
        snprintf(ring_name, sizeof(ring_name), "MProc_Client_RX_%d", i);

        client_rings[i] = rte_ring_create(ring_name, RING_SIZE,
                                          rte_socket_id(),
                                          RING_F_SP_ENQ | RING_F_SC_DEQ);
    }

    // 6. 主循环：接收和分发数据包
    while (!force_quit) {
        // 从端口接收包
        nb_rx = rte_eth_rx_burst(port_id, 0, bufs, BURST_SIZE);

        // 分发到Client（简单轮询策略）
        for (i = 0; i < nb_rx; i++) {
            client_id = i % num_clients;  // 轮询分配
            rte_ring_enqueue(client_rings[client_id], bufs[i]);
        }
    }

    return 0;
}
```

### 4.2 Client端核心代码结构

```c
// mp_client/client.c

int main(int argc, char **argv)
{
    // 1. 初始化EAL（Secondary进程）
    ret = rte_eal_init(argc, argv);  // 带--proc-type=secondary

    // 2. 解析Client ID
    parse_app_args(argc, argv);  // 获取-n参数

    // 3. 查找Server创建的内存池
    pktmbuf_pool = rte_mempool_lookup("mbuf_pool");
    if (pktmbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot find mbuf pool\n");

    // 4. 查找自己的Ring队列
    char ring_name[32];
    snprintf(ring_name, sizeof(ring_name), "MProc_Client_RX_%d", client_id);

    rx_ring = rte_ring_lookup(ring_name);
    if (rx_ring == NULL)
        rte_exit(EXIT_FAILURE, "Cannot find ring: %s\n", ring_name);

    // 5. 主循环：接收和处理数据包
    while (!force_quit) {
        // 从Ring接收包
        nb_rx = rte_ring_dequeue_burst(rx_ring, (void **)bufs,
                                       BURST_SIZE, NULL);

        // 处理包（简单的L2转发）
        for (i = 0; i < nb_rx; i++) {
            // 转发到对应端口
            // Port 0 → Port 1, Port 2 → Port 3, ...
            uint16_t dst_port = (bufs[i]->port) ^ 1;

            rte_eth_tx_burst(dst_port, 0, &bufs[i], 1);
        }
    }

    return 0;
}
```

### 4.3 共享数据结构定义

```c
// shared/common.h

#define MAX_CLIENTS 16
#define RING_SIZE 4096
#define MBUF_POOL_NAME "mbuf_pool"
#define CLIENT_RING_NAME "MProc_Client_RX_%u"

// Client信息结构
struct client_info {
    uint16_t client_id;
    struct rte_ring *rx_ring;     // 接收队列
    volatile uint64_t rx_pkts;    // 统计：接收包数
    volatile uint64_t tx_pkts;    // 统计：发送包数
} __rte_cache_aligned;

// 全局共享信息（存储在共享内存中）
struct shared_info {
    uint32_t num_clients;
    struct client_info clients[MAX_CLIENTS];
} __rte_cache_aligned;
```

---

## 五、实验任务

### 任务1：运行官方示例（基础）

**目标：** 理解Primary-Secondary基本交互

**步骤：**
1. 编译DPDK官方示例
2. 按照2.4节的命令启动Server和2个Client
3. 使用`pktgen-dpdk`或`tcpreplay`发送测试流量
4. 观察Server和Client的统计输出

**思考问题：**
- 如果先启动Client会发生什么？
- 如果Server崩溃，Client能继续运行吗？
- 如何确认Ring队列没有溢出？

### 任务2：修改负载均衡策略（进阶）

**目标：** 修改Server的包分发逻辑

**要求：**
1. 将轮询策略改为基于流哈希的分发
2. 使用`rte_hash_crc()`计算5元组哈希
3. 确保同一个流的包发送到同一个Client

**提示代码：**
```c
// 计算5元组哈希
struct rte_ipv4_hdr *ipv4_hdr = rte_pktmbuf_mtod_offset(mbuf,
                                    struct rte_ipv4_hdr *,
                                    sizeof(struct rte_ether_hdr));

uint32_t hash = rte_hash_crc(&ipv4_hdr->src_addr,
                              sizeof(uint32_t) * 2,  // src+dst IP
                              0);

uint32_t client_id = hash % num_clients;
```

### 任务3：实现动态Client管理（挑战）

**目标：** 支持Client动态启停

**要求：**
1. Server维护Client活跃状态表
2. Client定期发送心跳消息给Server
3. Server检测Client超时，停止向其分发包
4. Client重新上线后能自动恢复

**提示：** 使用`rte_mp_msg`实现进程间控制消息

---

## 六、常见问题与最佳实践

### 6.1 常见错误

#### 错误1：Secondary进程启动失败
```
EAL: Cannot init memory: Resource temporarily unavailable
```
**原因：** Hugepage内存布局不一致
**解决：** 确保Primary和Secondary使用相同的EAL参数（`-m`、`-n`等）

#### 错误2：找不到共享对象
```
Cannot find ring: MProc_Client_RX_0
```
**原因：**
- Primary进程未成功创建Ring
- 名称拼写错误
- Primary进程未完全启动

**解决：**
- 检查Primary日志确认Ring创建成功
- 使用`rte_ring_list_dump()`打印所有Ring

#### 错误3：Ring队列溢出
```
rte_ring_enqueue_burst returned 0 (ring full)
```
**原因：** Client处理速度跟不上Server发送速度

**解决：**
```c
// 监控Ring使用率
unsigned int free_count = rte_ring_free_count(ring);
unsigned int capacity = rte_ring_get_capacity(ring);
float usage = 1.0f - (float)free_count / capacity;

if (usage > 0.9f) {
    printf("Warning: Ring usage %.1f%%\n", usage * 100);
}
```

### 6.2 性能优化建议

#### 优化1：合理选择Ring大小
```c
// Ring大小建议：
// - 小于1K：低延迟场景
// - 2K-8K：通用场景（官方示例使用4K）
// - 大于8K：高吞吐、能容忍延迟抖动的场景

#define RING_SIZE 4096  // 必须是2的幂
```

#### 优化2：使用Bulk操作
```c
// 避免单个mbuf操作
// ❌ 不推荐
for (i = 0; i < nb_pkts; i++) {
    rte_ring_enqueue(ring, bufs[i]);
}

// ✅ 推荐：批量操作
rte_ring_enqueue_bulk(ring, (void **)bufs, nb_pkts, NULL);
```

#### 优化3：CPU亲和性绑定
```bash
# Server绑定到核心1-2
taskset -c 1-2 ./dpdk-mp_server ...

# Client 0绑定到核心3
taskset -c 3 ./dpdk-mp_client -n 0 ...

# 避免跨NUMA访问
# 查看NUMA拓扑：numactl --hardware
```

#### 优化4：避免不必要的内存拷贝
```c
// ❌ 错误：拷贝数据
void *data = rte_pktmbuf_mtod(mbuf, void *);
memcpy(local_buf, data, mbuf->data_len);  // 不必要的拷贝

// ✅ 正确：直接操作mbuf
struct rte_ether_hdr *eth = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
// 直接修改eth字段
```

### 6.3 生产环境部署建议

#### 建议1：优雅退出流程
```c
// 1. 捕获信号
signal(SIGINT, signal_handler);
signal(SIGTERM, signal_handler);

// 2. 停止数据平面
force_quit = true;

// 3. 等待所有包处理完成
while (rte_ring_count(ring) > 0) {
    usleep(1000);
}

// 4. 清理资源
rte_eth_dev_stop(port_id);
rte_mempool_free(pool);
```

#### 建议2：资源监控
```c
// 定期打印统计信息
static void print_stats(void)
{
    printf("\n=== Statistics ===\n");
    printf("Server RX: %lu packets\n", server_rx_pkts);

    for (i = 0; i < num_clients; i++) {
        printf("Client %d: RX=%lu, TX=%lu\n",
               i, clients[i].rx_pkts, clients[i].tx_pkts);

        // Ring使用率
        unsigned int used = rte_ring_count(clients[i].ring);
        printf("  Ring usage: %u/%u (%.1f%%)\n",
               used, RING_SIZE, 100.0f * used / RING_SIZE);
    }
}
```

#### 建议3：日志管理
```c
// 使用DPDK日志系统
RTE_LOG(INFO, APP, "Client %d connected\n", client_id);
RTE_LOG(WARNING, APP, "Ring full, dropping packets\n");
RTE_LOG(ERR, APP, "Failed to allocate mbuf: %s\n", strerror(errno));

// 设置日志级别
rte_log_set_level(RTE_LOGTYPE_APP, RTE_LOG_DEBUG);
```

---

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
| 模式 | 性能 | 适用场景 |
|------|------|----------|
| SP-SC | 最高 | 单一生产者和消费者 |
| MP-SC | 中等 | 多个生产者，单一消费者 |
| SP-MC | 中等 | 单一生产者，多个消费者 |
| MP-MC | 较低 | 多个生产者和消费者 |

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

---

## 八、课程总结

### 8.1 关键知识点回顾

1. **多进程架构优势：** 故障隔离、模块化部署
2. **Primary vs Secondary：** 职责划分清晰
3. **共享内存机制：** Hugepage映射是核心
4. **Ring队列：** 高性能无锁进程间通信
5. **对象查找机制：** 通过名称共享mempool、ring等

### 8.2 最佳实践总结

✅ **DO（推荐做法）：**
- Primary负责创建，Secondary负责查找
- 使用批量API（bulk operations）
- 监控Ring使用率，避免溢出
- 合理绑定CPU和NUMA节点
- 实现优雅退出机制

❌ **DON'T（避免做法）：**
- 不要在Secondary中创建新对象
- 不要在Secondary中配置设备端口
- 不要使用全局变量共享数据（用共享内存）
- 不要忽略Ring满的情况
- 不要跨NUMA访问内存

### 8.3 学习资源

- **DPDK官方文档：** https://doc.dpdk.org/guides/sample_app_ug/multi_process.html
- **示例源码：** `dpdk/examples/multi_process/`
- **API参考：** https://doc.dpdk.org/api/

### 8.4 下一步学习方向

- **课程8：** DPDK QoS和流量管理
- **课程9：** DPDK加密和安全加速
- **课程10：** DPDK虚拟化和容器化部署

---

## 附录

### A. 常用命令速查

```bash
# 查看hugepage配置
cat /proc/meminfo | grep Huge

# 查看DPDK进程
ps aux | grep dpdk

# 查看共享内存
ls -lh /dev/hugepages/

# 查看NUMA拓扑
numactl --hardware

# 清理hugepage
rm -rf /dev/hugepages/*

# 绑定网卡到DPDK
dpdk-devbind.py --bind=vfio-pci 0000:03:00.0
```

### B. 完整的启动脚本示例

```bash
#!/bin/bash

# setup.sh - DPDK多进程启动脚本

# 1. 配置hugepage
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# 2. 绑定网卡
dpdk-devbind.py --bind=vfio-pci 0000:03:00.0 0000:03:00.1

# 3. 启动Server
./dpdk-mp_server -l 1-2 -n 4 -- -p 0x3 -n 2 &
SERVER_PID=$!

sleep 2  # 等待Server初始化

# 4. 启动Client
./dpdk-mp_client -l 3 --proc-type=auto -- -n 0 &
CLIENT0_PID=$!

./dpdk-mp_client -l 4 --proc-type=auto -- -n 1 &
CLIENT1_PID=$!

# 5. 等待用户中断
echo "Press Ctrl+C to stop..."
wait

# 6. 清理
kill $SERVER_PID $CLIENT0_PID $CLIENT1_PID
```

### C. 调试技巧

```bash
# 使用gdb调试Secondary进程
sudo gdb --args ./dpdk-mp_client -l 3 --proc-type=secondary -- -n 0

# 查看进程内存映射
cat /proc/<PID>/maps | grep huge

# 使用strace跟踪系统调用
sudo strace -f ./dpdk-mp_server ...

# 使用perf分析性能
sudo perf record -g ./dpdk-mp_client ...
sudo perf report
```

---

## 九、动手实战：三个递进式实验

为了帮助新手更好地掌握DPDK多进程编程,本课程提供了3个递进式的实战实验。

### 9.1 实验目录结构

```
7-multiprocess/
├── basic/              # 实验1:基础Primary-Secondary示例
│   ├── primary.c       # Primary进程
│   ├── secondary.c     # Secondary进程
│   └── common.h        # 共享定义
├── ring_comm/          # 实验2:Ring双向通信
│   ├── sender.c        # Sender进程(Primary)
│   ├── receiver.c      # Receiver进程(Secondary)
│   └── common.h        # 共享定义
├── client_server/      # 实验3:Client-Server架构
│   ├── server.c        # Server进程(Primary)
│   ├── client.c        # Client进程(Secondary)
│   └── common.h        # 共享定义
├── CMakeLists.txt      # 构建配置
├── setup.sh            # 自动化脚本
└── README.md           # 详细实验指南
```

### 9.2 快速开始

#### 方法1: 使用自动化脚本(推荐)

```bash
cd 7-multiprocess
sudo ./setup.sh
```

脚本提供交互式菜单:
1. 检查环境和编译
2. 运行实验1 (基础Primary-Secondary)
3. 运行实验2 (Ring双向通信)
4. 运行实验3 (Client-Server架构)
5. 清理共享内存

#### 方法2: 手动编译

```bash
cd 7-multiprocess
mkdir build && cd build
cmake ..
make -j$(nproc)

# 可执行文件生成在 build/bin/ 目录
ls -lh bin/
```

### 9.3 实验1: 基础Primary-Secondary示例 ⭐

**学习目标:**
- 理解Primary和Secondary进程的角色区别
- 掌握共享对象的创建和查找
- 实现单向消息传递

**运行步骤:**

```bash
# 终端1: 启动Primary进程
sudo ./build/bin/mp_basic_primary -l 0 --no-huge

# 终端2: 启动Secondary进程
sudo ./build/bin/mp_basic_secondary -l 1 --proc-type=secondary --no-huge
```

**预期输出:**

Primary终端将显示:
```
步骤1: Primary进程初始化EAL...
✓ EAL初始化成功 (进程类型: PRIMARY)

步骤2: 创建共享内存池 (名称: mp_basic_pool)...
✓ 内存池创建成功
  - 对象数量: 512
  - 对象大小: 64 字节

步骤3: 创建共享Ring队列 (名称: mp_basic_ring)...
✓ Ring队列创建成功
  - Ring大小: 256

[Primary] 发送消息 #0: Hello from Primary #0
[Primary] 发送消息 #1: Hello from Primary #1
```

Secondary终端将显示:
```
步骤1: Secondary进程初始化EAL...
✓ EAL初始化成功 (进程类型: SECONDARY)

步骤2: 查找Primary创建的内存池...
✓ 内存池查找成功

步骤3: 查找Primary创建的Ring队列...
✓ Ring队列查找成功

[Secondary] 接收消息 #0 (总计: 1)
            发送者ID: 0
            数据: Hello from Primary #0
```

**关键代码对比:**

```c
// Primary进程 - 创建共享对象
struct rte_mempool *mp = rte_mempool_create(
    MEMPOOL_NAME, NUM_MBUFS, OBJ_SIZE, ...);

struct rte_ring *ring = rte_ring_create(
    RING_NAME, RING_SIZE, ...);

// Secondary进程 - 查找共享对象
struct rte_mempool *mp = rte_mempool_lookup(MEMPOOL_NAME);
struct rte_ring *ring = rte_ring_lookup(RING_NAME);
```

**思考题:**
1. 如果先启动Secondary会怎样?
2. Primary退出后Secondary还能运行吗?
3. 如何支持多个Secondary进程?

### 9.4 实验2: Ring双向通信示例 ⭐⭐

**学习目标:**
- 掌握双向Ring队列的创建和使用
- 实现Ping-Pong通信模式
- 测量往返时延(RTT)

**架构图:**

```
┌─────────────┐                    ┌─────────────┐
│   Sender    │                    │  Receiver   │
│  (Primary)  │                    │ (Secondary) │
└──────┬──────┘                    └──────┬──────┘
       │                                  │
       │  Ring: P2S                       │
       ├─────────────Ping──────────────>  │
       │                                  │
       │  Ring: S2P                       │
       │  <─────────────Pong──────────────┤
```

**运行步骤:**

```bash
# 终端1: 启动Sender进程
sudo ./build/bin/mp_ring_sender -l 0 --no-huge

# 终端2: 启动Receiver进程
sudo ./build/bin/mp_ring_receiver -l 1 --proc-type=secondary --no-huge
```

**预期输出:**

Sender终端:
```
创建双向Ring队列...
✓ Ring (Primary->Secondary) 创建成功
✓ Ring (Secondary->Primary) 创建成功

[Sender] 发送 Ping #0
[Sender] 接收 Pong #0 (RTT: 1234 us)
         内容: Pong #0 from Secondary

--- 统计 (Sender) ---
发送Ping: 5
收到Pong: 5
丢失率: 0.00%
```

Receiver终端:
```
查找双向Ring队列...
✓ Ring (Primary->Secondary) 查找成功
✓ Ring (Secondary->Primary) 查找成功

[Receiver] 接收 Ping #0
           内容: Ping #0 from Primary
[Receiver] 回复 Pong #0
```

**关键技术点:**
- 双向通信需要两个Ring队列
- RTT测量: 在Ping消息中携带时间戳,Pong原样返回
- 批量操作提高性能

**思考题:**
1. 为什么需要两个Ring而不是一个?
2. 如何优化RTT?
3. 如果Ring满了会发生什么?

### 9.5 实验3: Client-Server架构 ⭐⭐⭐

**学习目标:**
- 掌握实际的多进程架构设计
- 实现负载均衡(Round-Robin)
- 支持多个Client并行处理

**架构图:**

```
                    Packet Generator
                           │
                    ┌──────▼──────┐
                    │   Server    │
                    │  (Primary)  │
                    └──────┬──────┘
                           │
           ┌───────────────┼───────────────┐
           │               │               │
    ┌──────▼──────┐ ┌─────▼──────┐ ┌─────▼──────┐
    │  Client 0   │ │  Client 1  │ │  Client N  │
    │(Secondary)  │ │(Secondary) │ │(Secondary) │
    └─────────────┘ └────────────┘ └────────────┘
          │               │               │
          └───────────────┴───────────────┘
                  (并行处理数据包)
```

**运行步骤:**

```bash
# 终端1: 启动Server (支持2个Client)
sudo ./build/bin/mp_cs_server -l 0 --no-huge -- -n 2

# 终端2: 启动Client 0
sudo ./build/bin/mp_cs_client -l 1 --proc-type=secondary --no-huge -- -n 0

# 终端3: 启动Client 1
sudo ./build/bin/mp_cs_client -l 2 --proc-type=secondary --no-huge -- -n 1
```

**预期输出:**

Server终端:
```
配置: 2 个Client进程

步骤1: 创建packet mbuf内存池...
✓ Mbuf pool创建成功 (总mbuf: 16382)

步骤2: 为每个Client创建Ring队列...
✓ Ring 'cs_client_ring_0' 创建成功
✓ Ring 'cs_client_ring_1' 创建成功

Server开始生成并分发数据包...

--- Server统计 ---
已生成数据包: 3200
Mbuf可用: 14500
Client 0 Ring使用: 50/2048
Client 1 Ring使用: 45/2048
```

Client终端:
```
Client ID: 0

步骤1: 查找共享mbuf内存池...
✓ Mbuf pool查找成功

步骤2: 查找自己的Ring队列...
✓ Ring 'cs_client_ring_0' 查找成功

[Client 0] 处理包 #0
           时间戳: 123456789
           内容: Packet #0 for Client 0

--- Client 0 统计 ---
已接收数据包: 1600
Ring使用: 32/2048
```

**负载均衡策略:**

Server使用简单的Round-Robin轮询:
```c
next_client = (next_client + 1) % num_clients;
rte_ring_enqueue_burst(client_rings[next_client], pkts, BURST_SIZE);
```

**扩展思考:**
1. 如何改成基于Hash的分发? (提示: 5元组哈希)
2. 如何支持Client动态加入/退出?
3. 如何监控各Client的负载?

### 9.6 实验资源

**完整实验指南:** [7-multiprocess/README.md](7-multiprocess/README.md)

**源代码位置:**
- 实验1: [7-multiprocess/basic/](7-multiprocess/basic/)
- 实验2: [7-multiprocess/ring_comm/](7-multiprocess/ring_comm/)
- 实验3: [7-multiprocess/client_server/](7-multiprocess/client_server/)

**自动化脚本:** [7-multiprocess/setup.sh](7-multiprocess/setup.sh)

### 9.7 学习路径建议

**初级(第1周):**
1. ✅ 完成实验1,理解基础概念
2. ✅ 修改实验1,尝试多个Secondary进程
3. ✅ 阅读本课程理论部分

**中级(第2周):**
1. ✅ 完成实验2,理解双向通信
2. ✅ 测量不同Ring大小下的RTT
3. ✅ 实现基于Hash的消息路由

**高级(第3周):**
1. ✅ 完成实验3,理解生产架构
2. ✅ 修改为基于5元组Hash的分发
3. ✅ 实现Client动态管理(心跳机制)

### 9.8 常见问题

#### Q: 编译错误 "cannot find -ldpdk"
**A:** 检查DPDK是否安装:
```bash
pkg-config --modversion libdpdk
sudo apt-get install dpdk dpdk-dev  # 如果未安装
```

#### Q: 运行时错误 "Cannot init EAL"
**A:** 尝试以下方案:
1. 使用`sudo`运行
2. 使用`--no-huge`参数(测试时方便)
3. 配置hugepage: `echo 512 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages`

#### Q: Secondary找不到共享对象
**A:** 检查:
- Primary进程是否已经启动?
- 是否使用了`--proc-type=secondary`?
- 对象名称是否一致?

---

## 十、进阶挑战

完成基础实验后,尝试这些挑战:

1. **实现流表管理:** 在Client中维护5元组流表,统计每个流的包数
2. **添加控制平面:** 使用`rte_mp`实现进程间控制消息
3. **性能测试:** 测试不同配置下的吞吐量和延迟
4. **实现QoS:** 为不同优先级的流分配不同的Client
5. **故障恢复:** 实现Client崩溃后的自动重启

---

**课程结束，感谢学习！**

如有问题，请参考DPDK官方文档或在GitHub提Issue。

**实战实验文档:** [7-multiprocess/README.md](7-multiprocess/README.md)
