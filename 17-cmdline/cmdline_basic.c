/*
 * DPDK Cmdline Example 1: Basic Commands
 * DPDK Cmdline 示例 1：基础命令使用
 *
 * This example demonstrates:
 * 本示例演示：
 * - Basic command structure and registration | 基础命令结构和注册
 * - Token initialization and parsing | Token 初始化和解析
 * - Help system integration | 帮助系统集成
 * - Simple statistics tracking | 简单的统计追踪
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include <rte_eal.h>
#include <rte_log.h>
#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_parse_string.h>
#include <cmdline_parse_num.h>
#include <cmdline_socket.h>
#include <cmdline.h>

#define VERSION_STRING "DPDK Cmdline Example v1.0"

/* Global statistics structure | 全局统计结构 */
struct app_stats {
	uint64_t cmd_count;      /* Total commands executed | 已执行命令总数 */
	uint64_t error_count;    /* Total errors | 错误总数 */
	time_t start_time;       /* Application start time | 应用启动时间 */
};

static struct app_stats g_stats = {0};
static volatile int quit_signal = 0;

/*
 * ============================================================================
 * Command: help - Display available commands
 * 命令：help - 显示可用命令
 * ============================================================================
 */
struct cmd_help_result {
	cmdline_fixed_string_t help;
};

static void cmd_help_parsed(__rte_unused void *parsed_result,
                            struct cmdline *cl,
                            __rte_unused void *data)
{
	cmdline_printf(cl, "\nAvailable Commands:\n");
	cmdline_printf(cl, "==================\n");
	cmdline_printf(cl, "  help                - Display this help message\n");
	cmdline_printf(cl, "  show version        - Display version information\n");
	cmdline_printf(cl, "  show stats          - Display statistics\n");
	cmdline_printf(cl, "  set loglevel <0-8>  - Set log level (0=emergency, 8=debug)\n");
	cmdline_printf(cl, "  clear stats         - Clear statistics counters\n");
	cmdline_printf(cl, "  quit                - Exit application\n");
	cmdline_printf(cl, "\nTips:\n");
	cmdline_printf(cl, "  - Use Tab key for auto-completion\n");
	cmdline_printf(cl, "  - Use Up/Down arrows for command history\n\n");

	g_stats.cmd_count++;
}

cmdline_parse_token_string_t cmd_help_tok =
	TOKEN_STRING_INITIALIZER(struct cmd_help_result, help, "help");

