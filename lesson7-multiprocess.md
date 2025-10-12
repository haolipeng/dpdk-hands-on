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
# 方法1: 使用Makefile编译(推荐)
cd /path/to/dpdk-stable-24.11.1/examples/multi_process/client_server_mp

# 编译Server
cd mp_server
make
# 生成: build/mp_server

# 编译Client
cd ../mp_client
make
# 生成: build/mp_client

# 方法2: 使用meson编译整个DPDK(会包含所有示例)
cd /path/to/dpdk-stable-24.11.1
meson build
cd build
ninja
# 生成: build/examples/dpdk-mp_server, dpdk-mp_client
```

#### 运行Server进程

```bash
# 启动Server（Primary进程）
sudo ./mp_server/build/mp_server \
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
sudo ./mp_client/build/mp_client \
    -l 3 \                      # 使用核心3
    --proc-type=auto \          # 自动识别为Secondary
    -- \
    -n 0                        # Client ID为0
```

**启动Client 1：**
```bash
sudo ./mp_client/build/mp_client \
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

## 四、官方示例代码深度分析

本节基于DPDK官方`client_server_mp`示例进行详细分析,源码位于:
`/home/work/dpdk-stable-24.11.1/examples/multi_process/client_server_mp/`

### 4.1 Server端完整流程分析

#### 4.1.1 主程序入口 ([mp_server/main.c:290-313](mp_server/main.c#L290-L313))

```c
int main(int argc, char *argv[])
{
    signal(SIGINT, signal_handler);

    // 1. 初始化系统(包含EAL、端口、Ring等)
    if (init(argc, argv) < 0)
        return -1;
    RTE_LOG(INFO, APP, "Finished Process Init.\n");

    // 2. 为每个Client分配RX缓冲区
    cl_rx_buf = calloc(num_clients, sizeof(cl_rx_buf[0]));

    // 3. 清除统计数据
    clear_stats();

    // 4. 启动其他核心用于统计显示
    rte_eal_mp_remote_launch(sleep_lcore, NULL, SKIP_MAIN);

    // 5. 主核心执行数据包转发
    do_packet_forwarding();

    // 6. 清理资源
    rte_eal_cleanup();
    return 0;
}
```

#### 4.1.2 初始化流程 ([mp_server/init.c:244-289](mp_server/init.c#L244-L289))

```c
int init(int argc, char *argv[])
{
    int retval;
    const struct rte_memzone *mz;
    uint16_t i;

    // 1. 初始化EAL(默认为Primary进程)
    retval = rte_eal_init(argc, argv);
    if (retval < 0)
        return -1;
    argc -= retval;
    argv += retval;

    // 2. 在共享内存中创建端口信息结构(使用memzone)
    mz = rte_memzone_reserve(MZ_PORT_INFO, sizeof(*ports),
                            rte_socket_id(), NO_FLAGS);
    if (mz == NULL)
        rte_exit(EXIT_FAILURE, "Cannot reserve memory zone for port information\n");
    memset(mz->addr, 0, sizeof(*ports));
    ports = mz->addr;

    // 3. 解析应用参数(-p端口掩码, -n客户端数量)
    retval = parse_app_args(argc, argv);
    if (retval != 0)
        return -1;

    // 4. 创建共享mbuf内存池
    retval = init_mbuf_pools();
    if (retval != 0)
        rte_exit(EXIT_FAILURE, "Cannot create needed mbuf pools\n");

    // 5. 初始化所有网络端口
    for (i = 0; i < ports->num_ports; i++) {
        retval = init_port(ports->id[i]);
        if (retval != 0)
            rte_exit(EXIT_FAILURE, "Cannot initialise port %u\n", (unsigned)i);
    }

    check_all_ports_link_status(ports->num_ports, (~0x0));

    // 6. 为每个Client创建Ring队列
    init_shm_rings();

    return 0;
}
```

**关键设计点:**
- **Memzone:** 使用`rte_memzone_reserve()`在hugepage中创建共享的port_info结构
- **共享命名:** `MZ_PORT_INFO = "MProc_port_info"` 作为共享对象名称

#### 4.1.3 创建共享内存池 ([mp_server/init.c:62-82](mp_server/init.c#L62-L82))

