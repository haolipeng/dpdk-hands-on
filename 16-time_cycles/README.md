# Lesson 16: Time, Cycles and Performance Measurement

## 概述

本课程演示了 DPDK 中的高精度计时和性能测试技术,包括两个完整的示例程序。

## 为什么需要高精度计时?

### 场景示例

**网络数据包处理**:
- 10Gbps 流量 ≈ 每秒 1480 万个 64 字节数据包
- 每个数据包处理时间 ≈ 67 纳秒
- 传统 `gettimeofday()` 只有微秒精度 → 不够用!

**性能优化**:
- 比较两个算法的性能差异
- 需要纳秒级精度才能准确测量

**DPDK 的解决方案**: 使用 CPU 的 TSC (Time Stamp Counter) 实现纳秒级计时

## 核心概念

### 时钟周期 (Cycles)

**CPU 时钟周期**是计算机中最小的时间单位:
- 现代 CPU: 2-4 GHz
- 3 GHz CPU = 每纳秒 3 个周期
- TSC 寄存器: 64 位计数器,每个时钟周期 +1

### 时间转换公式

```
时间(秒) = 周期数 / 频率

例如:
  频率 = 2.4 GHz = 2,400,000,000 Hz
  周期数 = 2,400,000,000
  时间 = 2,400,000,000 / 2,400,000,000 = 1 秒
```

## 示例程序

### 1. time_demo - 时间和周期基础

基础的时间和周期演示程序,展示:
- 获取 CPU 时钟频率
- 测量代码执行时间
- 使用 DPDK 延时函数
- 测量 API 调用开销
- 时间单位转换
- 不同 API 对比

**编译**:
```bash
cd build
make time_demo
```

**运行**:
```bash
sudo ./bin/time_demo -l 0 --no-pci
```

**预期输出**:
```
╔════════════════════════════════════════════════════════╗
║   DPDK 时间与时钟周期演示 - Lesson 16                 ║
╚════════════════════════════════════════════════════════╝

系统信息:
  Lcore ID: 0
  总 lcore 数: 1
  时钟频率: 2400000000 Hz (2.400 GHz)

【第1部分】获取 CPU 时钟频率
─────────────────────────────────────────────
  时钟频率: 2400000000 Hz
  即: 2.400 GHz
  每个时钟周期: 0.417 纳秒
  ...
```

### 2. benchmark - 性能测试框架

完整的性能测试框架,包含:
- 空循环基准测试
- API 调用开销测试
- 内存分配/释放性能
- 内存拷贝性能(不同大小)
- Ring 队列操作性能
- Mempool 操作性能
- 标准 vs DPDK API 对比

**编译**:
```bash
cd build
make benchmark
```

**运行**:
```bash
sudo ./bin/benchmark -l 0 --no-pci
```

**预期输出**:
```
╔═══════════════════════════════════════════════════════════╗
║              DPDK 性能测试工具 - Lesson 16               ║
╠═══════════════════════════════════════════════════════════╣
║  CPU 频率: 2.400      GHz                                 ║
║  每周期: 0.417        纳秒                                ║
║  Lcore ID: 0                                              ║
╚═══════════════════════════════════════════════════════════╝

┌─ 开始性能测试...
│
  测试1: 空循环 - 测量循环本身的开销
  迭代次数: 100000000
  ├─ 测试项: 空循环(基准开销)
  │   ├─ 迭代次数: 100000000
  │   ├─ 总耗时: 41666.67 微秒 (41.667 毫秒)
  │   ├─ 每次操作: 0.42 纳秒
  │   └─ 吞吐量: 2400.00 M ops/sec
  ...
```

## 核心 API

### 获取时钟频率

```c
uint64_t rte_get_timer_hz(void);
```

返回每秒的时钟周期数(频率)。

**示例**:
```c
uint64_t hz = rte_get_timer_hz();
printf("CPU 频率: %lu Hz (%.2f GHz)\n", hz, hz / 1e9);
```

### 获取当前时钟周期

```c
uint64_t rte_get_timer_cycles(void);
```

返回当前的 TSC 计数值。

**示例**:
```c
uint64_t start = rte_get_timer_cycles();
// ... 执行操作 ...
uint64_t end = rte_get_timer_cycles();
uint64_t elapsed = end - start;
```

### 精确延时

```c
void rte_delay_us(unsigned us);  // 微秒级延时
void rte_delay_ms(unsigned ms);  // 毫秒级延时
```

**注意**: 这些是忙等待(busy-wait),会占用 CPU!

**示例**:
```c
rte_delay_us(100);  // 延时 100 微秒
rte_delay_ms(10);   // 延时 10 毫秒
```

