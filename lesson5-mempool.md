# Lesson 5: DPDK 内存池（Mempool）详解

## 课程简介

内存池（Mempool）是 DPDK 中用于高性能内存管理的核心组件。本课程将深入讲解内存池的原理、使用方法和最佳实践。

**学习目标：**
- 理解为什么需要内存池
- 掌握内存池的创建、使用和释放
- 理解内存池的缓存机制和标志位
- 学会内存池的性能优化技巧

**前置知识：**
- 完成 Lesson 1-4
- 理解内存管理基础
- 了解多线程编程

---

## 一、为什么需要内存池？

### 1.1 传统内存管理的问题

在网络数据包处理中，频繁使用传统的 `malloc/free` 会导致严重的性能问题：

```c
// 传统方式：每次都要 malloc/free
for (int i = 0; i < 1000000; i++) {
    char *buffer = malloc(2048);  // ❌ 系统调用，性能低
    // 处理数据包...
    free(buffer);                 // ❌ 可能产生内存碎片
}
```

**主要问题：**

| 问题 | 说明 | 影响 |
|------|------|------|
| **系统调用开销** | malloc/free 需要进入内核态 | 性能显著降低 |
| **内存碎片** | 频繁分配释放导致碎片化 | 可用内存减少 |
| **不可预测的延迟** | 分配时间不确定 | 无法满足实时性要求 |
| **线程竞争** | 多线程争抢内存分配锁 | 扩展性差 |

### 1.2 内存池的优势

内存池采用预分配 + 池化管理的方式：

```
传统方式：                  内存池方式：
┌─────────────┐            ┌──────────────────┐
│  应用程序   │            │    应用程序      │
└──────┬──────┘            └────────┬─────────┘
       │ malloc/free                │ get/put
       ↓                            ↓
┌─────────────┐            ┌──────────────────┐
│   操作系统  │            │   内存池(用户态)  │
│  (内核态)   │            │   预分配内存      │
└─────────────┘            └──────────────────┘
   性能低                      性能高
```

**内存池的优势：**

1. ✅ **预分配内存** - 启动时一次性分配，避免运行时开销
2. ✅ **无锁或轻量级锁** - 使用 per-core 缓存减少竞争
3. ✅ **固定大小对象** - 避免内存碎片
4. ✅ **O(1) 时间复杂度** - 获取和释放操作都是常数时间
5. ✅ **NUMA 感知** - 分配本地内存，提高访问速度

### 1.3 DPDK 中的内存池应用场景

| 应用场景 | 说明 | 对象大小 |
|---------|------|---------|
| **数据包接收** | 存储接收到的网络数据包 | 2048 字节（mbuf） |
| **会话表管理** | TCP/UDP 连接跟踪 | 自定义结构体 |
| **流表** | OpenFlow 流表项 | 自定义结构体 |
| **消息队列** | 进程间通信 | 消息结构体 |
| **对象缓存** | 任何需要频繁分配/释放的对象 | 任意大小 |

---

## 二、内存池的架构设计

### 2.1 内存池结构

```
内存池架构：
┌─────────────────────────────────────────────────────┐
│                   Mempool Header                    │
│  - name: 内存池名称                                 │
│  - size: 总对象数量                                 │
│  - elt_size: 对象大小                               │
│  - cache_size: 每个核心的缓存大小                   │
│  - private_data_size: 私有数据大小                  │
├─────────────────────────────────────────────────────┤
│               Per-core Cache (Optional)             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ Core 0   │  │ Core 1   │  │ Core 2   │          │
│  │ 本地缓存 │  │ 本地缓存 │  │ 本地缓存 │          │
│  └──────────┘  └──────────┘  └──────────┘          │
│       ↓              ↓              ↓               │
├─────────────────────────────────────────────────────┤
│                     Ring Buffer                     │
│              (共享的环形队列)                       │
│  ┌─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐                │
│  │o│o│o│o│o│o│o│o│o│o│o│o│o│o│o│o│                │
│  └─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘                │
│       ↓  ↓  ↓  ↓  ↓  ↓  ↓  ↓                       │
├─────────────────────────────────────────────────────┤
│                   Memory Objects                    │
│  ┌───────┐  ┌───────┐  ┌───────┐  ┌───────┐        │
│  │Object │  │Object │  │Object │  │Object │  ...   │
│  │  #1   │  │  #2   │  │  #3   │  │  #4   │        │
│  └───────┘  └───────┘  └───────┘  └───────┘        │
└─────────────────────────────────────────────────────┘
```