```c
static int init_mbuf_pools(void)
{
    // 计算所需mbuf总数
    const unsigned int num_mbufs_server =
        RTE_MP_RX_DESC_DEFAULT * ports->num_ports;  // Server RX需求
    const unsigned int num_mbufs_client =
        num_clients * (CLIENT_QUEUE_RINGSIZE +
                       RTE_MP_TX_DESC_DEFAULT * ports->num_ports);  // Client TX需求
    const unsigned int num_mbufs_mp_cache =
        (num_clients + 1) * MBUF_CACHE_SIZE;  // 各进程的per-core缓存
    const unsigned int num_mbufs =
        num_mbufs_server + num_mbufs_client + num_mbufs_mp_cache;

    printf("Creating mbuf pool '%s' [%u mbufs] ...\n",
            PKTMBUF_POOL_NAME, num_mbufs);

    pktmbuf_pool = rte_pktmbuf_pool_create(PKTMBUF_POOL_NAME, num_mbufs,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    return pktmbuf_pool == NULL; /* 0 on success */
}
```

**内存规划:**
- Server RX: `1024 * 端口数` (每个端口1024个RX描述符)
- Client TX: `num_clients * (128 + 1024 * 端口数)` (Ring缓冲 + TX描述符)
- Cache: `(num_clients + 1) * 512` (每个进程的per-core缓存)

#### 4.1.4 端口初始化 ([mp_server/init.c:92-144](mp_server/init.c#L92-L144))

```c
static int init_port(uint16_t port_num)
{
    const struct rte_eth_conf port_conf = {
        .rxmode = {
            .mq_mode = RTE_ETH_MQ_RX_RSS  // 启用RSS
        }
    };
    const uint16_t rx_rings = 1;  // Server使用1个RX队列
    const uint16_t tx_rings = num_clients;  // 每个Client一个TX队列

    uint16_t rx_ring_size = RTE_MP_RX_DESC_DEFAULT;
    uint16_t tx_ring_size = RTE_MP_TX_DESC_DEFAULT;
    uint16_t q;
    int retval;

    // 配置端口: 1个RX队列, N个TX队列
    if ((retval = rte_eth_dev_configure(port_num, rx_rings, tx_rings,
        &port_conf)) != 0)
        return retval;

    // 调整队列大小以匹配硬件能力
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port_num, &rx_ring_size,
            &tx_ring_size);

    // 设置RX队列(Server专用)
    for (q = 0; q < rx_rings; q++) {
        retval = rte_eth_rx_queue_setup(port_num, q, rx_ring_size,
                rte_eth_dev_socket_id(port_num),
                NULL, pktmbuf_pool);
        if (retval < 0) return retval;
    }

    // 设置TX队列(每个Client一个)
    for (q = 0; q < tx_rings; q++) {
        retval = rte_eth_tx_queue_setup(port_num, q, tx_ring_size,
                rte_eth_dev_socket_id(port_num),
                NULL);
        if (retval < 0) return retval;
    }

    // 启用混杂模式
    retval = rte_eth_promiscuous_enable(port_num);

    // 启动端口
    retval = rte_eth_dev_start(port_num);

    return 0;
}
```

**队列设计:**
- **1个RX队列:** Server独占,接收所有数据包
- **N个TX队列:** 每个Client使用独立的TX队列,避免竞争

#### 4.1.5 创建Ring队列 ([mp_server/init.c:152-176](mp_server/init.c#L152-L176))

```c
static int init_shm_rings(void)
{
    unsigned i;
    unsigned socket_id;
    const char *q_name;
    const unsigned ringsize = CLIENT_QUEUE_RINGSIZE;  // 128

    // 分配Client数组
    clients = rte_malloc("client details",
        sizeof(*clients) * num_clients, 0);
    if (clients == NULL)
        rte_exit(EXIT_FAILURE, "Cannot allocate memory for client program details\n");

    for (i = 0; i < num_clients; i++) {
        socket_id = rte_socket_id();
        q_name = get_rx_queue_name(i);  // "MProc_Client_%u_RX"

        // 创建Ring: 单生产者(Server) + 单消费者(Client)
        clients[i].rx_q = rte_ring_create(q_name,
                ringsize, socket_id,
                RING_F_SP_ENQ | RING_F_SC_DEQ);

        if (clients[i].rx_q == NULL)
            rte_exit(EXIT_FAILURE, "Cannot create rx ring queue for client %u\n", i);
    }
    return 0;
}
```

**Ring标志说明:**
- `RING_F_SP_ENQ`: Single Producer - Server是唯一的生产者
- `RING_F_SC_DEQ`: Single Consumer - 每个Client是唯一的消费者

#### 4.1.6 数据包转发主循环 ([mp_server/main.c:254-275](mp_server/main.c#L254-L275))