### API 速查表

| API | 功能 | 精度 | 开销 |
|-----|------|------|------|
| `rte_get_timer_cycles()` | 获取当前周期数 | 最高 | 低 |
| `rte_rdtsc()` | 直接读取 TSC | 最高 | 最低 |
| `rte_rdtsc_precise()` | 精确读取 TSC | 最高 | 中等 |
| `rte_get_timer_hz()` | 获取时钟频率 | - | 低 |
| `rte_delay_us()` | 微秒级延时 | 高 | 占用 CPU |

## 性能测试最佳实践

### 基本模式

```c
uint64_t hz = rte_get_timer_hz();
uint64_t start, end;
int iterations = 1000000;

start = rte_get_timer_cycles();
for (int i = 0; i < iterations; i++) {
    your_function();
}
end = rte_get_timer_cycles();

uint64_t elapsed = end - start;
double per_op_ns = (double)elapsed * 1e9 / hz / iterations;
double ops_per_sec = (double)iterations * hz / elapsed;

printf("每次操作: %.2f 纳秒\n", per_op_ns);
printf("吞吐量: %.2f M ops/sec\n", ops_per_sec / 1e6);
```

### 避免常见陷阱

#### 1. 编译器优化

```c
// 错误: 循环可能被优化掉
for (int i = 0; i < 1000000; i++) {
    int x = i * 2;  // 结果未使用
}

// 正确: 使用 volatile 或内存屏障
volatile int result;
for (int i = 0; i < 1000000; i++) {
    result = i * 2;
}
```

#### 2. CPU 频率变化

现代 CPU 会动态调整频率,影响测量准确性。

**解决方案**:
```bash
# 设置为性能模式(固定最高频率)
sudo sh -c 'echo performance > \
  /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'
```

#### 3. 缓存预热

第一次访问内存较慢(缓存未命中)。

**解决方案**:
```c
// 预热阶段(不计入统计)
for (int i = 0; i < 1000; i++) {
    your_function();
}

// 正式测量
start = rte_get_timer_cycles();
for (int i = 0; i < iterations; i++) {
    your_function();
}
end = rte_get_timer_cycles();
```

#### 4. 测量时间太短

```c
// 错误: 只执行一次,误差大
start = rte_get_timer_cycles();
your_function();
end = rte_get_timer_cycles();

// 正确: 执行多次取平均值
start = rte_get_timer_cycles();
for (int i = 0; i < 1000000; i++) {
    your_function();
}
end = rte_get_timer_cycles();
double per_op = (end - start) / 1000000.0;
```

### 性能测试清单

在进行性能测试前,检查以下事项:

```
☐ CPU 频率模式设为 "performance"
☐ 禁用 CPU 节能特性(C-states)
☐ 隔离测试用的 CPU 核心(isolcpus)
☐ 关闭不必要的后台进程
☐ 代码已预热(排除冷启动影响)
☐ 迭代次数足够多(至少 100 万次)
☐ 使用了 volatile 或内存屏障防止优化
☐ 多次运行取中位数(排除异常值)
```

## 时间单位转换

### 常用转换

```c
uint64_t hz = rte_get_timer_hz();

// 时间 → 周期数
uint64_t one_second = hz;           // 1 秒
uint64_t one_millisecond = hz / 1000;    // 1 毫秒
uint64_t one_microsecond = hz / 1000000; // 1 微秒

// 周期数 → 时间
double time_us = (double)cycles * 1e6 / hz;   // 微秒
double time_ns = (double)cycles * 1e9 / hz;   // 纳秒
double time_ms = (double)cycles * 1e3 / hz;   // 毫秒
```

### 性能指标计算

```c
// 总时间(秒)
double total_time = (double)total_cycles / hz;

// 每次操作耗时(纳秒)
double per_op_ns = (double)total_cycles * 1e9 / hz / iterations;

// 吞吐量(ops/sec)
double ops_per_sec = (double)iterations * hz / total_cycles;

// 带宽(Gbps) - 用于内存拷贝测试
double bandwidth_gbps = (double)total_bytes * 8 / total_time_us / 1000;
```

## 典型性能数据

基于 2.4 GHz CPU 的典型测试结果:

| 操作 | 每次耗时 | 吞吐量 |
|------|---------|--------|
| 空循环 | ~0.4 ns | 2400 M ops/s |
| rte_get_timer_cycles() | ~10-30 ns | 100 M ops/s |
| Ring enqueue/dequeue | ~10-50 ns | 50 M ops/s |
| Mempool get/put | ~20-60 ns | 30 M ops/s |
| rte_malloc/free (64B) | ~100-300 ns | 5 M ops/s |
| rte_memcpy (1KB) | ~30-50 ns | 20 GB/s |

