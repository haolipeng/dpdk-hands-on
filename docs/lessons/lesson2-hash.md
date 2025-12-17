# DPDK Hash表使用教程（第2课）

## 课程概述

Hash表是DPDK中最常用的数据结构之一，在网络应用中广泛用于会话管理、流量统计、路由查找等场景。本课程通过一个网络流量会话表的例子，学习DPDK Hash表的基本操作。

**课程目标：**
- 理解Hash表在网络应用中的作用
- 掌握DPDK Hash表的创建和销毁
- 学会Hash表的增删查操作
- 了解5元组会话查找的实现

**应用场景：**
- 会话表（Session Table）：追踪TCP/UDP连接状态
- 流量统计：按流分类统计数据包
- NAT转换：IP地址和端口映射
- 负载均衡：基于流的分发决策

---

## 一、程序功能

本示例程序模拟了网络会话表的典型操作：

```
创建会话表（Hash表）
    ↓
添加会话（5元组）
    ↓
查找会话（验证添加成功）
    ↓
模拟会话查找（相同5元组）
    ↓
删除会话
    ↓
销毁会话表
```

**什么是5元组？**
在网络通信中，5元组唯一标识一个会话流：
- 源IP地址（Source IP）
- 目的IP地址（Destination IP）
- 源端口（Source Port）
- 目的端口（Destination Port）
- 协议类型（Protocol）

---

## 二、核心数据结构

### 2.1 5元组Key定义

```c
struct flow_key {
    uint32_t ip_src;      // 源IP地址
    uint32_t ip_dst;      // 目的IP地址
    uint16_t port_src;    // 源端口
    uint16_t port_dst;    // 目的端口
    uint8_t proto;        // 协议（TCP=6, UDP=17）
} __rte_packed;
```

**关键点：**
- `__rte_packed`：确保结构体紧凑排列，避免内存对齐产生的空洞
- 总大小：4 + 4 + 2 + 2 + 1 = 13字节
- 用作Hash表的Key，唯一标识一个网络流

### 2.2 Hash表配置参数

```c
struct rte_hash_parameters params = {
    .name = "flow_table",              // Hash表名称
    .entries = 64,                     // 表项数量（建议是2的幂）
    .key_len = sizeof(struct flow_key),// Key的长度（13字节）
    .hash_func = rte_jhash,            // 哈希函数（Jenkins Hash）
    .hash_func_init_val = 0,           // 哈希函数初始值
    .socket_id = 0,                    // NUMA节点ID
};
```

**参数说明：**
- `name`：Hash表的唯一标识名称
- `entries`：Hash表可以存储的最大条目数
- `key_len`：Key的字节长度
- `hash_func`：哈希函数，常用`rte_jhash`（性能好、冲突少）
- `socket_id`：指定NUMA节点，0表示本地节点

---

## 三、Hash表核心操作

### 3.1 创建Hash表

#### API说明
```c
struct rte_hash *rte_hash_create(const struct rte_hash_parameters *params);
```

**功能：** 创建一个新的Hash表

**参数：**
- `params`：Hash表配置参数结构体指针

**返回值：**
- 成功：返回Hash表指针
- 失败：返回NULL

#### 代码示例
```c
struct rte_hash *hash_table = NULL;

// 创建Hash表
hash_table = rte_hash_create(&params);
if (hash_table == NULL) {
    printf("ERROR: Cannot create hash table\n");
    goto cleanup;
}
```

### 3.2 添加元素

#### API说明
```c
int32_t rte_hash_add_key(const struct rte_hash *h, const void *key);
```

**功能：** 向Hash表中添加一个Key

**参数：**
- `h`：Hash表指针
- `key`：要添加的Key指针

**返回值：**
- 成功：返回Key的位置索引（≥0）
- 失败：返回负数错误码

