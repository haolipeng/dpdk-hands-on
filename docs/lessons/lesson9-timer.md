
# DPDK Timer 定时器课程大纲

## 1. DPDK Timer 介绍

### 1.1 什么是 DPDK Timer
- DPDK Timer 库，允许在指定延迟后在指定的逻辑核心上执行回调函数
- 官方文档：https://doc.dpdk.org/guides-25.07/prog_guide/timer_lib.html



### 1.2 主要功能特性

- **两种定时器类型**：
  - **PERIODICAL（周期性）**：多次触发，自动重载
  - **SINGLE（单次）**：一次性触发，需手动重载
- **跨核心执行**：定时器可在一个核心上加载，在另一个核心上执行
- **高精度**：精度取决于 `rte_timer_manage()` 的调用频率



### 1.3 典型应用场景

- 周期性任务调度（如垃圾回收）
- 状态机超时处理（如 ARP、网桥等协议）
- 定时检查和监控
- 网络协议栈中的重传机制

---

## 2. 核心 API 介绍

### 2.1 `rte_timer_subsystem_init()`
```c
void rte_timer_subsystem_init(void);
```
- **功能**：初始化 DPDK 定时器子系统
- **调用时机**：在使用任何定时器功能之前，通常在 `rte_eal_init()` 之后调用
- **注意事项**：**整个应用只需调用一次**



### 2.2 `rte_timer_init()`

```c
void rte_timer_init(struct rte_timer *tim);
```
- **功能**：初始化定时器结构体
- **参数**：`tim` - 指向定时器结构的指针
- **作用**：将定时器设置为 STOPPED 状态
- **注意事项**：每个定时器使用前必须初始化



### 2.3 `rte_timer_reset()`

```c
int rte_timer_reset(struct rte_timer *tim,
                    uint64_t ticks,
                    enum rte_timer_type type,
                    unsigned tim_lcore,
                    rte_timer_cb_t fct,
                    void *arg);
```
- **功能**：启动或重新加载定时器
- **参数说明**：
  - `tim`：定时器结构指针
  - `ticks`：延迟时间（以 CPU 时钟周期为单位）
    - 使用 `rte_get_timer_hz()` 获取每秒的时钟周期数
    - 示例：`hz` 表示1秒，`hz/3` 表示1/3秒
  - `type`：定时器类型
    - `PERIODICAL`：周期性定时器，自动重载
    - `SINGLE`：单次定时器，触发后停止
  - `tim_lcore`：执行回调的逻辑核心 ID
  - `fct`：定时器到期时的回调函数
  - `arg`：传递给回调函数的参数
- **返回值**：成功返回0，失败返回负数



### 2.4 `rte_timer_stop()`

```c
int rte_timer_stop(struct rte_timer *tim);
```
- **功能**：停止正在运行的定时器
- **参数**：`tim` - 定时器结构指针
- **返回值**：成功返回0，失败返回负数
- **使用场景**：周期性定时器需要在满足特定条件时停止



### 2.5 `rte_timer_manage()`

```c
void rte_timer_manage(void);
```
- **功能**：扫描定时器列表，执行到期的定时器回调
- **调用方式**：需要在每个逻辑核心的主循环中周期性调用
- **精度控制**：调用频率决定定时器精度
  - 示例中每 ~10ms 调用一次
- **注意事项**：
  - 必须在所有需要处理定时器的核心上调用
  - 调用频率与定时器精度成正比

### 2.6 `rte_get_timer_hz()`
```c
uint64_t rte_get_timer_hz(void);
```
- **功能**：获取定时器频率（每秒的时钟周期数）
- **返回值**：时钟频率（Hz）
- **使用示例**：
  ```c
  uint64_t hz = rte_get_timer_hz();
  uint64_t one_second = hz;      // 1秒
  uint64_t half_second = hz / 2;  // 0.5秒
  ```

### 2.7 `rte_get_timer_cycles()`
```c
uint64_t rte_get_timer_cycles(void);
```
- **功能**：获取当前时间戳（时钟周期数）
- **返回值**：当前的 CPU 时钟周期计数
- **用途**：计算时间差，控制 `rte_timer_manage()` 的调用频率



