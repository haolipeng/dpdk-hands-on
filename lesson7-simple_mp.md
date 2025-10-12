# simple_mp 示例详细分析

---

## 一、示例概述

### 1.1 示例位置与推荐程度

**位置:** `examples/multi_process/simple_mp/`
**推荐程度:** ⭐⭐⭐⭐⭐ (最适合初学者)

### 1.2 示例特点

这是**最简单最基础的多进程示例**,展示了两个进程之间的基本双向通信:

- ✅ **无需真实网卡:** 不涉及网络端口,专注于进程间通信
- ✅ **双向Ring队列:** Primary↔Secondary 双向消息传递
- ✅ **交互式命令行:** 可手动发送消息,观察通信过程
- ✅ **代码简洁:** 仅130行,易于理解和学习

---

## 二、架构设计

### 2.1 进程间通信架构

```
┌─────────────────┐                    ┌─────────────────┐
│  Primary 进程   │                    │ Secondary 进程  │
│                 │                    │                 │
│  ┌───────────┐  │   Ring: PRI_2_SEC  │  ┌───────────┐  │
│  │ send_ring │──┼───────────────────→│  │ recv_ring │  │
│  └───────────┘  │                    │  └───────────┘  │
│                 │                    │                 │
│  ┌───────────┐  │   Ring: SEC_2_PRI  │  ┌───────────┐  │
│  │ recv_ring │←─┼────────────────────┼──│ send_ring │  │
│  └───────────┘  │                    │  └───────────┘  │
│                 │                    │                 │
│  共享 Mempool: MSG_POOL (存储消息buffer)               │
│  ┌─────────────────────────────────┐                   │
│  │ "Hello"  "World"  "Test" ...   │                   │
│  └─────────────────────────────────┘                   │
│                 │                    │                 │
│  主核心:        │                    │  主核心:        │
│  命令行交互     │                    │  命令行交互     │
│                 │                    │                 │
│  工作核心:      │                    │  工作核心:      │
│  接收消息线程   │                    │  接收消息线程   │
└─────────────────┘                    └─────────────────┘
```

### 2.2 进程类型说明

程序在启动时,只能选择以Primary方式启动,或者以Secondary方式启动。

- **Primary进程:** 负责创建共享资源(Ring队列、Mempool)
- **Secondary进程:** 查找并使用Primary创建的共享资源

---

## 三、编译与运行

### 3.1 编译

```bash
cd /home/work/dpdk-stable-24.11.1/examples/multi_process/simple_mp
make
# 生成: build/simple_mp
```

### 3.2 启动Primary进程

先启动Primary进程,`-l`参数指定使用的逻辑核为core0和core1,`--proc-type`参数可以省略,默认第一个进程是Primary进程,也可以指定值`auto`,表示自动检测进程的类型。

**终端1 - 启动Primary进程:**

```bash
sudo ./build/simple_mp -l 0,1 --proc-type=primary
```

**输出:**
```
EAL: Detected lcore 0 as core 0 on socket 0
EAL: Detected lcore 1 as core 1 on socket 0
EAL: Detected lcore 2 as core 2 on socket 0
...
EAL: Master lcore 0 is ready (tid=c1f1e900;cpuset=[0])
...
Starting core 1
simple_mp >
```

### 3.3 启动Secondary进程

Secondary进程需要在Primary进程之后启动,`--proc-type`参数**必须指定**(因为不指定该参数,将默认为Primary进程,程序将初始化失败,因为已经存在了Primary进程),值可以为`secondary`或`auto`。

**终端2 - 启动Secondary进程:**

```bash
sudo ./build/simple_mp -l 2,3 --proc-type=secondary
```

**输出:**
```
EAL: Multi-process socket /var/run/dpdk/rte/mp_socket_mp_11111
Starting core 3
EAL: Finished Process Init.

simple_mp >
```

### 3.4 查看支持的命令