```c
static void do_packet_forwarding(void)
{
    unsigned port_num = 0;  // 当前处理的端口索引

    for (;;) {
        struct rte_mbuf *buf[PACKET_READ_SIZE];  // 32个包的burst
        uint16_t rx_count;

        // 从当前端口接收数据包
        rx_count = rte_eth_rx_burst(ports->id[port_num], 0,
                buf, PACKET_READ_SIZE);
        ports->rx_stats.rx[port_num] += rx_count;

        // 处理并分发到Client
        if (likely(rx_count > 0))
            process_packets(port_num, buf, rx_count);

        // 轮转到下一个端口(轮询所有端口)
        if (++port_num == ports->num_ports)
            port_num = 0;
    }
}
```

**轮询策略:** Server轮询所有端口,而不是阻塞在单个端口上

#### 4.1.7 负载均衡策略 ([mp_server/main.c:232-248](mp_server/main.c#L232-L248))

```c
static void process_packets(uint32_t port_num __rte_unused,
        struct rte_mbuf *pkts[], uint16_t rx_count)
{
    uint16_t i;
    static uint8_t client = 0;  // 轮询计数器

    // Round-Robin分发
    for (i = 0; i < rx_count; i++) {
        enqueue_rx_packet(client, pkts[i]);  // 加入本地缓冲

        // 轮转到下一个Client
        if (++client == num_clients)
            client = 0;
    }

    // 批量发送到各Client的Ring
    for (i = 0; i < num_clients; i++)
        flush_rx_queue(i);
}
```

#### 4.1.8 批量入队优化 ([mp_server/main.c:196-216](mp_server/main.c#L196-L216))

```c
static void flush_rx_queue(uint16_t client)
{
    uint16_t j;
    struct client *cl;

    if (cl_rx_buf[client].count == 0)
        return;

    cl = &clients[client];

    // 批量入队到Ring
    if (rte_ring_enqueue_bulk(cl->rx_q,
            (void **)cl_rx_buf[client].buffer,
            cl_rx_buf[client].count, NULL) == 0) {
        // 入队失败(Ring满),释放数据包
        for (j = 0; j < cl_rx_buf[client].count; j++)
            rte_pktmbuf_free(cl_rx_buf[client].buffer[j]);
        cl->stats.rx_drop += cl_rx_buf[client].count;
    } else {
        cl->stats.rx += cl_rx_buf[client].count;
    }

    cl_rx_buf[client].count = 0;
}
```

**性能优化技巧:**
1. **本地缓冲:** 先缓存到`cl_rx_buf`,再批量enqueue
2. **Bulk操作:** 使用`rte_ring_enqueue_bulk()`而非单个enqueue
3. **失败处理:** Ring满时释放mbuf,避免内存泄漏

### 4.2 Client端完整流程分析

#### 4.2.1 主程序入口 ([mp_client/client.c:203-273](mp_client/client.c#L203-L273))

```c
int main(int argc, char *argv[])
{
    const struct rte_memzone *mz;
    struct rte_ring *rx_ring;
    struct rte_mempool *mp;
    struct port_info *ports;
    int need_flush = 0;
    int retval;
    void *pkts[PKT_READ_SIZE];
    uint16_t sent;

    // 1. 初始化EAL(Secondary进程)
    if ((retval = rte_eal_init(argc, argv)) < 0)
        return -1;
    argc -= retval;
    argv += retval;

    // 2. 解析Client ID
    if (parse_app_args(argc, argv) < 0)
        rte_exit(EXIT_FAILURE, "Invalid command-line arguments\n");

    if (rte_eth_dev_count_avail() == 0)
        rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

    // 3. 查找Server创建的Ring队列
    rx_ring = rte_ring_lookup(get_rx_queue_name(client_id));
    if (rx_ring == NULL)
        rte_exit(EXIT_FAILURE, "Cannot get RX ring - is server process running?\n");

    // 4. 查找Server创建的内存池
    mp = rte_mempool_lookup(PKTMBUF_POOL_NAME);
    if (mp == NULL)
        rte_exit(EXIT_FAILURE, "Cannot get mempool for mbufs\n");

    // 5. 查找共享的端口信息结构
    mz = rte_memzone_lookup(MZ_PORT_INFO);
    if (mz == NULL)
        rte_exit(EXIT_FAILURE, "Cannot get port info structure\n");
    ports = mz->addr;
    tx_stats = &(ports->tx_stats[client_id]);  // 本Client的统计结构

    // 6. 配置输出端口和TX buffer
    configure_output_ports(ports);

    RTE_LOG(INFO, APP, "Finished Process Init.\n");
    printf("\nClient process %d handling packets\n", client_id);

    // 7. 主循环：接收和转发数据包
    for (;;) {
        uint16_t i, rx_pkts;

        // 从Ring批量出队
        rx_pkts = rte_ring_dequeue_burst(rx_ring, pkts,
                PKT_READ_SIZE, NULL);

        // 没有收到包且需要flush TX buffer
        if (rx_pkts == 0 && need_flush) {
            for (i = 0; i < ports->num_ports; i++) {
                uint16_t port = ports->id[i];
                sent = rte_eth_tx_buffer_flush(port, client_id,
                                               tx_buffer[port]);
                tx_stats->tx[port] += sent;
            }
            need_flush = 0;
            continue;
        }

        // 处理接收到的包
        for (i = 0; i < rx_pkts; i++)
            handle_packet(pkts[i]);

        need_flush = 1;
    }

    rte_eal_cleanup();
}
```

