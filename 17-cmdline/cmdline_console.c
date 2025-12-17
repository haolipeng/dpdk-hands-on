/*
 * DPDK Cmdline Example 4: Management Console
 * DPDK Cmdline 示例 4：完整管理控制台
 *
 * This example demonstrates a production-ready management console with:
 * 本示例演示生产级管理控制台，包括：
 * - Ring management | Ring 管理
 * - Mempool management | Mempool 管理
 * - System information queries | 系统信息查询
 * - Statistics tracking and export | 统计追踪和导出
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_malloc.h>
#include <rte_lcore.h>
#include <rte_launch.h>
#include <rte_memory.h>
#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_parse_string.h>
#include <cmdline_parse_num.h>
#include <cmdline_socket.h>
#include <cmdline.h>

/* Global statistics | 全局统计 */
struct console_stats {
	uint64_t total_commands;
	uint64_t ring_ops;
	uint64_t mempool_ops;
	time_t start_time;
};

static struct console_stats g_stats = {0};
static volatile int quit_signal = 0;

/* Simple ring tracking | 简单的 Ring 追踪 */
struct tracked_ring {
	char name[32];
	struct tracked_ring *next;
};

static struct tracked_ring *g_rings = NULL;

static void track_ring(const char *name)
{
	struct tracked_ring *tr = malloc(sizeof(*tr));
	if (tr) {
		strncpy(tr->name, name, sizeof(tr->name) - 1);
		tr->next = g_rings;
		g_rings = tr;
	}
}

/*
 * ============================================================================
 * Ring Commands | Ring 命令组
 * ============================================================================
 */

/* ring create */
struct cmd_ring_create_result {
	cmdline_fixed_string_t ring;
	cmdline_fixed_string_t create;
	cmdline_fixed_string_t name;
	uint32_t size;
};

static void cmd_ring_create_parsed(void *parsed_result, struct cmdline *cl,
                                     __rte_unused void *data)
{
	struct cmd_ring_create_result *res = parsed_result;
	struct rte_ring *ring;

	ring = rte_ring_create(res->name, res->size, rte_socket_id(), 0);
	if (!ring) {
		cmdline_printf(cl, "Failed to create ring '%s'\n", res->name);
		return;
	}

	track_ring(res->name);
	g_stats.ring_ops++;
	g_stats.total_commands++;
	cmdline_printf(cl, "Ring '%s' created (size: %u)\n", res->name, res->size);
}

cmdline_parse_token_string_t cmd_ring_create_ring =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_create_result, ring, "ring");
cmdline_parse_token_string_t cmd_ring_create_create =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_create_result, create, "create");
cmdline_parse_token_string_t cmd_ring_create_name =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_create_result, name, NULL);
cmdline_parse_token_num_t cmd_ring_create_size =
	TOKEN_NUM_INITIALIZER(struct cmd_ring_create_result, size, RTE_UINT32);

cmdline_parse_inst_t cmd_ring_create = {
	.f = cmd_ring_create_parsed,
	.data = NULL,
	.help_str = "ring create <name> <size> - Create ring",
	.tokens = {
		(void *)&cmd_ring_create_ring,
		(void *)&cmd_ring_create_create,
		(void *)&cmd_ring_create_name,
		(void *)&cmd_ring_create_size,
		NULL,
	},
};

/* ring list */
struct cmd_ring_list_result {
	cmdline_fixed_string_t ring;
	cmdline_fixed_string_t list;
};

static void cmd_ring_list_parsed(__rte_unused void *parsed_result,
                                   struct cmdline *cl,
                                   __rte_unused void *data)
{
	struct tracked_ring *tr = g_rings;
	int count = 0;

	cmdline_printf(cl, "\nRings:\n");
	while (tr) {
		struct rte_ring *r = rte_ring_lookup(tr->name);
		if (r) {
			cmdline_printf(cl, "  %s (size: %u, used: %u)\n",
			               tr->name,
			               rte_ring_get_capacity(r),
			               rte_ring_count(r));
			count++;
		}
		tr = tr->next;
	}

	if (count == 0)
		cmdline_printf(cl, "  (none)\n");

	cmdline_printf(cl, "Total: %d\n\n", count);
	g_stats.total_commands++;
}

cmdline_parse_token_string_t cmd_ring_list_ring =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_list_result, ring, "ring");
cmdline_parse_token_string_t cmd_ring_list_list =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_list_result, list, "list");

cmdline_parse_inst_t cmd_ring_list = {
	.f = cmd_ring_list_parsed,
	.data = NULL,
	.help_str = "ring list - List all rings",
	.tokens = {
		(void *)&cmd_ring_list_ring,
		(void *)&cmd_ring_list_list,
		NULL,
	},
};