simple_mp使用DPDK cmdline库完成命令行功能,启动simple_mp程序后,输入`help`可以查看支持的命令:

```
simple_mp > help
Simple demo example of multi-process in RTE

This is a readline-like interface that can be used to
send commands to the simple app. Commands supported are:

- send [string]
- help
- quit
```

### 3.5 交互测试

#### Primary进程发送字符串

在Primary进程的命令行中输入:

```
simple_mp > send hello1
```

Secondary进程将打印:

```
simple_mp > core 3: Received 'hello1'
```

#### Secondary进程发送字符串

在Secondary进程执行:

```
simple_mp > send hello2
```

Primary进程将打印:

```
simple_mp > core 1: Received 'hello2'
```

---



## 四、核心代码分析

### 4.1 共享对象定义

```c
// main.c:47-49
static const char *_MSG_POOL = "MSG_POOL";      // 消息内存池名称
static const char *_SEC_2_PRI = "SEC_2_PRI";    // Secondary→Primary的Ring
static const char *_PRI_2_SEC = "PRI_2_SEC";    // Primary→Secondary的Ring

struct rte_ring *send_ring, *recv_ring;
struct rte_mempool *message_pool;
```

### 4.2 收发队列创建与查找

#### Primary进程: 创建双向Ring队列

```c
const unsigned ring_size = 64;          // Ring队列大小
const unsigned pool_size = 1024;        // 消息buffer池大小
const unsigned pool_cache = 32;         // 每核心缓存

if (rte_eal_process_type() == RTE_PROC_PRIMARY) {
    // 创建双向Ring队列
    send_ring = rte_ring_create(_PRI_2_SEC, ring_size, rte_socket_id(), flags);
    recv_ring = rte_ring_create(_SEC_2_PRI, ring_size, rte_socket_id(), flags);

    // 创建消息内存池(用于存储字符串buffer)
    message_pool = rte_mempool_create(_MSG_POOL, pool_size,
            STR_TOKEN_SIZE, pool_cache, priv_data_sz,
            NULL, NULL, NULL, NULL,
            rte_socket_id(), flags);
}
```

**关键参数:**

- `ring_size = 64`: Ring队列大小(可存放64个消息指针)
- `pool_size = 1024`: 内存池有1024个buffer
- `STR_TOKEN_SIZE`: 每个buffer可存储的字符串长度

#### Secondary进程: 查找共享对象

对于Secondary进程,查找发送和接收队列,顺序跟Primary相反:

- **发送队列:** SEC_2_PRI
- **接收队列:** PRI_2_SEC

```c
else {
    // 注意: send/recv是相对于自己进程的视角
    recv_ring = rte_ring_lookup(_PRI_2_SEC);  // 接收Primary发来的
    send_ring = rte_ring_lookup(_SEC_2_PRI);  // 发送给Primary的
    message_pool = rte_mempool_lookup(_MSG_POOL);
}
```

**设计巧妙之处:**

- Primary的`send_ring`对应Secondary的`recv_ring`
- 通过变量名的语义化,代码逻辑清晰

### 4.3 接收消息的工作线程

每个slave core上的线程执行出队操作:

```c
// main.c:56-72
static int lcore_recv(__rte_unused void *arg)
{
    unsigned lcore_id = rte_lcore_id();
    printf("Starting core %u\n", lcore_id);

    while (!quit) {
        void *msg;

        // 从Ring出队消息(指针)
        if (rte_ring_dequeue(recv_ring, &msg) < 0) {
            usleep(5);  // 没有消息,短暂休眠避免空转
            continue;
        }

        // 打印接收到的消息
        printf("core %u: Received '%s'\n", lcore_id, (char *)msg);

        // 重要!!! 将消息buffer归还给mempool
        rte_mempool_put(message_pool, msg);
    }

    return 0;
}
```

**关键点:**

- Ring传递的是**指针**(void *),不是数据本身
- 消息buffer在共享hugepage内存中
- 使用完必须`rte_mempool_put()`归还,否则泄漏

