# DPDK多进程编程实战教程

本目录包含3个递进式的DPDK多进程实验,帮助您从零开始掌握DPDK多进程编程。

## 📚 实验概述

| 实验 | 名称 | 难度 | 学习重点 |
|-----|------|------|----------|
| **实验1** | 基础Primary-Secondary | ⭐ | 进程角色、对象查找、单向通信 |
| **实验2** | Ring双向通信 | ⭐⭐ | 双向Ring、Ping-Pong、RTT测量 |
| **实验3** | Client-Server架构 | ⭐⭐⭐ | 负载均衡、多Client、实际应用 |

---

## 🚀 快速开始

### 方法1: 使用自动化脚本(推荐)

```bash
cd 7-multiprocess
sudo ./setup.sh
```

脚本会引导您完成:
- ✅ 环境检查(Hugepage)
- ✅ 编译所有实验
- ✅ 运行指定实验
- ✅ 清理共享内存

### 方法2: 手动编译和运行

```bash
# 1. 编译
mkdir build && cd build
cmake ..
make -j$(nproc)

# 2. 配置hugepage (如果需要)
sudo bash -c "echo 512 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"

# 3. 运行实验 (见下方各实验详细说明)
```

---

## 📖 实验详解

### 实验1: 基础Primary-Secondary示例

**学习目标:**
- 理解Primary和Secondary进程的区别
- 掌握`rte_mempool_create()` vs `rte_mempool_lookup()`
- 掌握`rte_ring_create()` vs `rte_ring_lookup()`
- 实现单向消息传递

**运行步骤:**

```bash
# 终端1: 启动Primary进程
sudo ./build/bin/mp_basic_primary -l 0 --no-huge

# 终端2: 启动Secondary进程
sudo ./build/bin/mp_basic_secondary -l 1 --proc-type=secondary --no-huge
```

**预期输出:**

Primary终端:
```
步骤1: Primary进程初始化EAL...
✓ EAL初始化成功 (进程类型: PRIMARY)

步骤2: 创建共享内存池 (名称: mp_basic_pool)...
✓ 内存池创建成功
  - 对象数量: 512
  - 对象大小: 64 字节
  - 可用对象: 512

步骤3: 创建共享Ring队列 (名称: mp_basic_ring)...
✓ Ring队列创建成功
  - Ring大小: 256

[Primary] 发送消息 #0: Hello from Primary #0
[Primary] 发送消息 #1: Hello from Primary #1
...
```

Secondary终端:
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
...
```

**关键代码分析:**

```c
// Primary进程: 创建共享对象
struct rte_mempool *mp = rte_mempool_create(
    MEMPOOL_NAME,  // 全局唯一名称
    NUM_MBUFS, OBJ_SIZE, ...);

struct rte_ring *ring = rte_ring_create(
    RING_NAME,     // 全局唯一名称
    RING_SIZE, ...);

// Secondary进程: 查找共享对象
struct rte_mempool *mp = rte_mempool_lookup(MEMPOOL_NAME);
struct rte_ring *ring = rte_ring_lookup(RING_NAME);
```

**思考题:**
1. 如果先启动Secondary会怎样?
2. Primary退出后Secondary还能运行吗?
3. 如何实现多个Secondary进程?

---

### 实验2: Ring双向通信示例

**学习目标:**
- 掌握双向Ring队列的创建和使用
- 实现Ping-Pong通信模式
- 测量往返时延(RTT)
- 理解Ring队列的性能特性

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
=== Ring通信示例 - Sender (Primary) ===

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
--------------------
```

Receiver终端:
```
=== Ring通信示例 - Receiver (Secondary) ===

查找双向Ring队列...
✓ Ring (Primary->Secondary) 查找成功
✓ Ring (Secondary->Primary) 查找成功

[Receiver] 接收 Ping #0
           内容: Ping #0 from Primary
[Receiver] 回复 Pong #0
```

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
       │                                  │
```

**关键代码分析:**

```c
// 创建双向Ring
ring_p2s = rte_ring_create("ring_primary_to_secondary", ...);
ring_s2p = rte_ring_create("ring_secondary_to_primary", ...);

// Sender: 发送Ping,接收Pong
rte_ring_enqueue(ring_p2s, ping_msg);
rte_ring_dequeue(ring_s2p, &pong_msg);

// Receiver: 接收Ping,发送Pong
rte_ring_dequeue(ring_p2s, &ping_msg);
rte_ring_enqueue(ring_s2p, pong_msg);
```

**性能测试:**
- RTT通常在几百微秒到几毫秒
- Ring队列是无锁的,性能很高
- 可以测试不同Ring大小对性能的影响

**思考题:**
1. 为什么需要两个Ring而不是一个?
2. 如何优化RTT?
3. 如果Ring满了会发生什么?

---

### 实验3: Client-Server架构

**学习目标:**
- 掌握实际的多进程架构设计
- 实现负载均衡(Round-Robin)
- 支持多个Client并行处理
- 理解生产环境的最佳实践

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
=== Client-Server架构 - Server (Primary) ===

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
------------------
```

