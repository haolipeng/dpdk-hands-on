/*
 * DPDK Time and Cycles Demonstration
 * Lesson 16: Time, Clock Cycles and Performance Measurement Basics
 *
 * This example demonstrates:
 * 1. Getting CPU clock frequency
 * 2. Measuring code execution time using cycles
 * 3. Converting cycles to real time units
 * 4. Using DPDK delay functions
 * 5. Measuring API call overhead
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

#include <rte_eal.h>
#include <rte_cycles.h>
#include <rte_lcore.h>

/* 全局标志,用于优雅退出 */
static volatile int force_quit = 0;

/* 信号处理函数 */
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        force_quit = 1;
    }
}

/*
 * 将时钟周期转换为微秒
 */
static inline double cycles_to_us(uint64_t cycles, uint64_t hz)
{
    return (double)cycles * 1000000.0 / hz;
}

/*
 * 将时钟周期转换为纳秒
 */
static inline double cycles_to_ns(uint64_t cycles, uint64_t hz)
{
    return (double)cycles * 1000000000.0 / hz;
}

/*
 * 将时钟周期转换为毫秒
 */
static inline double cycles_to_ms(uint64_t cycles, uint64_t hz)
{
    return (double)cycles * 1000.0 / hz;
}

/*
 * 模拟一些工作负载
 */
static void do_some_work(int iterations)
{
    volatile int sum = 0;
    for (int i = 0; i < iterations; i++) {
        sum += i;
    }
}

/*
 * 演示获取时钟频率
 */
static void demo_get_frequency(uint64_t hz)
{
    printf("\n");
    printf("【第1部分】获取 CPU 时钟频率\n");
    printf("─────────────────────────────────────────────\n");

    printf("  时钟频率: %lu Hz\n", hz);
    printf("  即: %.3f GHz\n", hz / 1e9);
    printf("  每个时钟周期: %.3f 纳秒\n\n", 1e9 / hz);

    /* 打印常用时间单位对应的周期数 */
    printf("  常用时间单位对应的周期数:\n");
    printf("  ┌──────────────┬────────────────────┐\n");
    printf("  │ 时间         │ 周期数              │\n");
    printf("  ├──────────────┼────────────────────┤\n");
    printf("  │ 1 秒         │ %-18lu │\n", hz);
    printf("  │ 1 毫秒       │ %-18lu │\n", hz / 1000);
    printf("  │ 1 微秒       │ %-18lu │\n", hz / 1000000);
    printf("  │ 100 纳秒     │ %-18lu │\n", hz / 10000000);
    printf("  └──────────────┴────────────────────┘\n");

    printf("\n  💡 说明:\n");
    printf("     - 时钟频率是每秒的时钟周期数\n");
    printf("     - 频率越高,每个周期越短\n");
    printf("     - 使用周期计数可以实现纳秒级精度测量\n");
}

/*
 * 演示测量代码执行时间
 */
static void demo_measure_execution_time(uint64_t hz)
{
    uint64_t start, end, elapsed;

    printf("\n");
    printf("【第2部分】测量代码执行时间\n");
    printf("─────────────────────────────────────────────\n");

    /* 实验1: 测量小循环 */
    printf("\n  实验1: 测量 1000 次循环\n");
    start = rte_get_timer_cycles();
    do_some_work(1000);
    end = rte_get_timer_cycles();
    elapsed = end - start;

    printf("    开始周期: %lu\n", start);
    printf("    结束周期: %lu\n", end);
    printf("    消耗周期: %lu\n", elapsed);
    printf("    消耗时间: %.2f 微秒\n", cycles_to_us(elapsed, hz));
    printf("    消耗时间: %.0f 纳秒\n", cycles_to_ns(elapsed, hz));

    /* 实验2: 测量较大循环 */
    printf("\n  实验2: 测量 100000 次循环\n");
    start = rte_get_timer_cycles();
    do_some_work(100000);
    end = rte_get_timer_cycles();
    elapsed = end - start;

    printf("    消耗周期: %lu\n", elapsed);
    printf("    消耗时间: %.2f 微秒\n", cycles_to_us(elapsed, hz));

    /* 实验3: 测量更大循环 */
    printf("\n  实验3: 测量 10000000 次循环\n");
    start = rte_get_timer_cycles();
    do_some_work(10000000);
    end = rte_get_timer_cycles();
    elapsed = end - start;

    printf("    消耗周期: %lu\n", elapsed);
    printf("    消耗时间: %.2f 毫秒\n", cycles_to_ms(elapsed, hz));

    printf("\n  💡 说明:\n");
    printf("     - rte_get_timer_cycles() 读取 CPU 的 TSC 寄存器\n");
    printf("     - TSC 是一个 64 位计数器,从开机一直递增\n");
    printf("     - 通过 (end - start) 计算消耗的周期数\n");
}