**查找对象顺序:** Ring → Mempool → Memzone(port_info)

#### 4.2.2 配置输出端口 ([mp_client/client.c:160-177](mp_client/client.c#L160-L177))

```c
static void configure_output_ports(const struct port_info *ports)
{
    int i;

    if (ports->num_ports > RTE_MAX_ETHPORTS)
        rte_exit(EXIT_FAILURE, "Too many ethernet ports.\n");

    // 配置端口对: Port 0↔1, Port 2↔3, ...
    for (i = 0; i < ports->num_ports - 1; i += 2) {
        uint16_t p1 = ports->id[i];
        uint16_t p2 = ports->id[i+1];

        output_ports[p1] = p2;  // Port 0 → Port 1
        output_ports[p2] = p1;  // Port 1 → Port 0

        // 为每个端口配置TX buffer
        configure_tx_buffer(p1, MBQ_CAPACITY);
        configure_tx_buffer(p2, MBQ_CAPACITY);
    }
}
```

**端口映射:** 实现简单的L2转发,Port 0和Port 1互为出口

#### 4.2.3 配置TX Buffer ([mp_client/client.c:133-153](mp_client/client.c#L133-L153))

```c
static void configure_tx_buffer(uint16_t port_id, uint16_t size)
{
    int ret;

    // 分配TX buffer(32个包的缓冲)
    tx_buffer[port_id] = rte_zmalloc_socket("tx_buffer",
            RTE_ETH_TX_BUFFER_SIZE(size), 0,
            rte_eth_dev_socket_id(port_id));

    if (tx_buffer[port_id] == NULL)
        rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
                 port_id);

    // 初始化TX buffer
    rte_eth_tx_buffer_init(tx_buffer[port_id], size);

    // 设置错误回调(处理发送失败的包)
    ret = rte_eth_tx_buffer_set_err_callback(tx_buffer[port_id],
            flush_tx_error_callback, (void *)(intptr_t)port_id);

    if (ret < 0)
        rte_exit(EXIT_FAILURE,
        "Cannot set error callback for tx buffer on port %u\n", port_id);
}
```

**TX Buffer作用:**
- 批量发送优化
- 自动flush机制
- 错误处理回调

#### 4.2.4 数据包处理 ([mp_client/client.c:184-196](mp_client/client.c#L184-L196))

```c
static void handle_packet(struct rte_mbuf *buf)
{
    int sent;
    const uint16_t in_port = buf->port;  // 原始入口端口
    const uint16_t out_port = output_ports[in_port];  // 查找出口端口
    struct rte_eth_dev_tx_buffer *buffer = tx_buffer[out_port];

    // 使用TX buffer发送(自动批量)
    sent = rte_eth_tx_buffer(out_port, client_id, buffer, buf);

    // 更新统计(sent可能是0,表示还在buffer中)
    if (sent)
        tx_stats->tx[out_port] += sent;
}
```

**关键点:**
- 使用`rte_eth_tx_buffer()`而非`rte_eth_tx_burst()`
- 每个Client使用独立的TX队列(client_id作为queue_id)

#### 4.2.5 TX错误处理 ([mp_client/client.c:118-130](mp_client/client.c#L118-L130))

```c
static void flush_tx_error_callback(struct rte_mbuf **unsent,
                                   uint16_t count,
                                   void *userdata)
{
    int i;
    uint16_t port_id = (uintptr_t)userdata;

    // 统计丢弃的包
    tx_stats->tx_drop[port_id] += count;

    // 释放未能发送的mbuf
    for (i = 0; i < count; i++)
        rte_pktmbuf_free(unsent[i]);
}
```

### 4.3 共享数据结构定义 ([shared/common.h:1-58](shared/common.h#L1-L58))

