# DPDK Hello World 教程（第1课）

## 课程概述

Hello World是DPDK最基础的示例程序，展示了如何在多个CPU核心上并行执行任务。

**课程目标：**
- 理解DPDK应用程序的基本结构
- 学会初始化DPDK环境
- 掌握多核并行执行的基本方法

---

## 一、程序功能

这个示例程序实现了一个非常简单的功能：

**在每个CPU核心上打印 "hello from core X"**

```
核心0（主核心）：hello from core 0
核心1（工作核心）：hello from core 1
核心2（工作核心）：hello from core 2
核心3（工作核心）：hello from core 3
```

通过这个简单的例子，你可以学习到：
- 如何初始化DPDK环境
- 如何在多个核心上并行运行代码
- DPDK应用程序的基本框架

---

## 二、程序执行流程

### 2.1 流程图

```
开始
  ↓
① 初始化EAL环境
  ↓
② 在Worker核心上启动任务
  ↓
③ 在Main核心上执行任务
  ↓
④ 等待所有Worker核心完成
  ↓
⑤ 清理资源
  ↓
结束
```

### 2.2 核心概念

**什么是Lcore（逻辑核心）？**
- **Main Lcore**：主核心，运行main()函数的核心（通常是核心0）
- **Worker Lcore**：工作核心，用于并行处理任务的核心（核心1、2、3...）

```
使用 -l 0-3 参数时的核心分配：
┌─────────┬──────────┬──────────┬──────────┐
│ 核心0   │  核心1   │  核心2   │  核心3   │
│ Main    │ Worker   │ Worker   │ Worker   │
└─────────┴──────────┴──────────┴──────────┘
```

---

## 三、源代码解析

完整代码：[main.c](1-helloworld/main.c)

### 3.1 核心函数

```c
// 在lcore上执行的任务函数
static int lcore_hello(__rte_unused void *arg)
{
    unsigned lcore_id;
    lcore_id = rte_lcore_id();  // 获取当前核心ID
    printf("hello from core %u\n", lcore_id);
    return 0;
}
```

**说明：**
- 这个函数会在每个核心上执行
- `rte_lcore_id()` 返回当前运行的核心编号
- `__rte_unused` 表示参数未使用，避免编译警告

### 3.2 主函数

```c
int main(int argc, char **argv)
{
    int ret;
    unsigned lcore_id;

    // ① 初始化EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    // ② 在每个Worker核心上启动任务
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        printf("worker lcore id:%d\n", lcore_id);
        rte_eal_remote_launch(lcore_hello, NULL, lcore_id);
    }

    // ③ 在Main核心上也执行任务
    lcore_hello(NULL);

    // ④ 等待所有Worker核心完成
    rte_eal_mp_wait_lcore();

    // ⑤ 清理EAL
    rte_eal_cleanup();

    return 0;
}
```

---

## 四、关键API说明

### 4.1 EAL初始化

#### `rte_eal_init()`
```c
int rte_eal_init(int argc, char **argv);
```
- **功能**：初始化DPDK环境抽象层（EAL）
- **作用**：
  - 解析命令行参数
  - 初始化大页内存
  - 检测CPU核心
  - 初始化PCI设备
- **返回值**：
  - 成功：返回解析的参数数量（正数）
  - 失败：返回负数
- **注意**：必须在所有DPDK API之前调用

#### `rte_eal_cleanup()`
```c
int rte_eal_cleanup(void);
```
- **功能**：清理EAL资源
- **注意**：程序退出前必须调用

### 4.2 核心管理

#### `rte_lcore_id()`
```c
unsigned rte_lcore_id(void);
```
- **功能**：获取当前线程运行的核心ID
- **返回值**：核心ID（0, 1, 2, 3...）

#### `RTE_LCORE_FOREACH_WORKER(i)`
```c
#define RTE_LCORE_FOREACH_WORKER(i)
```
- **功能**：遍历所有Worker核心（不包括Main核心）
- **用法**：
```c
unsigned lcore_id;
RTE_LCORE_FOREACH_WORKER(lcore_id) {
    // lcore_id 依次为 1, 2, 3...
}
```

### 4.3 远程启动

#### `rte_eal_remote_launch()`
```c
int rte_eal_remote_launch(lcore_function_t *f, void *arg, unsigned worker_id);
```
- **功能**：在指定的Worker核心上异步启动函数
- **参数**：
  - `f`：要执行的函数指针
  - `arg`：传递给函数的参数
  - `worker_id`：目标核心ID