/*
 * 演示 DPDK 延时函数
 */
static void demo_delay_functions(uint64_t hz)
{
    uint64_t start, end, elapsed;

    printf("\n");
    printf("【第3部分】DPDK 延时函数\n");
    printf("─────────────────────────────────────────────\n");

    /* 测试 rte_delay_us() */
    printf("\n  测试1: rte_delay_us(100) - 延时 100 微秒\n");
    printf("    请求延时: 100 微秒\n");
    start = rte_get_timer_cycles();
    rte_delay_us(100);
    end = rte_get_timer_cycles();
    elapsed = end - start;

    double actual_us = cycles_to_us(elapsed, hz);
    double error = (actual_us - 100.0) / 100.0 * 100;

    printf("    实际延时: %.2f 微秒\n", actual_us);
    printf("    误差: %.2f%%\n", error);

    /* 测试 rte_delay_ms() */
    printf("\n  测试2: rte_delay_ms(10) - 延时 10 毫秒\n");
    printf("    请求延时: 10 毫秒\n");
    start = rte_get_timer_cycles();
    rte_delay_ms(10);
    end = rte_get_timer_cycles();
    elapsed = end - start;

    double actual_ms = cycles_to_ms(elapsed, hz);
    error = (actual_ms - 10.0) / 10.0 * 100;

    printf("    实际延时: %.2f 毫秒\n", actual_ms);
    printf("    误差: %.2f%%\n", error);

    /* 测试更短的延时 */
    printf("\n  测试3: rte_delay_us(1) - 延时 1 微秒\n");
    printf("    请求延时: 1 微秒\n");
    start = rte_get_timer_cycles();
    rte_delay_us(1);
    end = rte_get_timer_cycles();
    elapsed = end - start;

    actual_us = cycles_to_us(elapsed, hz);

    printf("    实际延时: %.2f 微秒\n", actual_us);
    printf("    实际延时: %.0f 纳秒\n", cycles_to_ns(elapsed, hz));

    printf("\n  ⚠️  注意:\n");
    printf("     - rte_delay_us/ms 是忙等待(busy-wait),会占用 CPU\n");
    printf("     - 适合短时间精确延时,不适合长时间等待\n");
    printf("     - 误差通常在 1-5%% 范围内\n");
}

/*
 * 演示测量 API 调用开销
 */