### 2.8 `rte_get_next_lcore()`
```c
unsigned rte_get_next_lcore(unsigned i, int skip_main, int wrap);
```
- **功能**：获取下一个可用的逻辑核心 ID
- **参数说明**：
  - `i`：当前逻辑核心 ID
  - `skip_main`：是否跳过主核心（0=不跳过，1=跳过）
  - `wrap`：是否循环（0=不循环，1=循环）
- **返回值**：下一个逻辑核心 ID
- **使用场景**：在 SINGLE 类型定时器中实现跨核心轮转

---

## 3. 示例编译和运行

### 3.1 项目结构
```
9-timer/
├── CMakeLists.txt       # CMake 构建配置
├── periodic_timer.c     # 周期性定时器示例
├── single_timer.c       # 单次定时器示例
└── README.md           # 本文档
```

### 3.2 编译步骤
```bash
# 1. 进入项目目录
cd 9-timer

# 2. 创建构建目录并配置
cmake -B build

# 3. 进入构建目录
cd build

# 4. 编译所有程序
make
```

### 3.3 运行命令

```bash
# 运行周期性定时器（使用核心 0-1）
sudo ./periodic_timer -l 0-1 -n 4

# 运行单次定时器（使用核心 0-1）
sudo ./single_timer -l 0-1 -n 4
```

### 3.4 DPDK EAL 参数说明
- `-l 0-1`：使用逻辑核心 0 和 1
- `-n 4`：使用 4 个内存通道

---

## 4. 示例解读

### 4.1 SINGLE 类型定时器源代码解读

#### 4.1.1 程序概述
- **文件**：[single_timer.c](single_timer.c)
- **特点**：单次触发定时器，每次触发后手动重载到不同的核心
- **间隔**：1/3 秒触发一次
- **行为**：演示跨核心轮转执行

#### 4.1.2 关键代码分析

##### 定时器回调函数
```c
/* timer1 回调函数 - 单次触发定时器 */
static void
timer1_cb(__rte_unused struct rte_timer *tim,
          __rte_unused void *arg)
{
    unsigned lcore_id = rte_lcore_id();
    uint64_t hz;

    printf("[SINGLE] %s() on lcore %u\n", __func__, lcore_id);

    /* 在另一个lcore上重新加载 - 手动重载演示 */
    hz = rte_get_timer_hz();
    lcore_id = rte_get_next_lcore(lcore_id, 0, 1);  // 获取下一个核心
    printf("[SINGLE] Manually reloading timer on next lcore %u\n", lcore_id);
    rte_timer_reset(tim, hz/3, SINGLE, lcore_id, timer1_cb, NULL);
}
```

**关键点解析**：
1. **获取当前核心**：`rte_lcore_id()` 显示回调在哪个核心执行
2. **手动重载**：SINGLE 类型不会自动重载，需在回调中手动调用 `rte_timer_reset()`
3. **跨核心轮转**：`rte_get_next_lcore()` 实现核心间轮流执行
4. **循环执行**：每次回调都重新设置定时器，形成连续执行

##### 主循环 - 定时器管理
```c
static __rte_noreturn int
lcore_mainloop(__rte_unused void *arg)
{
    uint64_t prev_tsc = 0, cur_tsc, diff_tsc;
    unsigned lcore_id;

    lcore_id = rte_lcore_id();
    printf("Starting mainloop on core %u\n", lcore_id);

    while (1) {
        /*
         * 在每个核心上调用定时器处理程序：由于我们不需要非常精确的定时器，
         * 所以只需要每隔约10ms调用一次rte_timer_manage()
         */
        cur_tsc = rte_get_timer_cycles();
        diff_tsc = cur_tsc - prev_tsc;
        if (diff_tsc > timer_resolution_cycles) {
            rte_timer_manage();  // 处理到期的定时器
            prev_tsc = cur_tsc;
        }
    }
}
```