#### 代码示例
```c
struct flow_key firstKey;

// 初始化5元组
firstKey.ip_src = RTE_IPV4(3, 2, 1, 0);      // 3.2.1.0
firstKey.ip_dst = RTE_IPV4(7, 6, 5, 4);      // 7.6.5.4
firstKey.port_src = 0x0908;                   // 端口2312
firstKey.port_dst = 0x0b0a;                   // 端口2826
firstKey.proto = 15;                          // 协议15

// 添加到Hash表
int pos = rte_hash_add_key(hash_table, &firstKey);
if (pos < 0) {
    printf("ERROR: Cannot add key: %s\n", strerror(-pos));
    goto cleanup;
}

printf("Key added at position: %d\n", pos);
```

### 3.3 查找元素

#### API说明
```c
int32_t rte_hash_lookup(const struct rte_hash *h, const void *key);
```

**功能：** 在Hash表中查找一个Key

**参数：**
- `h`：Hash表指针
- `key`：要查找的Key指针

**返回值：**
- 成功：返回Key的位置索引（≥0）
- 失败：返回负数（-ENOENT表示Key不存在）

#### 代码示例
```c
// 查找Key
pos = rte_hash_lookup(hash_table, &firstKey);
if (pos < 0) {
    printf("ERROR: Key not found: %s\n", strerror(-pos));
} else {
    printf("Key found at position: %d\n", pos);
}
```

**典型应用场景：**
```c
// 收到数据包时，提取5元组查找会话
struct flow_key incoming_flow;
// ... 从数据包中提取5元组填充incoming_flow ...

int session_index = rte_hash_lookup(hash_table, &incoming_flow);
if (session_index >= 0) {
    // 找到会话，继续处理
    printf("Existing session found at index %d\n", session_index);
} else {
    // 新会话，创建新条目
    printf("New session, creating entry\n");
}
```

### 3.4 删除元素

#### API说明
```c
int32_t rte_hash_del_key(const struct rte_hash *h, const void *key);
```

**功能：** 从Hash表中删除一个Key

**参数：**
- `h`：Hash表指针
- `key`：要删除的Key指针

**返回值：**
- 成功：返回被删除Key的位置索引（≥0）
- 失败：返回负数（-ENOENT表示Key不存在）

#### 代码示例
```c
// 删除Key
pos = rte_hash_del_key(hash_table, &firstKey);
if (pos < 0) {
    printf("ERROR: Cannot delete key: %s\n", strerror(-pos));
} else {
    printf("Key deleted from position: %d\n", pos);
}
```

### 3.5 销毁Hash表

#### API说明
```c
void rte_hash_free(struct rte_hash *h);
```

**功能：** 销毁Hash表并释放内存

**参数：**
- `h`：Hash表指针

**返回值：** 无

#### 代码示例
```c
// 程序退出前销毁Hash表
if (hash_table != NULL) {
    rte_hash_free(hash_table);
    printf("Hash table successfully freed\n");
}
```

---

## 四、编译和运行

### 4.1 编译

```bash
# 在项目根目录
cd /home/work/dpdk-hands-on

# 创建构建目录（如果还没有）
mkdir -p build && cd build

# 配置和编译
cmake ..
make

# 可执行文件：bin/hash_usage
```

### 4.2 运行

```bash
# 切换到项目根目录
cd /home/work/dpdk-hands-on

# 运行程序（需要root权限）
sudo ./bin/hash_usage -l 0-1 --no-huge
```

**运行参数说明：**
- `-l 0-1`：使用CPU核心0和1
- `--no-huge`：不使用大页内存（方便调试）

---

## 五、完整执行流程

### 5.1 源代码结构

完整代码：[main.c](2-hash_usage/main.c)

```c
int main(int argc, char **argv)
{
    struct rte_hash *hash_table = NULL;

    // ① 初始化EAL
    ret = rte_eal_init(argc, argv);

    // ② 准备测试数据
    struct flow_key firstKey;
    init_test_flow_key(&firstKey);

    // ③ 创建Hash表
    hash_table = rte_hash_create(&params);

    // ④ 添加Key
    pos = rte_hash_add_key(hash_table, &firstKey);

    // ⑤ 查找Key（验证添加成功）
    pos = rte_hash_lookup(hash_table, &firstKey);

    // ⑥ 模拟会话查找（使用相同的5元组）
    struct flow_key secondKey;
    init_test_flow_key(&secondKey);  // 相同的5元组
    pos = rte_hash_lookup(hash_table, &secondKey);

    // ⑦ 删除Key
    pos = rte_hash_del_key(hash_table, &firstKey);

cleanup:
    // ⑧ 清理资源
    if (hash_table != NULL)
        rte_hash_free(hash_table);

    rte_eal_cleanup();
    return 0;
}
```