static void demo_api_overhead(uint64_t hz)
{
    uint64_t start, end;
    uint64_t overhead_total = 0;
    int num_iterations = 1000000;

    printf("\n");
    printf("【第4部分】测量 API 调用开销\n");
    printf("─────────────────────────────────────────────\n");

    /* 测量 rte_get_timer_cycles() 的开销 */
    printf("\n  测量 rte_get_timer_cycles() 的调用开销...\n");
    printf("  迭代次数: %d\n", num_iterations);

    for (int i = 0; i < num_iterations; i++) {
        start = rte_get_timer_cycles();
        end = rte_get_timer_cycles();
        overhead_total += (end - start);
    }

    double avg_overhead_cycles = (double)overhead_total / num_iterations;
    double avg_overhead_ns = avg_overhead_cycles * 1e9 / hz;

    printf("\n  结果:\n");
    printf("    总周期数: %lu\n", overhead_total);
    printf("    平均周期数: %.1f cycles/call\n", avg_overhead_cycles);
    printf("    平均时间: %.1f 纳秒/call\n", avg_overhead_ns);

    /* 测量 rte_rdtsc() 的开销 */
    overhead_total = 0;
    printf("\n  测量 rte_rdtsc() 的调用开销...\n");
    printf("  迭代次数: %d\n", num_iterations);

    for (int i = 0; i < num_iterations; i++) {
        start = rte_rdtsc();
        end = rte_rdtsc();
        overhead_total += (end - start);
    }

    avg_overhead_cycles = (double)overhead_total / num_iterations;
    avg_overhead_ns = avg_overhead_cycles * 1e9 / hz;

    printf("\n  结果:\n");
    printf("    总周期数: %lu\n", overhead_total);
    printf("    平均周期数: %.1f cycles/call\n", avg_overhead_cycles);
    printf("    平均时间: %.1f 纳秒/call\n", avg_overhead_ns);

    printf("\n  💡 说明:\n");
    printf("     - rte_rdtsc() 直接读取 TSC,开销最小\n");
    printf("     - rte_get_timer_cycles() 封装了 rdtsc,跨平台兼容\n");
    printf("     - 典型开销在 10-30 纳秒之间\n");
}

/*
 * 演示时间单位转换
 */
static void demo_time_conversion(uint64_t hz)
{
    printf("\n");
    printf("【第5部分】时间单位转换\n");
    printf("─────────────────────────────────────────────\n");

    printf("\n  时间 → 周期数转换:\n");
    printf("  ┌──────────────┬────────────────────┐\n");
    printf("  │ 时间         │ 周期数              │\n");
    printf("  ├──────────────┼────────────────────┤\n");
    printf("  │ 1 秒         │ %-18lu │\n", hz);
    printf("  │ 100 毫秒     │ %-18lu │\n", hz / 10);
    printf("  │ 10 毫秒      │ %-18lu │\n", hz / 100);
    printf("  │ 1 毫秒       │ %-18lu │\n", hz / 1000);
    printf("  │ 100 微秒     │ %-18lu │\n", hz / 10000);
    printf("  │ 10 微秒      │ %-18lu │\n", hz / 100000);
    printf("  │ 1 微秒       │ %-18lu │\n", hz / 1000000);
    printf("  └──────────────┴────────────────────┘\n");

    printf("\n  周期数 → 时间转换示例:\n");

    uint64_t sample_cycles[] = {2400, 24000, 240000, 2400000, 24000000};
    const char *cycle_desc[] = {"2.4K", "24K", "240K", "2.4M", "24M"};
    int num_samples = sizeof(sample_cycles) / sizeof(sample_cycles[0]);

    printf("  ┌──────────┬──────────┬──────────┬──────────┐\n");
    printf("  │ 周期数   │ 纳秒     │ 微秒     │ 毫秒     │\n");
    printf("  ├──────────┼──────────┼──────────┼──────────┤\n");

    for (int i = 0; i < num_samples; i++) {
        printf("  │ %-8s │ %8.0f │ %8.2f │ %8.3f │\n",
               cycle_desc[i],
               cycles_to_ns(sample_cycles[i], hz),
               cycles_to_us(sample_cycles[i], hz),
               cycles_to_ms(sample_cycles[i], hz));
    }

    printf("  └──────────┴──────────┴──────────┴──────────┘\n");

    printf("\n  转换公式:\n");
    printf("    时间(秒)   = 周期数 / 频率\n");
    printf("    时间(毫秒) = 周期数 * 1000 / 频率\n");
    printf("    时间(微秒) = 周期数 * 1000000 / 频率\n");
    printf("    时间(纳秒) = 周期数 * 1000000000 / 频率\n");
}

/*
 * 演示不同 API 的对比
 */