### 2.2 获取/归还对象的流程

**获取对象（Get）：**
```
1. 检查本地缓存 (per-core cache)
   ├─ 如果有空闲对象 → 直接返回（无锁，最快）
   └─ 如果缓存为空 ↓
2. 从共享环形队列批量获取对象到本地缓存
   └─ 使用 CAS 操作（轻量级同步）
3. 从本地缓存返回对象给应用
```

**归还对象（Put）：**
```
1. 放入本地缓存 (per-core cache)
   ├─ 如果缓存未满 → 直接放入（无锁，最快）
   └─ 如果缓存已满 ↓
2. 批量归还部分对象到共享环形队列
   └─ 使用 CAS 操作（轻量级同步）
3. 将当前对象放入本地缓存
```

---

## 三、内存池的基本操作

### 3.1 创建内存池

#### 函数原型

```c
struct rte_mempool * rte_mempool_create(
    const char *name,              // 内存池名称（全局唯一）
    unsigned n,                    // 对象数量
    unsigned elt_size,             // 每个对象的大小（字节）
    unsigned cache_size,           // 每个核心的本地缓存大小
    unsigned private_data_size,    // 私有数据大小
    rte_mempool_ctor_t *mp_init,   // 内存池初始化回调
    void *mp_init_arg,             // 初始化回调参数
    rte_mempool_obj_cb_t *obj_init,// 对象初始化回调
    void *obj_init_arg,            // 对象初始化回调参数
    int socket_id,                 // NUMA 节点 ID
    unsigned flags                 // 标志位
);
```

#### 参数详解

| 参数 | 类型 | 说明 | 推荐值 |
|------|------|------|--------|
| **name** | `const char *` | 内存池名称，必须全局唯一 | "my_pool" |
| **n** | `unsigned` | 内存池中对象的数量 | 1024, 2048, 4096 |
| **elt_size** | `unsigned` | 每个对象的大小（字节） | 256, 512, 2048 |
| **cache_size** | `unsigned` | 每个 CPU 核心的本地缓存大小 | 0-512（推荐 0 或 256） |
| **private_data_size** | `unsigned` | 每个内存池的私有数据大小 | 通常为 0 |
| **mp_init** | 回调函数 | 内存池创建时的初始化回调 | 通常为 NULL |
| **mp_init_arg** | `void *` | 初始化回调的参数 | 通常为 NULL |
| **obj_init** | 回调函数 | 每个对象的初始化回调 | 通常为 NULL |
| **obj_init_arg** | `void *` | 对象初始化回调的参数 | 通常为 NULL |
| **socket_id** | `int` | NUMA 节点 ID | `SOCKET_ID_ANY` 或 `rte_socket_id()` |
| **flags** | `unsigned` | 标志位（见下表） | 0（默认多生产者多消费者） |

#### 标志位详解

| 标志位 | 说明 | 使用场景 | 性能影响 |
|--------|------|---------|---------|
| **0（默认）** | 多生产者多消费者（MP-MC） | 多线程环境 | 标准性能 |
| **RTE_MEMPOOL_F_SP_PUT** | 单生产者（Single Producer） | 只有一个线程 put 对象 | 提升 put 性能 20-30% |
| **RTE_MEMPOOL_F_SC_GET** | 单消费者（Single Consumer） | 只有一个线程 get 对象 | 提升 get 性能 20-30% |
| **RTE_MEMPOOL_F_NO_CACHE_ALIGN** | 不按缓存行对齐 | 节省内存，但可能假共享 | 可能降低性能 |
| **RTE_MEMPOOL_F_NO_SPREAD** | 不在内存通道间分散 | 不需要 NUMA 优化 | NUMA 系统性能下降 |

**标志位组合示例：**

```c
// 单生产者单消费者（最高性能）
flags = RTE_MEMPOOL_F_SP_PUT | RTE_MEMPOOL_F_SC_GET;

// 单生产者多消费者
flags = RTE_MEMPOOL_F_SP_PUT;

// 多生产者单消费者
flags = RTE_MEMPOOL_F_SC_GET;

// 默认多生产者多消费者
flags = 0;
```

#### 代码示例