cmdline_parse_inst_t cmd_help = {
	.f = cmd_help_parsed,
	.data = NULL,
	.help_str = "help - Display available commands",
	.tokens = {
		(void *)&cmd_help_tok,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: show version - Display version information
 * 命令：show version - 显示版本信息
 * ============================================================================
 */
struct cmd_show_version_result {
	cmdline_fixed_string_t show;
	cmdline_fixed_string_t version;
};

static void cmd_show_version_parsed(__rte_unused void *parsed_result,
                                     struct cmdline *cl,
                                     __rte_unused void *data)
{
	cmdline_printf(cl, "\n%s\n", VERSION_STRING);
	cmdline_printf(cl, "Built with DPDK\n");
	cmdline_printf(cl, "Cmdline Library: Interactive command-line interface\n\n");

	g_stats.cmd_count++;
}

cmdline_parse_token_string_t cmd_show_version_show =
	TOKEN_STRING_INITIALIZER(struct cmd_show_version_result, show, "show");
cmdline_parse_token_string_t cmd_show_version_version =
	TOKEN_STRING_INITIALIZER(struct cmd_show_version_result, version, "version");

cmdline_parse_inst_t cmd_show_version = {
	.f = cmd_show_version_parsed,
	.data = NULL,
	.help_str = "show version - Display version information",
	.tokens = {
		(void *)&cmd_show_version_show,
		(void *)&cmd_show_version_version,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: show stats - Display statistics
 * 命令：show stats - 显示统计信息
 * ============================================================================
 */
struct cmd_show_stats_result {
	cmdline_fixed_string_t show;
	cmdline_fixed_string_t stats;
};

static void cmd_show_stats_parsed(__rte_unused void *parsed_result,
                                   struct cmdline *cl,
                                   __rte_unused void *data)
{
	time_t now = time(NULL);
	uint64_t uptime = (uint64_t)(now - g_stats.start_time);

	cmdline_printf(cl, "\n=== Application Statistics ===\n");
	cmdline_printf(cl, "Commands executed:  %lu\n", g_stats.cmd_count);
	cmdline_printf(cl, "Errors occurred:    %lu\n", g_stats.error_count);
	cmdline_printf(cl, "Uptime:             %lu seconds (%lu minutes)\n",
	               uptime, uptime / 60);
	cmdline_printf(cl, "Start time:         %s", ctime(&g_stats.start_time));
	cmdline_printf(cl, "\n");

	g_stats.cmd_count++;
}

cmdline_parse_token_string_t cmd_show_stats_show =
	TOKEN_STRING_INITIALIZER(struct cmd_show_stats_result, show, "show");
cmdline_parse_token_string_t cmd_show_stats_stats =
	TOKEN_STRING_INITIALIZER(struct cmd_show_stats_result, stats, "stats");

cmdline_parse_inst_t cmd_show_stats = {
	.f = cmd_show_stats_parsed,
	.data = NULL,
	.help_str = "show stats - Display application statistics",
	.tokens = {
		(void *)&cmd_show_stats_show,
		(void *)&cmd_show_stats_stats,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: set loglevel <level> - Set DPDK log level
 * 命令：set loglevel <level> - 设置 DPDK 日志级别
 * ============================================================================
 */
struct cmd_set_loglevel_result {
	cmdline_fixed_string_t set;
	cmdline_fixed_string_t loglevel;
	uint8_t level;
};

static void cmd_set_loglevel_parsed(void *parsed_result,
                                     struct cmdline *cl,
                                     __rte_unused void *data)
{
	struct cmd_set_loglevel_result *res = parsed_result;

	/* Validate log level range (0-8) | 验证日志级别范围 (0-8) */
	if (res->level > 8) {
		cmdline_printf(cl, "Error: Log level must be between 0 and 8\n");
		g_stats.error_count++;
		return;
	}

	/* Set global log level | 设置全局日志级别 */
	rte_log_set_global_level(res->level);

	const char *level_names[] = {
		"EMERGENCY", "ALERT", "CRITICAL", "ERROR",
		"WARNING", "NOTICE", "INFO", "DEBUG"
	};

	cmdline_printf(cl, "Log level set to %u (%s)\n",
	               res->level,
	               res->level <= 7 ? level_names[res->level] : "DEBUG");

	g_stats.cmd_count++;
}

cmdline_parse_token_string_t cmd_set_loglevel_set =
	TOKEN_STRING_INITIALIZER(struct cmd_set_loglevel_result, set, "set");
cmdline_parse_token_string_t cmd_set_loglevel_loglevel =
	TOKEN_STRING_INITIALIZER(struct cmd_set_loglevel_result, loglevel, "loglevel");
cmdline_parse_token_num_t cmd_set_loglevel_level =
	TOKEN_NUM_INITIALIZER(struct cmd_set_loglevel_result, level, RTE_UINT8);

cmdline_parse_inst_t cmd_set_loglevel = {
	.f = cmd_set_loglevel_parsed,
	.data = NULL,
	.help_str = "set loglevel <0-8> - Set log level",
	.tokens = {
		(void *)&cmd_set_loglevel_set,
		(void *)&cmd_set_loglevel_loglevel,
		(void *)&cmd_set_loglevel_level,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: clear stats - Clear statistics
 * 命令：clear stats - 清空统计信息
 * ============================================================================
 */
struct cmd_clear_stats_result {
	cmdline_fixed_string_t clear;
	cmdline_fixed_string_t stats;
};

static void cmd_clear_stats_parsed(__rte_unused void *parsed_result,
                                    struct cmdline *cl,
                                    __rte_unused void *data)
{
	uint64_t old_count = g_stats.cmd_count;

	g_stats.cmd_count = 0;
	g_stats.error_count = 0;
	g_stats.start_time = time(NULL);

	cmdline_printf(cl, "Statistics cleared (had %lu commands executed)\n",
	               old_count);
}

cmdline_parse_token_string_t cmd_clear_stats_clear =
	TOKEN_STRING_INITIALIZER(struct cmd_clear_stats_result, clear, "clear");
cmdline_parse_token_string_t cmd_clear_stats_stats =
	TOKEN_STRING_INITIALIZER(struct cmd_clear_stats_result, stats, "stats");

cmdline_parse_inst_t cmd_clear_stats = {
	.f = cmd_clear_stats_parsed,
	.data = NULL,
	.help_str = "clear stats - Clear statistics counters",
	.tokens = {
		(void *)&cmd_clear_stats_clear,
		(void *)&cmd_clear_stats_stats,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: quit - Exit application
 * 命令：quit - 退出应用程序
 * ============================================================================
 */
struct cmd_quit_result {
	cmdline_fixed_string_t quit;
};

static void cmd_quit_parsed(__rte_unused void *parsed_result,
                             struct cmdline *cl,
                             __rte_unused void *data)
{
	cmdline_printf(cl, "\nExiting... (executed %lu commands)\n\n",
	               g_stats.cmd_count);
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
 * Context: List of all commands
 * 上下文：所有命令的列表
 * ============================================================================
 */
cmdline_parse_ctx_t main_ctx[] = {
	(cmdline_parse_inst_t *)&cmd_help,
	(cmdline_parse_inst_t *)&cmd_show_version,
	(cmdline_parse_inst_t *)&cmd_show_stats,
	(cmdline_parse_inst_t *)&cmd_set_loglevel,
	(cmdline_parse_inst_t *)&cmd_clear_stats,
	(cmdline_parse_inst_t *)&cmd_quit,
	NULL,
};

/*
 * ============================================================================
 * Main function
 * 主函数
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

	/* Initialize statistics | 初始化统计信息 */
	g_stats.start_time = time(NULL);
	g_stats.cmd_count = 0;
	g_stats.error_count = 0;

	printf("\n");
	printf("==============================================\n");
	printf("  DPDK Cmdline Example 1: Basic Commands\n");
	printf("==============================================\n");
	printf("\n");
	printf("Welcome to DPDK cmdline interface!\n");
	printf("Type 'help' to see available commands.\n");
	printf("Use Tab for auto-completion, Up/Down for history.\n");
	printf("\n");

	/* Create cmdline instance | 创建 cmdline 实例 */
	cl = cmdline_stdin_new(main_ctx, "dpdk-basic> ");
	if (cl == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create cmdline instance\n");

	/* Enter interactive loop | 进入交互循环 */
	cmdline_interact(cl);

	/* Cleanup | 清理资源 */
	cmdline_stdin_exit(cl);

	/* Cleanup EAL | 清理 EAL */
	rte_eal_cleanup();

	return 0;
}