**关键点解析**：
1. **时间测量**：使用 TSC（时间戳计数器）测量时间间隔
2. **控制精度**：每 10ms 检查一次定时器，平衡性能与精度
3. **必须调用**：每个核心都必须执行此循环，否则该核心上的定时器不会触发

##### 主函数 - 初始化和启动
```c
int main(int argc, char **argv)
{
    int ret;
    uint64_t hz;
    unsigned lcore_id;

    /* 初始化EAL环境 */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    /* 初始化RTE定时器库 */
    rte_timer_subsystem_init();

    /* 初始化定时器结构 */
    rte_timer_init(&timer1);

    /* 加载timer1，每1/3秒触发一次，在下一个lcore上运行，手动重新加载 */
    hz = rte_get_timer_hz();
    timer_resolution_cycles = hz * 10 / 1000; /* 约10ms */

    lcore_id = rte_lcore_id();
    lcore_id = rte_get_next_lcore(lcore_id, 0, 1);  // 从第一个工作核心开始
    printf("Setting up SINGLE timer on lcore %u, interval=1/3 second\n", lcore_id);
    printf("Timer will be manually reloaded on different cores each time\n");
    rte_timer_reset(&timer1, hz/3, SINGLE, lcore_id, timer1_cb, NULL);

    /* 在每个工作lcore上调用lcore_mainloop() */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        rte_eal_remote_launch(lcore_mainloop, NULL, lcore_id);
    }

    /* 在主lcore上也调用 */
    (void) lcore_mainloop(NULL);

    /* 清理EAL环境 */
    rte_eal_cleanup();

    return 0;
}
```

**执行流程**：
1. 初始化 DPDK EAL 环境
2. 初始化定时器子系统
3. 初始化定时器结构
4. 设置首次定时器（1/3秒后在工作核心上触发）
5. 在所有核心上启动主循环
6. 定时器触发 → 回调执行 → 手动重载到下一个核心 → 循环往复

#### 4.1.3 预期输出
```
Starting mainloop on core 0
Starting mainloop on core 1
Setting up SINGLE timer on lcore 1, interval=1/3 second
Timer will be manually reloaded on different cores each time
[SINGLE] timer1_cb() on lcore 1
[SINGLE] Manually reloading timer on next lcore 0
[SINGLE] timer1_cb() on lcore 0
[SINGLE] Manually reloading timer on next lcore 1
...（在核心0和1之间轮转）
```

---

### 4.2 PERIODICAL 类型定时器源代码解读

#### 4.2.1 程序概述
- **文件**：[periodic_timer.c](periodic_timer.c)
- **特点**：周期性定时器，自动重载
- **间隔**：1 秒触发一次
- **行为**：固定在主核心执行，触发 20 次后自动停止

#### 4.2.2 关键代码分析

##### 定时器回调函数
```c
/* timer0 回调函数 - 周期性定时器 */
static void
timer0_cb(__rte_unused struct rte_timer *tim,
          __rte_unused void *arg)
{
    static unsigned counter = 0;
    unsigned lcore_id = rte_lcore_id();

    printf("[PERIODIC] %s() on lcore %u, counter=%u\n", __func__, lcore_id, counter);

    /* 这个定时器会自动重新加载，直到我们决定停止它
     * 当计数器达到20时停止 */
    if ((counter ++) == 20)
        rte_timer_stop(tim);
}
```

**关键点解析**：
1. **自动重载**：PERIODICAL 类型会自动重新触发，无需手动 `rte_timer_reset()`
2. **计数器**：使用静态变量跟踪触发次数
3. **条件停止**：达到 20 次后调用 `rte_timer_stop()` 停止定时器
4. **固定核心**：始终在设置时指定的核心上执行

