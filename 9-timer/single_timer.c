#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>

#include <rte_common.h>
#include <rte_memory.h>
#include <rte_launch.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_timer.h>
#include <rte_debug.h>

static uint64_t timer_resolution_cycles;
static struct rte_timer timer1;

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
	lcore_id = rte_get_next_lcore(lcore_id, 0, 1);
	printf("[SINGLE] Manually reloading timer on next lcore %u\n", lcore_id);
	rte_timer_reset(tim, hz * 3, SINGLE, lcore_id, timer1_cb, NULL);
}

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
			rte_timer_manage();
			prev_tsc = cur_tsc;
		}
	}
}

int
main(int argc, char **argv)
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

	/* 加载timer1，每3秒触发一次，在下一个lcore上运行，手动重新加载 */
	hz = rte_get_timer_hz();
	timer_resolution_cycles = hz * 10 / 1000; /* 约10ms */

	lcore_id = rte_lcore_id();
	lcore_id = rte_get_next_lcore(lcore_id, 0, 1);
	printf("Setting up SINGLE timer on lcore %u, interval=3 seconds\n", lcore_id);
	printf("Timer will be manually reloaded on different cores each time\n");
	rte_timer_reset(&timer1, hz * 3, SINGLE, lcore_id, timer1_cb, NULL);

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