Client终端:
```
=== Client-Server架构 - Client (Secondary) ===

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
--------------------
```

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
    │(Secondary 1)│ │(Secondary2)│ │(SecondaryN)│
    └─────────────┘ └────────────┘ └────────────┘
          │               │               │
          └───────────────┴───────────────┘
                    (并行处理)
```

**负载均衡策略:**

Server使用Round-Robin轮询分发:
```c
// 简单轮询
next_client = (next_client + 1) % num_clients;
rte_ring_enqueue_burst(client_rings[next_client], pkts, BURST_SIZE);
```

**扩展思考:**
1. 如何改成基于Hash的分发? (提示: 5元组哈希)
2. 如何支持Client动态加入/退出?
3. 如何监控各Client的负载?

---

## 🔧 常见问题

### Q1: 编译错误 "cannot find -ldpdk"

**解决:**
```bash
# 检查DPDK是否安装
pkg-config --modversion libdpdk

# 如果没有,安装DPDK
sudo apt-get install dpdk dpdk-dev  # Ubuntu/Debian
```

### Q2: 运行时错误 "Cannot init EAL"

**可能原因:**
1. 没有root权限 → 使用`sudo`
2. Hugepage未配置 → 运行`setup.sh`或使用`--no-huge`
3. 资源被占用 → 清理旧进程和共享内存

**解决:**
```bash
# 方法1: 使用--no-huge (测试时方便)
sudo ./build/bin/mp_basic_primary -l 0 --no-huge

# 方法2: 配置hugepage
sudo bash -c "echo 512 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"

# 方法3: 清理旧的共享内存
sudo rm -rf /dev/hugepages/rtemap_*
```

### Q3: Secondary找不到共享对象

**检查清单:**
- [ ] Primary进程是否已经启动?
- [ ] 对象名称是否一致?
- [ ] 是否使用了`--proc-type=secondary`?
- [ ] 是否在同一台机器上运行?

### Q4: Ring队列已满

**原因:** Client处理速度跟不上Server发送速度

**解决:**
```c
// 1. 增大Ring大小
#define RING_SIZE 4096  // 从2048增大到4096

// 2. 降低Server发送速率
usleep(5000);  // 从3ms增加到5ms

// 3. 优化Client处理逻辑
// - 批量处理
// - 减少打印
```

---

## 📊 性能优化建议

### 1. 使用批量操作

```c
// ❌ 不好: 逐个操作
for (int i = 0; i < n; i++) {
    rte_ring_enqueue(ring, objs[i]);
}

// ✅ 好: 批量操作
rte_ring_enqueue_burst(ring, objs, n, NULL);
```

### 2. 合理选择Ring标志位

```c
// 单生产者单消费者 (最快)
RING_F_SP_ENQ | RING_F_SC_DEQ

// 多生产者多消费者 (默认,最安全)
0
```

### 3. CPU亲和性绑定

```bash
# 绑定到指定CPU核心
taskset -c 0 ./server &
taskset -c 1 ./client -n 0 &
taskset -c 2 ./client -n 1 &
```

### 4. NUMA优化

```c
// 在创建对象时指定NUMA节点
int socket_id = rte_socket_id();
mp = rte_mempool_create(..., socket_id, ...);
ring = rte_ring_create(..., socket_id, ...);
```

---

## 📝 学习路径建议

### 初级 (第1周)
1. ✅ 完成实验1,理解基础概念
2. ✅ 阅读[lesson7-multiprocess.md](../lesson7-multiprocess.md)理论部分
3. ✅ 修改实验1,尝试多个Secondary进程

### 中级 (第2周)
1. ✅ 完成实验2,理解双向通信
2. ✅ 测量不同Ring大小下的RTT
3. ✅ 实现基于Hash的消息路由

### 高级 (第3周)
1. ✅ 完成实验3,理解生产架构
2. ✅ 修改为基于5元组Hash的分发
3. ✅ 实现Client动态管理(心跳机制)

---

## 📚 参考资料

- **DPDK官方文档:** https://doc.dpdk.org/guides/prog_guide/multi_proc_support.html
- **示例源码:** https://github.com/DPDK/dpdk/tree/main/examples/multi_process
- **本课程文档:** [lesson7-multiprocess.md](../lesson7-multiprocess.md)

---

## 💡 进阶挑战

完成基础实验后,尝试这些挑战:

1. **实现流表管理:** 在Client中维护5元组流表,统计每个流的包数
2. **添加控制平面:** 使用`rte_mp`实现进程间控制消息
3. **性能测试:** 测试不同配置下的吞吐量和延迟
4. **实现QoS:** 为不同优先级的流分配不同的Client
5. **故障恢复:** 实现Client崩溃后的自动重启

---

**祝学习愉快! 🚀**

如有问题,请参考[lesson7-multiprocess.md](../lesson7-multiprocess.md)或在GitHub提Issue。