- **返回值**：0表示成功

#### `rte_eal_mp_wait_lcore()`
```c
void rte_eal_mp_wait_lcore(void);
```
- **功能**：等待所有Worker核心完成任务
- **说明**：阻塞调用，类似于`pthread_join()`

---

## 五、编译和运行

### 5.1 编译

```bash
# 在项目根目录
cd /home/work/dpdk-hands-on

# 创建构建目录
mkdir -p build && cd build

# 配置和编译
cmake ..
make

# 可执行文件：bin/helloworld
```

### 5.2 运行示例

#### 示例1：使用2个核心
```bash
sudo ./bin/helloworld -l 0-1
```

**输出：**
```
EAL: Detected 8 lcore(s)
EAL: Detected 1 NUMA nodes
worker lcore id:1
hello from core 0
hello from core 1
```

#### 示例2：使用4个核心
```bash
sudo ./bin/helloworld -l 0-3
```

**输出：**
```
EAL: Detected 8 lcore(s)
worker lcore id:1
worker lcore id:2
worker lcore id:3
hello from core 0
hello from core 1
hello from core 2
hello from core 3
```

### 5.3 常用EAL参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `-l` | 指定使用的核心列表 | `-l 0-3` 使用核心0到3 |
| `-c` | 核心掩码（十六进制） | `-c 0xf` 等同于`-l 0-3` |
| `--no-huge` | 不使用大页内存（方便调试） | `--no-huge` |
| `-m` | 分配的内存大小（MB） | `-m 1024` |

---

## 六、常见问题

### Q1: 运行时提示 "Cannot init EAL"

**原因：**
1. 没有使用root权限
2. 大页内存未配置

**解决方法：**
```bash
# 方法1：使用sudo
sudo ./bin/helloworld -l 0-1

# 方法2：不使用大页内存
sudo ./bin/helloworld -l 0-1 --no-huge

# 方法3：配置大页内存
echo 256 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

### Q2: 输出顺序为什么不固定？

**答：** Worker核心是并行执行的，打印顺序取决于操作系统的线程调度。

```
# 第一次运行
hello from core 0
hello from core 1
hello from core 2

# 第二次运行
hello from core 0
hello from core 2
hello from core 1
```

这是正常现象，说明多核并行成功。

### Q3: Main核心和Worker核心有什么区别？

| 特性 | Main Lcore | Worker Lcore |
|------|-----------|--------------|
| 数量 | 1个 | 0个或多个 |
| 启动方式 | 自动运行main()函数 | 需要用`rte_eal_remote_launch()`启动 |
| 执行模式 | 同步执行 | 异步执行 |
| 典型用途 | 控制逻辑、管理 | 数据包处理、计算任务 |

---

## 七、核心要点总结

### 7.1 程序流程（5步）

```c
// ① 初始化EAL
rte_eal_init(argc, argv);

// ② 在Worker核心上启动任务
RTE_LCORE_FOREACH_WORKER(lcore_id) {
    rte_eal_remote_launch(lcore_hello, NULL, lcore_id);
}

// ③ 在Main核心上执行任务
lcore_hello(NULL);

// ④ 等待Worker核心完成
rte_eal_mp_wait_lcore();

// ⑤ 清理资源
rte_eal_cleanup();
```

### 7.2 核心API（6个）

| API | 功能 |
|-----|------|
| `rte_eal_init()` | 初始化DPDK环境 |
| `rte_lcore_id()` | 获取当前核心ID |
| `RTE_LCORE_FOREACH_WORKER()` | 遍历Worker核心 |
| `rte_eal_remote_launch()` | 在指定核心上启动函数 |
| `rte_eal_mp_wait_lcore()` | 等待所有Worker完成 |
| `rte_eal_cleanup()` | 清理资源 |

---

## 八、下一步学习

完成本课程后，建议继续学习：
- **第2课：Hash Table使用** - DPDK哈希表
- **第3课：抓包程序** - 网络数据包接收
- **第4课：解析数据包** - 解析以太网/IP/TCP协议

---

## 参考资料

- **DPDK官方文档**：https://doc.dpdk.org/guides-24.11/sample_app_ug/hello_world.html
- **源代码**：[main.c](1-helloworld/main.c)
- **编译配置**：[CMakeLists.txt](1-helloworld/CMakeLists.txt)

---

**课程结束！**