static void demo_api_comparison(uint64_t hz)
{
    printf("\n");
    printf("【第6部分】不同计时 API 对比\n");
    printf("─────────────────────────────────────────────\n");

    printf("\n  API 特性对比:\n");
    printf("  ┌─────────────────────────┬──────────┬──────────┐\n");
    printf("  │ API                     │ 精度     │ 开销     │\n");
    printf("  ├─────────────────────────┼──────────┼──────────┤\n");
    printf("  │ rte_get_timer_cycles()  │ 最高     │ 低       │\n");
    printf("  │ rte_rdtsc()             │ 最高     │ 最低     │\n");
    printf("  │ rte_rdtsc_precise()     │ 最高     │ 中等     │\n");
    printf("  │ gettimeofday()          │ 微秒     │ 高       │\n");
    printf("  │ clock_gettime()         │ 纳秒     │ 高       │\n");
    printf("  └─────────────────────────┴──────────┴──────────┘\n");

    printf("\n  推荐使用场景:\n");
    printf("    • rte_get_timer_cycles(): 通用场景,推荐使用\n");
    printf("    • rte_rdtsc():           追求极致性能\n");
    printf("    • rte_rdtsc_precise():   需要严格内存顺序时\n");
    printf("    • rte_delay_us():        精确短时间延时\n");
    printf("    • rte_delay_ms():        毫秒级延时\n");

    printf("\n  💡 核心概念:\n");
    printf("     - TSC (Time Stamp Counter): CPU 内置的 64 位计数器\n");
    printf("     - 每个时钟周期 TSC +1\n");
    printf("     - 现代 CPU 的 TSC 是恒定频率且多核同步的\n");
    printf("     - DPDK 利用 TSC 实现纳秒级精度计时\n");
}

/*
 * 主函数
 */
int main(int argc, char **argv)
{
    int ret;
    uint64_t hz;

    /* 注册信号处理 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 初始化 EAL */
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_panic("Cannot init EAL\n");

    /* 打印欢迎信息 */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║   DPDK 时间与时钟周期演示 - Lesson 16                 ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");

    /* 获取时钟频率 */
    hz = rte_get_timer_hz();

    printf("\n系统信息:\n");
    printf("  Lcore ID: %u\n", rte_lcore_id());
    printf("  总 lcore 数: %u\n", rte_lcore_count());
    printf("  时钟频率: %lu Hz (%.3f GHz)\n", hz, hz / 1e9);

    /* 运行所有演示 */
    if (!force_quit) {
        demo_get_frequency(hz);
    }

    if (!force_quit) {
        demo_measure_execution_time(hz);
    }

    if (!force_quit) {
        demo_delay_functions(hz);
    }

    if (!force_quit) {
        demo_api_overhead(hz);
    }

    if (!force_quit) {
        demo_time_conversion(hz);
    }

    if (!force_quit) {
        demo_api_comparison(hz);
    }

    /* 总结 */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("                         总结\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n关键要点:\n");
    printf("  1. rte_get_timer_hz() 获取 CPU 时钟频率\n");
    printf("  2. rte_get_timer_cycles() 获取当前周期计数\n");
    printf("  3. 时间 = (周期数 * 时间单位) / 频率\n");
    printf("  4. rte_delay_us/ms() 用于精确延时\n");
    printf("  5. TSC 提供纳秒级计时精度\n");

    printf("\n性能测试三步曲:\n");
    printf("  1. start = rte_get_timer_cycles()\n");
    printf("  2. 执行被测试代码\n");
    printf("  3. end = rte_get_timer_cycles()\n");
    printf("     elapsed = end - start\n");

    printf("\n下一步:\n");
    printf("  运行 benchmark 示例查看完整的性能测试框架:\n");
    printf("  sudo ./bin/benchmark -l 0 --no-pci\n");

    /* 清理 EAL */
    rte_eal_cleanup();

    printf("\n程序正常退出.\n");
    return 0;
}