## 实用工具函数

```c
/* 时间转换函数 */
static inline double cycles_to_ns(uint64_t cycles, uint64_t hz) {
    return (double)cycles * 1000000000.0 / hz;
}

static inline double cycles_to_us(uint64_t cycles, uint64_t hz) {
    return (double)cycles * 1000000.0 / hz;
}

static inline double cycles_to_ms(uint64_t cycles, uint64_t hz) {
    return (double)cycles * 1000.0 / hz;
}

/* 简单的性能测试宏 */
#define BENCHMARK_START(name) \
    do { \
        const char *__bench_name = (name); \
        uint64_t __bench_start = rte_get_timer_cycles();

#define BENCHMARK_END(iterations) \
        uint64_t __bench_end = rte_get_timer_cycles(); \
        uint64_t __bench_elapsed = __bench_end - __bench_start; \
        double __bench_per_op = (double)__bench_elapsed * 1e9 / \
                                rte_get_timer_hz() / (iterations); \
        printf("[BENCH] %s: %.2f ns/op\n", __bench_name, __bench_per_op); \
    } while (0)

/* 使用示例 */
BENCHMARK_START("my_function");
for (int i = 0; i < 1000000; i++) {
    my_function();
}
BENCHMARK_END(1000000);
```

## 常见问题

### Q1: rte_get_timer_cycles() 和 rte_rdtsc() 有什么区别?

**A:**
- `rte_rdtsc()`: 直接读取 TSC,最快,但可能乱序执行
- `rte_rdtsc_precise()`: 添加了内存屏障,保证顺序
- `rte_get_timer_cycles()`: 更通用,跨平台兼容

一般情况下使用 `rte_get_timer_cycles()` 即可。

### Q2: 为什么测量结果每次都不一样?

**A:** 常见原因:
1. CPU 频率���态变化
2. 操作系统中断干扰
3. 缓存状态不同
4. 其他进程竞争 CPU

**解决方案**: 多次测量取中位数,使用隔离的 CPU 核心,设置性能模式。

### Q3: 如何测量纳秒级的操作?

**A:** 单次操作太快无法精确测量,需要:
1. 执行大量次数(如 1000 万次)
2. 测量总时间
3. 计算平均值

### Q4: TSC 会溢出吗?

**A:** TSC 是 64 位计数器,以 3GHz 频率计算:
```
2^64 / 3,000,000,000 / 3600 / 24 / 365 ≈ 195 年
```
不用担心溢出!

### Q5: 多核系统上不同核心的 TSC 一样吗?

**A:**
- 现代 CPU (Intel Nehalem 之后)的 TSC 是同步的
- DPDK 假设 TSC 是恒定速率且核心间同步的
- 检查 CPU 标志: `constant_tsc` 和 `nonstop_tsc`

```bash
grep -E "constant_tsc|nonstop_tsc" /proc/cpuinfo
```

## 系统优化建议

### CPU 频率设置

```bash
# 查看当前频率策略
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# 设置为性能模式
sudo sh -c 'echo performance > \
  /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'

# 查看当前频率
cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq
```

### CPU 隔离

在 grub 配置中添加:
```
GRUB_CMDLINE_LINUX="isolcpus=1-3 nohz_full=1-3"
```

然后运行:
```bash
sudo update-grub
sudo reboot
```

### 禁用 C-states

在 BIOS 中禁用 CPU C-states(节能状态),或使用:
```bash
sudo sh -c 'for i in /sys/devices/system/cpu/cpu*/cpuidle/state*/disable; \
  do echo 1 > $i; done'
```

## 相关文档

- [docs/lessons/lesson16-basic-time-cycles.md](../docs/lessons/lesson16-basic-time-cycles.md) - 时间和周期详细教程
- DPDK Programming Guide - Cycles and Timing
- Intel TSC Documentation

## 总结

### 核心要点

1. **rte_get_timer_hz()** 获取 CPU 时钟频率
2. **rte_get_timer_cycles()** 获取当前周期计数
3. 时间 = (周期数 × 时间单位) / 频率
4. **rte_delay_us/ms()** 用于精确延时
5. TSC 提供纳秒级计时精度

### 性能测试三步曲

```c
1. start = rte_get_timer_cycles()
2. 执行被测试代码
3. end = rte_get_timer_cycles()
   elapsed = end - start
```

### 下一步学习

- DPDK Timer 定时器: 基于时钟周期实现定时任务
- 性能调优: 使用这些工具优化你的 DPDK 应用
- 延迟分析: 测量数据包处理的端到端延迟