##### 主函数 - 定时器设置
```c
int main(int argc, char **argv)
{
    int ret;
    uint64_t hz;
    unsigned lcore_id;

    /* 初始化EAL环境 */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    /* 初始化RTE定时器库 */
    rte_timer_subsystem_init();

    /* 初始化定时器结构 */
    rte_timer_init(&timer0);

    /* 加载timer0，每秒触发一次，在主lcore上运行，自动重新加载 */
    hz = rte_get_timer_hz();
    timer_resolution_cycles = hz * 10 / 1000; /* 约10ms */

    lcore_id = rte_lcore_id();  // 获取主核心ID
    printf("Setting up PERIODIC timer on lcore %u, interval=1 second\n", lcore_id);
    rte_timer_reset(&timer0, hz, PERIODICAL, lcore_id, timer0_cb, NULL);
    //               ↑       ↑    ↑         ↑
    //               |       |    |         在主核心上执行
    //               |       |    周期性类型
    //               |       1秒间隔
    //               定时器结构

    /* 在每个工作lcore上调用lcore_mainloop() */
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        rte_eal_remote_launch(lcore_mainloop, NULL, lcore_id);
    }

    /* 在主lcore上也调用 */
    (void) lcore_mainloop(NULL);

    /* 清理EAL环境 */
    rte_eal_cleanup();

    return 0;
}
```

**执行流程**：
1. 初始化 DPDK EAL 环境
2. 初始化定时器子系统
3. 初始化定时器结构
4. 设置周期性定时器（每秒在主核心上触发）
5. 在所有核心上启动主循环
6. 定时器自动触发 → 回调执行 → 自动重载 → 20次后停止

#### 4.2.3 预期输出
```
Starting mainloop on core 0
Starting mainloop on core 1
Setting up PERIODIC timer on lcore 0, interval=1 second
[PERIODIC] timer0_cb() on lcore 0, counter=0
[PERIODIC] timer0_cb() on lcore 0, counter=1
[PERIODIC] timer0_cb() on lcore 0, counter=2
...
[PERIODIC] timer0_cb() on lcore 0, counter=19
[PERIODIC] timer0_cb() on lcore 0, counter=20
（定时器停止，但主循环继续运行）
```

---

### 4.3 两种定时器类型对比

| 特性 | PERIODICAL（周期性） | SINGLE（单次） |
|------|---------------------|---------------|
| **触发次数** | 多次，自动重复 | 一次，手动重载才能再次触发 |
| **重载方式** | 自动重载 | 需在回调中手动调用 `rte_timer_reset()` |
| **执行核心** | 固定在设置时指定的核心 | 可在每次重载时更换核心 |
| **停止方式** | 调用 `rte_timer_stop()` | 回调中不重载即停止 |
| **适用场景** | 固定周期任务（垃圾回收、状态检查） | 灵活调度、跨核心负载均衡 |
| **代码复杂度** | 简单，设置一次即可 | 需要在回调中处理重载逻辑 |

---

## 5. 总结

### 5.1 核心要点
1. **定时器类型选择**：
   - 固定周期任务 → PERIODICAL
   - 需要动态调整或跨核心 → SINGLE

2. **必须的初始化步骤**：
   
   ```c
   rte_eal_init()              // DPDK环境初始化
   rte_timer_subsystem_init()  // 定时器子系统初始化
   rte_timer_init()            // 单个定时器初始化
   rte_timer_reset()           // 启动定时器
   ```
   
3. **定时器管理循环**：
   
   - 每个核心都必须周期性调用 `rte_timer_manage()`
   - 调用频率决定定时器精度
   - 典型值：每 10ms 调用一次
   
4. **时间计算**：
   ```c
   uint64_t hz = rte_get_timer_hz();  // 获取频率
   uint64_t one_sec = hz;             // 1秒
   uint64_t half_sec = hz / 2;        // 0.5秒
   uint64_t ms_100 = hz / 10;         // 0.1秒
   ```

### 5.2 最佳实践
1. **避免在回调中执行耗时操作**：回调应快速完成，避免阻塞定时器管理
2. **合理设置检查间隔**：`rte_timer_manage()` 调用频率应略高于定时器精度要求
3. **多核心应用**：确保所有核心都运行定时器管理循环
4. **资源清理**：程序退出前停止所有定时器