### 5.2 程序输出示例

```
EAL: Detected 8 lcore(s)
EAL: Detected 1 NUMA nodes
Key added at position: 0
Key found at position: 0
Session key found at position: 0
Key deleted from position: 0
INFO: Hash table operations completed successfully
INFO: Hash table successfully freed
INFO: EAL cleanup completed
```

---

## 六、常见问题

### Q1: Hash表的entries参数如何选择？

**建议：**
- 设置为预期流数量的1.5-2倍
- 最好是2的幂（如64, 128, 256, 1024, 4096）
- 太小：容易满，性能下降
- 太大：浪费内存

**示例：**
```c
// 预期1000个会话，设置为2048
.entries = 2048,
```

### Q2: 为什么要使用__rte_packed？

**原因：** 避免结构体内存对齐产生的空洞

**对比：**
```c
// 不使用__rte_packed（可能有填充字节）
struct flow_key {
    uint32_t ip_src;   // 4字节
    uint32_t ip_dst;   // 4字节
    uint16_t port_src; // 2字节
    uint16_t port_dst; // 2字节
    uint8_t proto;     // 1字节
};  // 实际可能是16字节（有3字节填充）

// 使用__rte_packed（紧凑排列）
struct flow_key {
    // ... 相同字段 ...
} __rte_packed;  // 确保是13字节
```

**影响：**
- 未对齐的结构体在哈希时，填充字节值不确定，导致相同5元组产生不同哈希值
- 使用`__rte_packed`确保一致性

### Q3: 如何处理Hash冲突？

**DPDK Hash表特性：**
- 自动处理冲突（使用Cuckoo Hashing或链表法）
- 用户无需手动处理冲突
- 当冲突严重时，性能会下降

**最佳实践：**
```c
// 监控Hash表使用率
uint32_t used = rte_hash_count(hash_table);
uint32_t capacity = params.entries;
float usage = (float)used / capacity;

if (usage > 0.8) {
    printf("Warning: Hash table usage %.1f%%, consider resize\n",
           usage * 100);
}
```

### Q4: Hash表是否线程安全？

**答：** DPDK Hash表支持多线程，但需要配置

```c
struct rte_hash_parameters params = {
    .name = "flow_table",
    .entries = 1024,
    .key_len = sizeof(struct flow_key),
    .hash_func = rte_jhash,
    .socket_id = rte_socket_id(),

    // 启用多线程支持（添加以下标志）
    .extra_flag = RTE_HASH_EXTRA_FLAGS_TRANS_MEM_SUPPORT |
                  RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD,
};
```

**性能提示：**
- 单线程访问：不需要额外标志，性能最高
- 多线程访问：添加相应标志，有一定性能开销

---

## 七、核心API总结

| API | 功能 | 返回值 |
|-----|------|--------|
| `rte_hash_create()` | 创建Hash表 | Hash表指针/NULL |
| `rte_hash_add_key()` | 添加Key | 位置索引/负数 |
| `rte_hash_lookup()` | 查找Key | 位置索引/负数 |
| `rte_hash_del_key()` | 删除Key | 位置索引/负数 |
| `rte_hash_free()` | 销毁Hash表 | 无 |

**辅助API：**
| API | 功能 |
|-----|------|
| `rte_hash_count()` | 获取当前表项数量 |
| `rte_hash_reset()` | 清空Hash表（保留结构） |
| `rte_jhash()` | Jenkins哈希函数 |

---

## 参考资料

- **DPDK Hash库文档**：https://doc.dpdk.org/guides/prog_guide/hash_lib.html
- **源代码**：[main.c](2-hash_usage/main.c)
- **编译配置**：[CMakeLists.txt](2-hash_usage/CMakeLists.txt)

---

**课程结束！**