/*
 * ============================================================================
 * Mempool Commands | Mempool 命令组
 * ============================================================================
 */

/* mempool create */
struct cmd_mempool_create_result {
	cmdline_fixed_string_t mempool;
	cmdline_fixed_string_t create;
	cmdline_fixed_string_t name;
	uint32_t n;
	uint32_t elt_size;
};

static void cmd_mempool_create_parsed(void *parsed_result, struct cmdline *cl,
                                        __rte_unused void *data)
{
	struct cmd_mempool_create_result *res = parsed_result;
	struct rte_mempool *mp;

	mp = rte_mempool_create(res->name, res->n, res->elt_size, 0, 0,
	                         NULL, NULL, NULL, NULL,
	                         rte_socket_id(), 0);
	if (!mp) {
		cmdline_printf(cl, "Failed to create mempool '%s'\n", res->name);
		return;
	}

	g_stats.mempool_ops++;
	g_stats.total_commands++;
	cmdline_printf(cl, "Mempool '%s' created (n: %u, elt_size: %u)\n",
	               res->name, res->n, res->elt_size);
}

cmdline_parse_token_string_t cmd_mempool_create_mempool =
	TOKEN_STRING_INITIALIZER(struct cmd_mempool_create_result, mempool, "mempool");
cmdline_parse_token_string_t cmd_mempool_create_create =
	TOKEN_STRING_INITIALIZER(struct cmd_mempool_create_result, create, "create");
cmdline_parse_token_string_t cmd_mempool_create_name =
	TOKEN_STRING_INITIALIZER(struct cmd_mempool_create_result, name, NULL);
cmdline_parse_token_num_t cmd_mempool_create_n =
	TOKEN_NUM_INITIALIZER(struct cmd_mempool_create_result, n, RTE_UINT32);
cmdline_parse_token_num_t cmd_mempool_create_elt_size =
	TOKEN_NUM_INITIALIZER(struct cmd_mempool_create_result, elt_size, RTE_UINT32);

cmdline_parse_inst_t cmd_mempool_create = {
	.f = cmd_mempool_create_parsed,
	.data = NULL,
	.help_str = "mempool create <name> <n> <elt_size> - Create mempool",
	.tokens = {
		(void *)&cmd_mempool_create_mempool,
		(void *)&cmd_mempool_create_create,
		(void *)&cmd_mempool_create_name,
		(void *)&cmd_mempool_create_n,
		(void *)&cmd_mempool_create_elt_size,
		NULL,
	},
};

/*
 * ============================================================================
 * System Commands | 系统命令组
 * ============================================================================
 */

/* lcore list */
struct cmd_lcore_list_result {
	cmdline_fixed_string_t lcore;
	cmdline_fixed_string_t list;
};

static void cmd_lcore_list_parsed(__rte_unused void *parsed_result,
                                    struct cmdline *cl,
                                    __rte_unused void *data)
{
	unsigned int lcore_id;

	cmdline_printf(cl, "\n%-8s %-8s\n", "Lcore", "Socket");
	cmdline_printf(cl, "-------------------\n");

	RTE_LCORE_FOREACH(lcore_id) {
		cmdline_printf(cl, "%-8u %-8u\n",
		               lcore_id,
		               rte_lcore_to_socket_id(lcore_id));
	}

	cmdline_printf(cl, "\n");
	g_stats.total_commands++;
}

cmdline_parse_token_string_t cmd_lcore_list_lcore =
	TOKEN_STRING_INITIALIZER(struct cmd_lcore_list_result, lcore, "lcore");
cmdline_parse_token_string_t cmd_lcore_list_list =
	TOKEN_STRING_INITIALIZER(struct cmd_lcore_list_result, list, "list");

cmdline_parse_inst_t cmd_lcore_list = {
	.f = cmd_lcore_list_parsed,
	.data = NULL,
	.help_str = "lcore list - List all lcores",
	.tokens = {
		(void *)&cmd_lcore_list_lcore,
		(void *)&cmd_lcore_list_list,
		NULL,
	},
};

/* memory info */
struct cmd_memory_info_result {
	cmdline_fixed_string_t memory;
	cmdline_fixed_string_t info;
};