```c
#define MAX_CLIENTS 16

// RX统计结构(Server写,按端口对齐到缓存行)
struct __rte_cache_aligned rx_stats {
    uint64_t rx[RTE_MAX_ETHPORTS];
};

// TX统计结构(每个Client一个,避免缓存行竞争)
struct __rte_cache_aligned tx_stats {
    uint64_t tx[RTE_MAX_ETHPORTS];
    uint64_t tx_drop[RTE_MAX_ETHPORTS];
};

// 端口信息结构(存储在memzone共享内存中)
struct port_info {
    uint16_t num_ports;                    // 端口数量
    uint16_t id[RTE_MAX_ETHPORTS];         // 端口ID数组
    volatile struct rx_stats rx_stats;     // Server的RX统计
    volatile struct tx_stats tx_stats[MAX_CLIENTS];  // 每个Client的TX统计
};

// 共享对象命名规范
#define MP_CLIENT_RXQ_NAME "MProc_Client_%u_RX"
#define PKTMBUF_POOL_NAME "MProc_pktmbuf_pool"
#define MZ_PORT_INFO "MProc_port_info"

// 获取Client的Ring队列名称
static inline const char *get_rx_queue_name(uint8_t id)
{
    static char buffer[sizeof(MP_CLIENT_RXQ_NAME) + 2];
    snprintf(buffer, sizeof(buffer), MP_CLIENT_RXQ_NAME, id);
    return buffer;
}
```

**数据结构设计原则:**

1. **缓存行对齐(`__rte_cache_aligned`):**
   - 避免False Sharing
   - 每个Client的统计独占缓存行

2. **Volatile修饰:**
   - `volatile struct tx_stats`: 多进程访问,防止编译器优化

3. **Memzone存储:**
   - `port_info`存储在共享memzone中
   - 通过`rte_memzone_reserve()`和`rte_memzone_lookup()`共享

4. **命名约定:**
   - 所有共享对象以`MProc_`前缀
   - Ring名称包含Client ID: `MProc_Client_0_RX`

### 4.4 流程图总结

**完整数据流:**

```
┌─────────────────────────────────────────────────────────────────┐
│                       NIC Port 0 RX                             │
└────────────────────────────┬────────────────────────────────────┘
                             │
                   ┌─────────▼─────────┐
                   │   Server Process  │
                   │    (Primary)      │
                   │                   │
                   │ 1. rx_burst()     │
                   │ 2. Round-Robin    │
                   │ 3. enqueue_bulk() │
                   └─────┬───────┬─────┘
                         │       │
        ┌────────────────┘       └────────────────┐
        │                                         │
  ┌─────▼─────┐                           ┌─────▼─────┐
  │ Ring 0    │                           │ Ring 1    │
  │ (128 pkts)│                           │ (128 pkts)│
  └─────┬─────┘                           └─────┬─────┘
        │                                       │
  ┌─────▼─────┐                          ┌─────▼─────┐
  │ Client 0  │                          │ Client 1  │
  │(Secondary)│                          │(Secondary)│
  │           │                          │           │
  │1.dequeue()│                          │1.dequeue()│
  │2.handle() │                          │2.handle() │
  │3.tx_buf() │                          │3.tx_buf() │
  └─────┬─────┘                          └─────┬─────┘
        │                                      │
        │  TX Queue 0                          │  TX Queue 1
        └──────────┬───────────────────────────┘
                   │
        ┌──────────▼──────────┐
        │  NIC Port 1 TX      │
        └─────────────────────┘
```

**关键性能指标(基于官方示例):**
- Ring大小: 128 (CLIENT_QUEUE_RINGSIZE)
- Burst大小: 32 (PACKET_READ_SIZE)
- RX描述符: 1024 (RTE_MP_RX_DESC_DEFAULT)
- TX描述符: 1024 (RTE_MP_TX_DESC_DEFAULT)
- Mbuf Cache: 512 (MBUF_CACHE_SIZE)

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
./mp_server/build/mp_server -l 1-2 -n 4 -- -p 0x3 -n 2 &
SERVER_PID=$!

sleep 2  # 等待Server初始化

# 4. 启动Client
./mp_client/build/mp_client -l 3 --proc-type=auto -- -n 0 &
CLIENT0_PID=$!

./mp_client/build/mp_client -l 4 --proc-type=auto -- -n 1 &
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
sudo gdb --args ./mp_client/build/mp_client -l 3 --proc-type=secondary -- -n 0

# 查看进程内存映射
cat /proc/<PID>/maps | grep huge

# 使用strace跟踪系统调用
sudo strace -f ./mp_server/build/mp_server -l 1-2 -n 4 -- -p 0x3 -n 2

# 使用perf分析性能
sudo perf record -g ./mp_client/build/mp_client -l 3 --proc-type=auto -- -n 0
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