### 4.4 发送消息的命令处理

当在进程执行`send hello1`时,`cmd_send_parsed`函数执行入队逻辑:

```c
// mp_commands.c:11-25
void cmd_send_parsed(void *parsed_result, ...)
{
    void *msg = NULL;
    struct cmd_send_result *res = parsed_result;

    // 1. 从mempool获取一个buffer
    if (rte_mempool_get(message_pool, &msg) < 0)
        rte_panic("Failed to get message buffer\n");

    // 2. 拷贝用户输入的字符串到buffer
    strlcpy((char *)msg, res->message, STR_TOKEN_SIZE);

    // 3. 将buffer的指针入队到Ring
    if (rte_ring_enqueue(send_ring, msg) < 0) {
        printf("Failed to send message - message discarded\n");
        rte_mempool_put(message_pool, msg);  // 失败则归还
    }
}
```

**内存管理流程:**

```
┌──────────────────────────────────────────────────────┐
│ 1. rte_mempool_get()     → 获取buffer指针           │
│ 2. strlcpy()             → 填充数据到buffer          │
│ 3. rte_ring_enqueue()    → 发送buffer指针           │
│ 4. 对方rte_ring_dequeue()→ 接收buffer指针           │
│ 5. 使用buffer数据                                    │
│ 6. rte_mempool_put()     → 归还buffer到pool         │
└──────────────────────────────────────────────────────┘
```

### 4.5 主循环: 交互式命令行

```c
// main.c:113-123
// 启动工作核心监听消息
RTE_LCORE_FOREACH_WORKER(lcore_id) {
    rte_eal_remote_launch(lcore_recv, NULL, lcore_id);
}

// 主核心运行命令行界面
struct cmdline *cl = cmdline_stdin_new(simple_mp_ctx, "\nsimple_mp > ");
if (cl == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create cmdline instance\n");
cmdline_interact(cl);  // 阻塞等待用户输入
cmdline_stdin_exit(cl);

rte_eal_mp_wait_lcore();  // 等待所有核心退出
```

## 五、支持的命令

| 命令 | 功能 | 示例 |
|------|------|------|
| `send <message>` | 发送消息到另一个进程 | `send Hello World` |
| `help` | 显示帮助信息 | `help` |
| `quit` | 退出程序 | `quit` |

---

## 六、学习要点总结

### 6.1 双向通信模式
- 需要创建2个Ring队列(每个方向一个)
- Primary和Secondary对Ring的引用是相反的
- 变量命名要体现视角(send/recv)

### 6.2 Mempool的作用
- 不是传递数据本身,而是传递指向共享内存的**指针**
- 消息buffer在hugepage中,两个进程都能访问
- 零拷贝: 只传递指针,不拷贝数据

### 6.3 资源回收
- 接收方使用完消息后**必须**`rte_mempool_put()`归还
- 否则会导致内存泄漏,最终pool耗尽

### 6.4 Cmdline库
- DPDK提供的交互式命令行框架
- 支持命令解析、自动补全等功能
- 适合调试和原型开发

### 6.5 进程启动顺序
- **必须先启动Primary进程**,完成共享资源初始化
- Secondary进程启动时必须明确指定`--proc-type=secondary`
- Secondary只能查找(lookup)资源,不能创建(create)

### 6.6 与client_server_mp对比

| 特性 | simple_mp | client_server_mp |
|------|-----------|------------------|
| **复杂度** | ⭐ 简单 | ⭐⭐⭐ 复杂 |
| **通信模式** | 双向对等 | Server分发,Client转发 |
| **Ring数量** | 2个(双向) | N个(每Client一个) |
| **网络端口** | ❌ 不涉及 | ✅ 真实收发包 |
| **适用场景** | 学习基础通信 | 生产环境数据平面 |
| **交互方式** | 命令行 | 后台运行 |
| **代码行数** | ~130行 | ~800行 |