static void cmd_memory_info_parsed(__rte_unused void *parsed_result,
                                     struct cmdline *cl,
                                     __rte_unused void *data)
{
	unsigned int socket_id;

	cmdline_printf(cl, "\n=== Memory Information ===\n\n");

	for (socket_id = 0; socket_id < RTE_MAX_NUMA_NODES; socket_id++) {
		struct rte_malloc_socket_stats stats;

		if (rte_malloc_get_socket_stats(socket_id, &stats) < 0)
			continue;

		if (stats.heap_totalsz_bytes == 0)
			continue;

		cmdline_printf(cl, "Socket %u:\n", socket_id);
		cmdline_printf(cl, "  Total:     %lu MB\n",
		               stats.heap_totalsz_bytes / (1024 * 1024));
		cmdline_printf(cl, "  Allocated: %lu MB\n",
		               stats.heap_allocsz_bytes / (1024 * 1024));
		cmdline_printf(cl, "  Free:      %lu MB\n\n",
		               stats.heap_freesz_bytes / (1024 * 1024));
	}

	g_stats.total_commands++;
}

cmdline_parse_token_string_t cmd_memory_info_memory =
	TOKEN_STRING_INITIALIZER(struct cmd_memory_info_result, memory, "memory");
cmdline_parse_token_string_t cmd_memory_info_info =
	TOKEN_STRING_INITIALIZER(struct cmd_memory_info_result, info, "info");

cmdline_parse_inst_t cmd_memory_info = {
	.f = cmd_memory_info_parsed,
	.data = NULL,
	.help_str = "memory info - Display memory information",
	.tokens = {
		(void *)&cmd_memory_info_memory,
		(void *)&cmd_memory_info_info,
		NULL,
	},
};

/*
 * ============================================================================
 * Statistics Commands | 统计命令组
 * ============================================================================
 */

/* stats show */
struct cmd_stats_show_result {
	cmdline_fixed_string_t stats;
	cmdline_fixed_string_t show;
};

static void cmd_stats_show_parsed(__rte_unused void *parsed_result,
                                    struct cmdline *cl,
                                    __rte_unused void *data)
{
	time_t now = time(NULL);
	uint64_t uptime = (uint64_t)(now - g_stats.start_time);

	cmdline_printf(cl, "\n=== Console Statistics ===\n");
	cmdline_printf(cl, "Total commands:    %lu\n", g_stats.total_commands);
	cmdline_printf(cl, "Ring operations:   %lu\n", g_stats.ring_ops);
	cmdline_printf(cl, "Mempool operations: %lu\n", g_stats.mempool_ops);
	cmdline_printf(cl, "Uptime:            %lu seconds\n", uptime);
	cmdline_printf(cl, "Start time:        %s\n", ctime(&g_stats.start_time));

	g_stats.total_commands++;
}

cmdline_parse_token_string_t cmd_stats_show_stats =
	TOKEN_STRING_INITIALIZER(struct cmd_stats_show_result, stats, "stats");
cmdline_parse_token_string_t cmd_stats_show_show =
	TOKEN_STRING_INITIALIZER(struct cmd_stats_show_result, show, "show");

cmdline_parse_inst_t cmd_stats_show = {
	.f = cmd_stats_show_parsed,
	.data = NULL,
	.help_str = "stats show - Display statistics",
	.tokens = {
		(void *)&cmd_stats_show_stats,
		(void *)&cmd_stats_show_show,
		NULL,
	},
};

/* stats export */
struct cmd_stats_export_result {
	cmdline_fixed_string_t stats;
	cmdline_fixed_string_t export_cmd;
	cmdline_fixed_string_t filename;
};

static void cmd_stats_export_parsed(void *parsed_result,
                                      struct cmdline *cl,
                                      __rte_unused void *data)
{
	struct cmd_stats_export_result *res = parsed_result;
	FILE *fp;

	fp = fopen(res->filename, "w");
	if (!fp) {
		cmdline_printf(cl, "Error: Cannot open file '%s'\n", res->filename);
		return;
	}

	fprintf(fp, "=== DPDK Console Statistics ===\n");
	fprintf(fp, "Start Time: %s", ctime(&g_stats.start_time));
	fprintf(fp, "Total Commands: %lu\n", g_stats.total_commands);
	fprintf(fp, "Ring Operations: %lu\n", g_stats.ring_ops);
	fprintf(fp, "Mempool Operations: %lu\n", g_stats.mempool_ops);

	fclose(fp);

	cmdline_printf(cl, "Statistics exported to '%s'\n", res->filename);
	g_stats.total_commands++;
}

cmdline_parse_token_string_t cmd_stats_export_stats =
	TOKEN_STRING_INITIALIZER(struct cmd_stats_export_result, stats, "stats");
cmdline_parse_token_string_t cmd_stats_export_export =
	TOKEN_STRING_INITIALIZER(struct cmd_stats_export_result, export_cmd, "export");
cmdline_parse_token_string_t cmd_stats_export_filename =
	TOKEN_STRING_INITIALIZER(struct cmd_stats_export_result, filename, NULL);