查看完整实现：[main.c:75-91](5-mempool_usage/main.c#L75-L91)

```c
#define MEMPOOL_SIZE (1024)       // 内存池大小
#define MEMPOOL_ELT_SIZE (256)    // 对象大小

// 创建基本内存池
struct rte_mempool *mp = rte_mempool_create(
    "test_mempool_basic",        // 内存池名称
    MEMPOOL_SIZE,                 // 对象数量：1024
    MEMPOOL_ELT_SIZE,             // 对象大小：256 字节
    0,                            // cache_size：0（无本地缓存）
    0,                            // private_data_size：0
    NULL, NULL,                   // 内存池初始化回调（不需要）
    NULL, NULL,                   // 对象初始化回调（不需要）
    SOCKET_ID_ANY,                // 任意 NUMA 节点
    0);                           // 标志位：默认（MP-MC）

if (mp == NULL) {
    printf("ERROR: create mempool failed\n");
    return -1;
}

printf("内存池创建成功！\n");
```

### 3.2 从内存池获取对象（Get）

DPDK 提供了三个获取对象的 API：

#### 3.2.1 rte_mempool_get - 获取单个对象

```c
// 获取单个对象（常用）
static inline int rte_mempool_get(
    struct rte_mempool *mp,    // 内存池指针
    void **obj_p               // 输出参数：返回对象指针
);
```

**使用示例：**
```c
void *obj;
if (rte_mempool_get(mp, &obj) < 0) {
    printf("ERROR: 获取对象失败\n");
    return -1;
}
printf("成功获取对象：%p\n", obj);
```

#### 3.2.2 rte_mempool_get_bulk - 批量获取对象

```c
// 批量获取对象（高性能）
static inline int rte_mempool_get_bulk(
    struct rte_mempool *mp,    // 内存池指针
    void **obj_table,          // 对象指针数组
    unsigned int n             // 要获取的对象数量
);
```

**使用示例：**

查看完整实现：[main.c:109-120](5-mempool_usage/main.c#L109-L120)

```c
// 批量获取 2 个对象
void *objects[2];
if (rte_mempool_get_bulk(mp, objects, 2) < 0) {
    printf("ERROR: bulk get objects failed\n");
    return -1;
}

void *obj1 = objects[0];
void *obj2 = objects[1];
printf("成功获取 2 个对象：%p, %p\n", obj1, obj2);
```

#### 3.2.3 rte_mempool_generic_get - 通用获取函数

```c
// 通用获取函数（最灵活）
int rte_mempool_generic_get(
    struct rte_mempool *mp,         // 内存池指针
    void **obj_table,               // 对象指针数组
    unsigned int n,                 // 要获取的对象数量
    struct rte_mempool_cache *cache // 缓存指针（通常为 NULL）
);
```

**使用示例：**

查看完整实现：[main.c:94-102](5-mempool_usage/main.c#L94-L102)

```c
void *obj;
if (rte_mempool_generic_get(mp, &obj, 1, NULL) < 0) {
    printf("ERROR: get object failed\n");
    return -1;
}
printf("对象地址: %p\n", obj);
```

**三个函数的对比：**

| 函数 | 使用场景 | 性能 | 灵活性 |
|------|---------|------|--------|
| `rte_mempool_get` | 获取单个对象（最常用） | 高 | 低 |
| `rte_mempool_get_bulk` | 批量获取（推荐） | 最高 | 中 |
| `rte_mempool_generic_get` | 需要自定义缓存 | 高 | 最高 |

### 3.3 归还对象到内存池（Put）

#### 3.3.1 rte_mempool_put - 归还单个对象

```c
// 归还单个对象（常用）
static inline void rte_mempool_put(
    struct rte_mempool *mp,    // 内存池指针
    void *obj                  // 要归还的对象指针
);
```

**使用示例：**

查看完整实现：[main.c:104-107](5-mempool_usage/main.c#L104-L107)

```c
// 归还对象到内存池
rte_mempool_put(mp, obj);
printf("对象已归还\n");
```

#### 3.3.2 rte_mempool_put_bulk - 批量归还对象

```c
// 批量归还对象（高性能）
static inline void rte_mempool_put_bulk(
    struct rte_mempool *mp,    // 内存池指针
    void * const *obj_table,   // 对象指针数组
    unsigned int n             // 要归还的对象数量
);
```

**使用示例：**

查看完整实现：[main.c:122-126](5-mempool_usage/main.c#L122-L126)

```c
// 批量归还 2 个对象
void *objects[2] = {obj1, obj2};
rte_mempool_put_bulk(mp, objects, 2);
printf("成功归还 2 个对象\n");
```

#### 3.3.3 rte_mempool_generic_put - 通用归还函数

```c
// 通用归还函数（最灵活）
void rte_mempool_generic_put(
    struct rte_mempool *mp,         // 内存池指针
    void * const *obj_table,        // 对象指针数组
    unsigned int n,                 // 要归还的对象数量
    struct rte_mempool_cache *cache // 缓存指针（通常为 NULL）
);
```

**使用示例：**

查看完整实现：[main.c:105](5-mempool_usage/main.c#L105)

```c
rte_mempool_generic_put(mp, &obj, 1, NULL);
```

### 3.4 查询内存池状态

#### 3.4.1 获取可用对象数量

```c
// 获取内存池中可用（空闲）对象的数量
unsigned int rte_mempool_avail_count(const struct rte_mempool *mp);
```

#### 3.4.2 获取正在使用的对象数量

```c
// 获取内存池中正在使用的对象数量
unsigned int rte_mempool_in_use_count(const struct rte_mempool *mp);
```

**使用示例：**

查看完整实现：[main.c:89-90](5-mempool_usage/main.c#L89-L90)

```c
printf("可用对象数量: %d, 使用中对象数量: %d\n",
       rte_mempool_avail_count(mp),
       rte_mempool_in_use_count(mp));
```

### 3.5 释放内存池

```c
// 释放内存池及其所有资源
void rte_mempool_free(struct rte_mempool *mp);
```

**重要提示：**
- 释放内存池前，确保所有对象都已归还
- 释放后不能再使用该内存池
- 内存池名称可以被重新使用

**使用示例：**

查看完整实现：[main.c:128-129](5-mempool_usage/main.c#L128-L129)

```c
rte_mempool_free(mp);
printf("内存池已释放\n");
```

---

## 四、完整示例代码解析

查看完整实现：[main.c](5-mempool_usage/main.c)

### 4.1 基本使用示例

查看完整实现：[main.c:64-132](5-mempool_usage/main.c#L64-L132)

```c
static int test_mempool_basic()
{
    void *obj, *obj2;

    // 1. 创建内存池
    printf("1. 创建内存池\n");
    struct rte_mempool *mp = rte_mempool_create(
        "test_mempool_basic",
        MEMPOOL_SIZE,
        MEMPOOL_ELT_SIZE,
        0, 0,
        NULL, NULL,
        NULL, NULL,
        SOCKET_ID_ANY, 0);

    if (mp == NULL) {
        printf("ERROR: create mempool failed\n");
        return -1;
    }

    // 查看初始状态
    printf("可用对象数量: %d, 使用中对象数量: %d\n",
           rte_mempool_avail_count(mp),
           rte_mempool_in_use_count(mp));
    printf("-----------------------------------------\n");

    // 2. 获取一个对象
    printf("2. 获取一个对象\n");
    if (rte_mempool_generic_get(mp, &obj, 1, NULL) < 0) {
        printf("ERROR: get object failed\n");
        return -1;
    }

    printf("对象地址: %p, 可用对象数量: %d, 使用中对象数量: %d\n",
           obj, rte_mempool_avail_count(mp),
           rte_mempool_in_use_count(mp));
    printf("-----------------------------------------\n");

    // 3. 将对象放回内存池
    printf("3. 将对象放回内存池\n");
    rte_mempool_generic_put(mp, &obj, 1, NULL);
    printf("对象地址: %p, 可用对象数量: %d, 使用中对象数量: %d\n",
           obj, rte_mempool_avail_count(mp),
           rte_mempool_in_use_count(mp));
    printf("-----------------------------------------\n");

    // 4. 批量获取两个对象
    printf("4. 批量获取两个对象后获取对象数量\n");
    void *objects[2];
    if (rte_mempool_get_bulk(mp, objects, 2) < 0) {
        printf("ERROR: bulk get objects failed\n");
        return -1;
    }
    obj = objects[0];
    obj2 = objects[1];
    printf("对象地址: %p, %p, 可用对象数量: %d, 使用中对象数量: %d\n",
           obj, obj2, rte_mempool_avail_count(mp),
           rte_mempool_in_use_count(mp));
    printf("-----------------------------------------\n");

    // 5. 批量将两个对象归还给内存池
    printf("5. 批量将两个对象归还给内存池\n");
    rte_mempool_put_bulk(mp, objects, 2);
    printf("可用对象数量: %d, 使用中对象数量: %d\n",
           rte_mempool_avail_count(mp),
           rte_mempool_in_use_count(mp));
    printf("-----------------------------------------\n");

    // 6. 释放内存池
    printf("6. 释放内存池\n");
    rte_mempool_free(mp);

    return 0;
}
```

### 4.2 同名内存池测试

查看完整实现：[main.c:22-62](5-mempool_usage/main.c#L22-L62)

```c
// 测试同名内存池是否可以创建两次
static int test_mempool_same_name_twice_creation(void)
{
    struct rte_mempool *mp_tc, *mp_tc2;
    char *mempool_name = "1234";

    // 第一次创建
    mp_tc = rte_mempool_create(mempool_name, MEMPOOL_SIZE,
        MEMPOOL_ELT_SIZE, 0, 0,
        NULL, NULL,
        NULL, NULL,
        SOCKET_ID_ANY, 0);

    if (mp_tc == NULL) {
        printf("INFO: first mempool %s created failed\n", mempool_name);
        return -1;
    }

    // 第二次创建同名内存池
    mp_tc2 = rte_mempool_create(mempool_name, MEMPOOL_SIZE,
        MEMPOOL_ELT_SIZE, 0, 0,
        NULL, NULL,
        NULL, NULL,
        SOCKET_ID_ANY, 0);

    if (mp_tc2 == NULL) {
        printf("ERROR: second mempool %s created failed\n", mempool_name);
        return -1;
    }

    // 如果第二次创建成功，说明返回的是同一个内存池
    if (mp_tc2 != NULL) {
        printf("INFO: 第二次创建返回了已存在的内存池\n");
        printf("INFO: mp_tc = %p, mp_tc2 = %p, 相同: %s\n",
               mp_tc, mp_tc2, (mp_tc == mp_tc2) ? "是" : "否");

        // 只需要释放一次
        rte_mempool_free(mp_tc);
        printf("INFO: mempool created successfully, let's free it\n");
    }

    return 0;
}
```

**测试结论：**
- ✅ DPDK 允许创建同名内存池
- ✅ 第二次创建会返回已存在的内存池（查找功能）
- ✅ 两个指针指向同一个内存池对象
- ⚠️ 只需要释放一次

### 4.3 程序输出示例

```
1. 创建内存池
可用对象数量: 1024, 使用中对象数量: 0
-----------------------------------------
2. 获取一个对象
对象地址: 0x7f8e40000040, 可用对象数量: 1023, 使用中对象数量: 1
-----------------------------------------
3. 将对象放回内存池
对象地址: 0x7f8e40000040, 可用对象数量: 1024, 使用中对象数量: 0
-----------------------------------------
4. 批量获取两个对象后获取对象数量
对象地址: 0x7f8e40000040, 0x7f8e40000140, 可用对象数量: 1022, 使用中对象数量: 2
-----------------------------------------
5. 批量将两个对象归还给内存池
可用对象数量: 1024, 使用中对象数量: 0
-----------------------------------------
6. 释放内存池
```

---

## 五、进阶话题

### 5.1 Per-Core Cache（本地缓存）详解

#### 5.1.1 什么是 Per-Core Cache？

Per-Core Cache 是每个 CPU 核心独有的本地缓存，用于减少访问共享环形队列的竞争。

```
没有 Cache 的情况：
┌──────────┐  ┌──────────┐  ┌──────────┐
│  Core 0  │  │  Core 1  │  │  Core 2  │
└────┬─────┘  └────┬─────┘  └────┬─────┘
     │             │             │
     └─────────────┼─────────────┘
                   ↓
           ┌───────────────┐
           │  Ring Buffer  │ ← 所有核心竞争
           │  (需要 CAS)   │
           └───────────────┘

有 Cache 的情况：
┌──────────┐  ┌──────────┐  ┌──────────┐
│  Core 0  │  │  Core 1  │  │  Core 2  │
│ ┌──────┐ │  │ ┌──────┐ │  │ ┌──────┐ │
│ │Cache │ │  │ │Cache │ │  │ │Cache │ │ ← 本地访问
│ └───┬──┘ │  │ └───┬──┘ │  │ └───┬──┘ │    (无锁)
└─────┼────┘  └─────┼────┘  └─────┼────┘
      └─────────────┼──────────────┘
                    ↓
            ┌───────────────┐
            │  Ring Buffer  │ ← 批量操作
            │  (减少竞争)   │    (偶尔)
            └───────────────┘
```

#### 5.1.2 Cache Size 的选择

| Cache Size | 适用场景 | 优点 | 缺点 |
|-----------|---------|------|------|
| **0** | 单核或低频访问 | 节省内存 | 高竞争 |
| **32-64** | 中等流量 | 平衡性能和内存 | - |
| **128-256** | 高流量 | 最高性能 | 占用更多内存 |
| **512** | 极高流量 | 极致性能 | 大量内存占用 |

**计算公式：**
```
总内存占用 = n × elt_size + (cache_size × 核心数) × elt_size
```

**示例：**
```c
// 高性能配置（推荐）
struct rte_mempool *mp_high_perf = rte_mempool_create(
    "high_perf_pool",
    8192,        // 8192 个对象
    2048,        // 2KB 每个对象
    256,         // 256 个对象的本地缓存
    0,
    NULL, NULL, NULL, NULL,
    SOCKET_ID_ANY, 0);

// 低内存配置
struct rte_mempool *mp_low_mem = rte_mempool_create(
    "low_mem_pool",
    1024,        // 1024 个对象
    256,         // 256 字节每个对象
    0,           // 无本地缓存
    0,
    NULL, NULL, NULL, NULL,
    SOCKET_ID_ANY, 0);
```

### 5.2 内存池操作模式对比

DPDK 根据标志位选择不同的底层实现：

查看完整实现：[main.c](5-mempool_usage/main.c)（参考文档注释）

```c
// 在 rte_mempool_create_empty 函数中
if ((flags & RTE_MEMPOOL_F_SP_PUT) && (flags & RTE_MEMPOOL_F_SC_GET))
    ret = rte_mempool_set_ops_byname(mp, "ring_sp_sc", NULL);
else if (flags & RTE_MEMPOOL_F_SP_PUT)
    ret = rte_mempool_set_ops_byname(mp, "ring_sp_mc", NULL);
else if (flags & RTE_MEMPOOL_F_SC_GET)
    ret = rte_mempool_set_ops_byname(mp, "ring_mp_sc", NULL);
else
    ret = rte_mempool_set_ops_byname(mp, "ring_mp_mc", NULL);
```

**四种操作模式：**

| 模式 | 生产者 | 消费者 | 实现 | 性能 | 使用场景 |
|------|--------|--------|------|------|---------|
| **SP-SC** | 单个 | 单个 | `ring_sp_sc` | 最高 | 单线程生产-单线程消费 |
| **SP-MC** | 单个 | 多个 | `ring_sp_mc` | 高 | 单线程生产-多线程消费 |
| **MP-SC** | 多个 | 单个 | `ring_mp_sc` | 高 | 多线程生产-单线程消费 |
| **MP-MC** | 多个 | 多个 | `ring_mp_mc` | 标准 | 通用场景（默认） |

**性能提升示例：**
```c
// 场景：单个接收线程，多个处理线程
struct rte_mempool *mp = rte_mempool_create(
    "sp_mc_pool",
    4096, 2048, 256, 0,
    NULL, NULL, NULL, NULL,
    SOCKET_ID_ANY,
    RTE_MEMPOOL_F_SP_PUT);  // 单生产者模式，提升 20-30% 性能
```

### 5.3 rte_mempool_create vs rte_mempool_create_empty

| 函数 | 特点 | 使用场景 |
|------|------|---------|
| **rte_mempool_create** | 一步到位创建内存池 | 简单场景，快速创建 |
| **rte_mempool_create_empty** | 创建空壳，需要手动填充 | 需要自定义内存分配策略 |

**rte_mempool_create_empty 使用流程：**
```c
// 1. 创建空内存池
struct rte_mempool *mp = rte_mempool_create_empty(
    "custom_pool", 1024, 256, 256, 0,
    SOCKET_ID_ANY, 0);

// 2. 设置操作函数（如自定义的内存分配器）
rte_mempool_set_ops_byname(mp, "my_custom_ops", NULL);

// 3. 分配内存
rte_mempool_populate_default(mp);
```

### 5.4 NUMA 优化

在 NUMA 系统中，访问本地内存比远程内存快 2-3 倍。

```c
// 获取当前线程运行的 NUMA 节点
int socket_id = rte_socket_id();

// 在本地 NUMA 节点创建内存池
struct rte_mempool *mp_local = rte_mempool_create(
    "numa_local_pool",
    4096, 2048, 256, 0,
    NULL, NULL, NULL, NULL,
    socket_id,  // 使用本地 socket
    0);

// 如果需要跨 NUMA 节点，使用 SOCKET_ID_ANY
struct rte_mempool *mp_any = rte_mempool_create(
    "numa_any_pool",
    4096, 2048, 256, 0,
    NULL, NULL, NULL, NULL,
    SOCKET_ID_ANY,  // 任意节点
    0);
```

---

## 六、性能优化技巧

### 6.1 批量操作

```c
// ❌ 不好：逐个获取/归还
for (int i = 0; i < 32; i++) {
    void *obj;
    rte_mempool_get(mp, &obj);
    // 处理对象...
    rte_mempool_put(mp, obj);
}

// ✅ 好：批量操作
void *objs[32];
rte_mempool_get_bulk(mp, objs, 32);
for (int i = 0; i < 32; i++) {
    // 处理对象...
}
rte_mempool_put_bulk(mp, objs, 32);
```

**性能提升：** 批量操作可以提升 2-5 倍性能！

### 6.2 合理设置缓存大小

```c
// 根据每个核心的预期并发对象数量设置缓存
// 经验公式：cache_size = 每秒处理包数 / 1000000 * 256

// 低流量（< 1 Mpps）
cache_size = 0 或 32;

// 中等流量（1-10 Mpps）
cache_size = 128 或 256;

// 高流量（> 10 Mpps）
cache_size = 256 或 512;
```

### 6.3 对象大小对齐

```c
// 对象大小应该是 64 字节（缓存行大小）的倍数
#define CACHE_LINE_SIZE 64

// ❌ 不好：非对齐
elt_size = 100;  // 可能导致假共享

// ✅ 好：对齐到缓存行
elt_size = 128;  // 64 的倍数，避免假共享
```

### 6.4 预分配足够的对象

```c
// 计算需要的对象数量
// 公式：n = 每个队列的描述符数量 * 队列数量 * 端口数量 * 安全系数

// 示例：2 个端口，每个 2 个队列，每个队列 1024 个描述符
int num_objects = 1024 * 2 * 2 * 2;  // 8192

struct rte_mempool *mp = rte_mempool_create(
    "packet_pool",
    num_objects,  // 预分配足够多
    2048, 256, 0,
    NULL, NULL, NULL, NULL,
    SOCKET_ID_ANY, 0);
```

---

## 七、常见问题与解决方案

### 问题 1：内存池创建失败

**症状：**
```
ERROR: create mempool failed
```

**可能原因：**
1. 大页内存不足
2. 内存池名称已存在（如果不是查找已有的）
3. 对象数量或大小不合理

**解决方案：**
```bash
# 检查大页内存
cat /proc/meminfo | grep Huge

# 增加大页内存
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# 或者在启动时指定
./your_app --huge-dir=/mnt/huge
```

### 问题 2：获取对象失败

**症状：**
```
ERROR: get object failed
rte_mempool_get() returned -ENOENT
```

**原因：** 内存池中所有对象都被占用

**解决方案：**
```c
// 1. 确保及时归还对象
rte_mempool_put(mp, obj);

// 2. 增加内存池大小
mp = rte_mempool_create(name, 2048, ...);  // 从 1024 增加到 2048

// 3. 检查是否有内存泄漏
printf("可用: %d, 使用中: %d\n",
       rte_mempool_avail_count(mp),
       rte_mempool_in_use_count(mp));
```

### 问题 3：性能不如预期

**排查步骤：**

1. **检查缓存配置**
   ```c
   // 尝试增加缓存大小
   cache_size = 256;  // 从 0 增加
   ```

2. **使用单生产者/消费者模式**
   ```c
   // 如果确定只有单线程访问
   flags = RTE_MEMPOOL_F_SP_PUT | RTE_MEMPOOL_F_SC_GET;
   ```

3. **使用批量操作**
   ```c
   // 使用 get_bulk / put_bulk 代替单个操作
   rte_mempool_get_bulk(mp, objs, 32);
   ```

4. **NUMA 优化**
   ```c
   // 使用本地 NUMA 节点
   socket_id = rte_socket_id();
   ```

---

## 八、编译和运行

### 8.1 编译项目

```bash
# 在项目根目录
cd build
cmake ..
make

# 可执行文件生成在 bin 目录
ls -lh bin/mempool_usage
```

### 8.2 运行程序

```bash
# 基本运行
sudo ./bin/mempool_usage -l 0

# 查看详细日志
sudo ./bin/mempool_usage -l 0 --log-level=8
```

### 8.3 预期输出

```
INFO: first mempool 1234 created successfully
INFO: 第二次创建返回了已存在的内存池
INFO: mp_tc = 0x7f8e40000000, mp_tc2 = 0x7f8e40000000, 相同: 是
INFO: mempool created successfully, let's free it

1. 创建内存池
可用对象数量: 1024, 使用中对象数量: 0
-----------------------------------------
2. 获取一个对象
对象地址: 0x7f8e40000040, 可用对象数量: 1023, 使用中对象数量: 1
-----------------------------------------
3. 将对象放回内存池
对象地址: 0x7f8e40000040, 可用对象数量: 1024, 使用中对象数量: 0
-----------------------------------------
4. 批量获取两个对象后获取对象数量
对象地址: 0x7f8e40000040, 0x7f8e40000140, 可用对象数量: 1022, 使用中对象数量: 2
-----------------------------------------
5. 批量将两个对象归还给内存池
可用对象数量: 1024, 使用中对象数量: 0
-----------------------------------------
6. 释放内存池
EAL cleanup completed
```

---

## 九、实战应用场景

### 9.1 数据包接收场景

```c
// 为网络数据包创建内存池
struct rte_mempool *pkt_pool = rte_pktmbuf_pool_create(
    "pkt_pool",
    NUM_MBUFS * nb_ports,  // 足够的 mbuf
    MBUF_CACHE_SIZE,       // 256
    0,
    RTE_MBUF_DEFAULT_BUF_SIZE,
    rte_socket_id());

// 接收数据包
struct rte_mbuf *pkts[BURST_SIZE];
uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, pkts, BURST_SIZE);

// 处理数据包...

// 释放数据包（归还到内存池）
for (int i = 0; i < nb_rx; i++) {
    rte_pktmbuf_free(pkts[i]);  // 内部调用 rte_mempool_put
}
```

### 9.2 会话表管理

```c
// 会话结构体
struct flow_entry {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint64_t packet_count;
    // ... 其他字段
};

// 创建会话表内存池
struct rte_mempool *flow_pool = rte_mempool_create(
    "flow_pool",
    65536,                      // 支持 64K 并发流
    sizeof(struct flow_entry),  // 会话结构体大小
    256, 0,
    NULL, NULL, NULL, NULL,
    SOCKET_ID_ANY,
    RTE_MEMPOOL_F_SP_PUT);      // 单生产者优化

// 创建新会话
struct flow_entry *flow;
if (rte_mempool_get(flow_pool, (void **)&flow) == 0) {
    flow->src_ip = packet_src_ip;
    flow->dst_ip = packet_dst_ip;
    // 初始化其他字段...

    // 添加到哈希表
    rte_hash_add_key_data(flow_table, &key, flow);
}

// 删除会话
rte_hash_del_key(flow_table, &key);
rte_mempool_put(flow_pool, flow);
```

---

## 十、学习资源

### 官方文档
- [DPDK Mempool Library](https://doc.dpdk.org/guides/prog_guide/mempool_lib.html)
- [rte_mempool.h API Reference](https://doc.dpdk.org/api/rte__mempool_8h.html)
- [DPDK Sample Application - rte_mempool](https://doc.dpdk.org/guides/sample_app_ug/)

### 推荐阅读
- **DPDK Programmer's Guide** - Mempool Library 章节
- **Intel DPDK Getting Started Guide**
- **DPDK Performance Optimization Guide**

### 下一步学习
- **Lesson 6：** Ring 环形队列
- **Lesson 7：** 多进程通信
- **Lesson 8：** Hash 表和查找

---

## 十一、总结

本课程详细讲解了 DPDK 内存池的核心技术：

### 关键知识点

1. ✅ **内存池的优势**
   - 预分配避免运行时开销
   - Per-core 缓存减少锁竞争
   - O(1) 时间复杂度的操作

2. ✅ **基本操作**
   - `rte_mempool_create()` - 创建内存池
   - `rte_mempool_get()` / `get_bulk()` - 获取对象
   - `rte_mempool_put()` / `put_bulk()` - 归还对象
   - `rte_mempool_avail_count()` - 查询状态

3. ✅ **性能优化**
   - 使用批量操作提升 2-5 倍性能
   - 合理设置 cache_size
   - 选择合适的操作模式（SP/SC）
   - NUMA 亲和性优化

4. ✅ **实战技巧**
   - 数据包接收和处理
   - 会话表管理
   - 对象池化管理

### 实践建议

1. **从简单开始** - 先使用默认参数创建内存池
2. **性能测试** - 对比不同配置的性能差异
3. **监控状态** - 定期检查内存池使用情况
4. **及时归还** - 避免对象泄漏

内存池是 DPDK 高性能的基石之一，掌握它对后续学习至关重要！🚀

---

**相关代码文件：**
- 完整源代码：[main.c](5-mempool_usage/main.c)
- 构建配置：[CMakeLists.txt](5-mempool_usage/CMakeLists.txt)