cmdline_parse_inst_t cmd_stats_export = {
	.f = cmd_stats_export_parsed,
	.data = NULL,
	.help_str = "stats export <filename> - Export statistics to file",
	.tokens = {
		(void *)&cmd_stats_export_stats,
		(void *)&cmd_stats_export_export,
		(void *)&cmd_stats_export_filename,
		NULL,
	},
};

/*
 * ============================================================================
 * Utility Commands | 工具命令
 * ============================================================================
 */

/* help */
struct cmd_help_result {
	cmdline_fixed_string_t help;
};

static void cmd_help_parsed(__rte_unused void *parsed_result,
                            struct cmdline *cl,
                            __rte_unused void *data)
{
	cmdline_printf(cl, "\nDPDK Management Console Commands:\n");
	cmdline_printf(cl, "==================================\n\n");
	cmdline_printf(cl, "Ring Management:\n");
	cmdline_printf(cl, "  ring create <name> <size>     - Create ring\n");
	cmdline_printf(cl, "  ring list                     - List rings\n\n");
	cmdline_printf(cl, "Mempool Management:\n");
	cmdline_printf(cl, "  mempool create <name> <n> <sz> - Create mempool\n\n");
	cmdline_printf(cl, "System Information:\n");
	cmdline_printf(cl, "  lcore list                    - List lcores\n");
	cmdline_printf(cl, "  memory info                   - Memory info\n\n");
	cmdline_printf(cl, "Statistics:\n");
	cmdline_printf(cl, "  stats show                    - Display stats\n");
	cmdline_printf(cl, "  stats export <file>           - Export stats\n\n");
	cmdline_printf(cl, "Utility:\n");
	cmdline_printf(cl, "  help                          - This help\n");
	cmdline_printf(cl, "  quit                          - Exit\n\n");
}

cmdline_parse_token_string_t cmd_help_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_help_result, help, "help");

cmdline_parse_inst_t cmd_help = {
	.f = cmd_help_parsed,
	.data = NULL,
	.help_str = "help - Display help",
	.tokens = {
		(void *)&cmd_help_tok,
		NULL,
	},
};

/* quit */
struct cmd_quit_result {
	cmdline_fixed_string_t quit;
};

static void cmd_quit_parsed(__rte_unused void *parsed_result,
                             struct cmdline *cl,
                             __rte_unused void *data)
{
	cmdline_printf(cl, "\nExiting (executed %lu commands)...\n\n",
	               g_stats.total_commands);
	quit_signal = 1;
	cmdline_quit(cl);
}

cmdline_parse_token_string_t cmd_quit_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_quit_result, quit, "quit");

cmdline_parse_inst_t cmd_quit = {
	.f = cmd_quit_parsed,
	.data = NULL,
	.help_str = "quit - Exit application",
	.tokens = {
		(void *)&cmd_quit_tok,
		NULL,
	},
};

/*
 * ============================================================================
 * Context: All commands
 * ============================================================================
 */
cmdline_parse_ctx_t main_ctx[] = {
	(cmdline_parse_inst_t *)&cmd_help,
	(cmdline_parse_inst_t *)&cmd_ring_create,
	(cmdline_parse_inst_t *)&cmd_ring_list,
	(cmdline_parse_inst_t *)&cmd_mempool_create,
	(cmdline_parse_inst_t *)&cmd_lcore_list,
	(cmdline_parse_inst_t *)&cmd_memory_info,
	(cmdline_parse_inst_t *)&cmd_stats_show,
	(cmdline_parse_inst_t *)&cmd_stats_export,
	(cmdline_parse_inst_t *)&cmd_quit,
	NULL,
};

/*
 * ============================================================================
 * Main function
 * ============================================================================
 */
int main(int argc, char **argv)
{
	struct cmdline *cl;
	int ret;

	/* Initialize EAL | 初始化 EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "EAL initialization failed\n");

	/* Initialize statistics | 初始化统计 */
	g_stats.start_time = time(NULL);

	printf("\n");
	printf("==================================================\n");
	printf("  DPDK Management Console\n");
	printf("==================================================\n");
	printf("\n");
	printf("Full-featured management console with:\n");
	printf("- Ring and mempool management\n");
	printf("- System information queries\n");
	printf("- Statistics tracking and export\n");
	printf("\nType 'help' for available commands.\n\n");

	/* Create cmdline instance | 创建 cmdline 实例 */
	cl = cmdline_stdin_new(main_ctx, "dpdk-console> ");
	if (cl == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create cmdline instance\n");

	/* Enter interactive loop | 进入交互循环 */
	cmdline_interact(cl);

	/* Cleanup | 清理资源 */
	cmdline_stdin_exit(cl);
	rte_eal_cleanup();

	return 0;
}
