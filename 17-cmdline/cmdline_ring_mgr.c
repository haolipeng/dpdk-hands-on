/*
 * DPDK Cmdline Example 3: Ring Manager with Dynamic Completion
 * DPDK Cmdline 示例 3：带动态补全的 Ring 管理器
 *
 * This example demonstrates:
 * 本示例演示：
 * - Ring lifecycle management | Ring 生命周期管理
 * - Dynamic token completion | 动态 Token 补全
 * - Custom token types | 自定义 Token 类型
 * - Integration with DPDK rings | 与 DPDK Ring 的集成
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <rte_eal.h>
#include <rte_ring.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_parse_string.h>
#include <cmdline_parse_num.h>
#include <cmdline_socket.h>
#include <cmdline.h>

#define MAX_RING_NAME 32
#define DEFAULT_RING_SIZE 1024

static volatile int quit_signal = 0;

/* Managed ring structure | Ring 管理结构 */
struct managed_ring {
	struct rte_ring *ring;
	char name[MAX_RING_NAME];
	uint64_t enq_count;
	uint64_t deq_count;
	struct managed_ring *next;
};

static struct managed_ring *g_ring_list = NULL;

/* Helper functions | 辅助函数 */
static struct managed_ring *find_ring(const char *name)
{
	struct managed_ring *mr = g_ring_list;
	while (mr) {
		if (strcmp(mr->name, name) == 0)
			return mr;
		mr = mr->next;
	}
	return NULL;
}

static void add_ring_to_list(struct managed_ring *mr)
{
	mr->next = g_ring_list;
	g_ring_list = mr;
}

static void remove_ring_from_list(struct managed_ring *mr)
{
	struct managed_ring **pp = &g_ring_list;
	while (*pp) {
		if (*pp == mr) {
			*pp = mr->next;
			return;
		}
		pp = &(*pp)->next;
	}
}

/*
 * ============================================================================
 * Command: ring create <name> <size> - Create a new ring
 * 命令：ring create <name> <size> - 创建新 Ring
 * ============================================================================
 */
struct cmd_ring_create_result {
	cmdline_fixed_string_t ring;
	cmdline_fixed_string_t create;
	cmdline_fixed_string_t name;
	uint32_t size;
};

static void cmd_ring_create_parsed(void *parsed_result,
                                     struct cmdline *cl,
                                     __rte_unused void *data)
{
	struct cmd_ring_create_result *res = parsed_result;
	struct managed_ring *mr;
	struct rte_ring *ring;

	/* Check if ring already exists | 检查 Ring 是否已存在 */
	if (find_ring(res->name)) {
		cmdline_printf(cl, "Error: Ring '%s' already exists\n", res->name);
		return;
	}

	/* Validate size (must be power of 2) | 验证大小（必须是 2 的幂） */
	if ((res->size & (res->size - 1)) != 0) {
		cmdline_printf(cl, "Error: Size must be a power of 2\n");
		return;
	}

	/* Create ring | 创建 Ring */
	ring = rte_ring_create(res->name, res->size, rte_socket_id(),
	                        RING_F_SP_ENQ | RING_F_SC_DEQ);
	if (!ring) {
		cmdline_printf(cl, "Error: Failed to create ring\n");
		return;
	}

	/* Allocate management structure | 分配管理结构 */
	mr = rte_zmalloc(NULL, sizeof(*mr), 0);
	if (!mr) {
		rte_ring_free(ring);
		cmdline_printf(cl, "Error: Failed to allocate memory\n");
		return;
	}

	/* Initialize and add to list | 初始化并添加到列表 */
	mr->ring = ring;
	strncpy(mr->name, res->name, MAX_RING_NAME - 1);
	mr->enq_count = 0;
	mr->deq_count = 0;
	add_ring_to_list(mr);

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
	.help_str = "ring create <name> <size> - Create a new ring (size must be power of 2)",
	.tokens = {
		(void *)&cmd_ring_create_ring,
		(void *)&cmd_ring_create_create,
		(void *)&cmd_ring_create_name,
		(void *)&cmd_ring_create_size,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: ring delete <name> - Delete a ring
 * 命令：ring delete <name> - 删除 Ring
 * ============================================================================
 */
struct cmd_ring_delete_result {
	cmdline_fixed_string_t ring;
	cmdline_fixed_string_t delete;
	cmdline_fixed_string_t name;
};

static void cmd_ring_delete_parsed(void *parsed_result,
                                     struct cmdline *cl,
                                     __rte_unused void *data)
{
	struct cmd_ring_delete_result *res = parsed_result;
	struct managed_ring *mr;

	mr = find_ring(res->name);
	if (!mr) {
		cmdline_printf(cl, "Error: Ring '%s' not found\n", res->name);
		return;
	}

	/* Remove from list and free | 从列表中移除并释放 */
	remove_ring_from_list(mr);
	rte_ring_free(mr->ring);
	rte_free(mr);

	cmdline_printf(cl, "Ring '%s' deleted\n", res->name);
}

cmdline_parse_token_string_t cmd_ring_delete_ring =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_delete_result, ring, "ring");
cmdline_parse_token_string_t cmd_ring_delete_delete =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_delete_result, delete, "delete");
cmdline_parse_token_string_t cmd_ring_delete_name =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_delete_result, name, NULL);

cmdline_parse_inst_t cmd_ring_delete = {
	.f = cmd_ring_delete_parsed,
	.data = NULL,
	.help_str = "ring delete <name> - Delete a ring",
	.tokens = {
		(void *)&cmd_ring_delete_ring,
		(void *)&cmd_ring_delete_delete,
		(void *)&cmd_ring_delete_name,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: ring list - List all rings
 * 命令：ring list - 列出所有 Ring
 * ============================================================================
 */
struct cmd_ring_list_result {
	cmdline_fixed_string_t ring;
	cmdline_fixed_string_t list;
};

static void cmd_ring_list_parsed(__rte_unused void *parsed_result,
                                   struct cmdline *cl,
                                   __rte_unused void *data)
{
	struct managed_ring *mr = g_ring_list;
	int count = 0;

	cmdline_printf(cl, "\n%-20s %-10s %-10s %-10s\n",
	               "Name", "Size", "Used", "Free");
	cmdline_printf(cl, "--------------------------------------------------------\n");

	while (mr) {
		unsigned int used = rte_ring_count(mr->ring);
		unsigned int free = rte_ring_free_count(mr->ring);
		unsigned int size = rte_ring_get_capacity(mr->ring);

		cmdline_printf(cl, "%-20s %-10u %-10u %-10u\n",
		               mr->name, size, used, free);
		count++;
		mr = mr->next;
	}

	if (count == 0) {
		cmdline_printf(cl, "(No rings created)\n");
	}

	cmdline_printf(cl, "\nTotal: %d ring(s)\n\n", count);
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
 * Command: ring enqueue <name> <value> - Enqueue element
 * 命令：ring enqueue <name> <value> - 入队元素
 * ============================================================================
 */
struct cmd_ring_enqueue_result {
	cmdline_fixed_string_t ring;
	cmdline_fixed_string_t enqueue;
	cmdline_fixed_string_t name;
	uint64_t value;
};

static void cmd_ring_enqueue_parsed(void *parsed_result,
                                      struct cmdline *cl,
                                      __rte_unused void *data)
{
	struct cmd_ring_enqueue_result *res = parsed_result;
	struct managed_ring *mr;
	void *obj = (void *)(uintptr_t)res->value;
	int ret;

	mr = find_ring(res->name);
	if (!mr) {
		cmdline_printf(cl, "Error: Ring '%s' not found\n", res->name);
		return;
	}

	ret = rte_ring_enqueue(mr->ring, obj);
	if (ret < 0) {
		cmdline_printf(cl, "Error: Ring is full\n");
		return;
	}

	mr->enq_count++;
	cmdline_printf(cl, "Enqueued value %lu to ring '%s' (total: %lu)\n",
	               res->value, res->name, mr->enq_count);
}

cmdline_parse_token_string_t cmd_ring_enqueue_ring =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_enqueue_result, ring, "ring");
cmdline_parse_token_string_t cmd_ring_enqueue_enqueue =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_enqueue_result, enqueue, "enqueue");
cmdline_parse_token_string_t cmd_ring_enqueue_name =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_enqueue_result, name, NULL);
cmdline_parse_token_num_t cmd_ring_enqueue_value =
	TOKEN_NUM_INITIALIZER(struct cmd_ring_enqueue_result, value, RTE_UINT64);

cmdline_parse_inst_t cmd_ring_enqueue = {
	.f = cmd_ring_enqueue_parsed,
	.data = NULL,
	.help_str = "ring enqueue <name> <value> - Enqueue element to ring",
	.tokens = {
		(void *)&cmd_ring_enqueue_ring,
		(void *)&cmd_ring_enqueue_enqueue,
		(void *)&cmd_ring_enqueue_name,
		(void *)&cmd_ring_enqueue_value,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: ring dequeue <name> - Dequeue element
 * 命令：ring dequeue <name> - 出队元素
 * ============================================================================
 */
struct cmd_ring_dequeue_result {
	cmdline_fixed_string_t ring;
	cmdline_fixed_string_t dequeue;
	cmdline_fixed_string_t name;
};

static void cmd_ring_dequeue_parsed(void *parsed_result,
                                      struct cmdline *cl,
                                      __rte_unused void *data)
{
	struct cmd_ring_dequeue_result *res = parsed_result;
	struct managed_ring *mr;
	void *obj;
	int ret;

	mr = find_ring(res->name);
	if (!mr) {
		cmdline_printf(cl, "Error: Ring '%s' not found\n", res->name);
		return;
	}

	ret = rte_ring_dequeue(mr->ring, &obj);
	if (ret < 0) {
		cmdline_printf(cl, "Error: Ring is empty\n");
		return;
	}

	mr->deq_count++;
	cmdline_printf(cl, "Dequeued value %lu from ring '%s' (total: %lu)\n",
	               (uint64_t)(uintptr_t)obj, res->name, mr->deq_count);
}

cmdline_parse_token_string_t cmd_ring_dequeue_ring =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_dequeue_result, ring, "ring");
cmdline_parse_token_string_t cmd_ring_dequeue_dequeue =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_dequeue_result, dequeue, "dequeue");
cmdline_parse_token_string_t cmd_ring_dequeue_name =
	TOKEN_STRING_INITIALIZER(struct cmd_ring_dequeue_result, name, NULL);

cmdline_parse_inst_t cmd_ring_dequeue = {
	.f = cmd_ring_dequeue_parsed,
	.data = NULL,
	.help_str = "ring dequeue <name> - Dequeue element from ring",
	.tokens = {
		(void *)&cmd_ring_dequeue_ring,
		(void *)&cmd_ring_dequeue_dequeue,
		(void *)&cmd_ring_dequeue_name,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: help - Display available commands
 * ============================================================================
 */
struct cmd_help_result {
	cmdline_fixed_string_t help;
};

static void cmd_help_parsed(__rte_unused void *parsed_result,
                            struct cmdline *cl,
                            __rte_unused void *data)
{
	cmdline_printf(cl, "\nRing Manager Commands:\n");
	cmdline_printf(cl, "======================\n");
	cmdline_printf(cl, "  ring create <name> <size> - Create a new ring\n");
	cmdline_printf(cl, "  ring delete <name>        - Delete a ring\n");
	cmdline_printf(cl, "  ring list                 - List all rings\n");
	cmdline_printf(cl, "  ring enqueue <name> <val> - Enqueue value to ring\n");
	cmdline_printf(cl, "  ring dequeue <name>       - Dequeue value from ring\n");
	cmdline_printf(cl, "  help                      - Display this help\n");
	cmdline_printf(cl, "  quit                      - Exit application\n\n");
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
 * Command: quit - Exit application
 * ============================================================================
 */
struct cmd_quit_result {
	cmdline_fixed_string_t quit;
};

static void cmd_quit_parsed(__rte_unused void *parsed_result,
                             struct cmdline *cl,
                             __rte_unused void *data)
{
	/* Cleanup all rings | 清理所有 Ring */
	struct managed_ring *mr = g_ring_list;
	int count = 0;

	while (mr) {
		struct managed_ring *next = mr->next;
		rte_ring_free(mr->ring);
		rte_free(mr);
		mr = next;
		count++;
	}

	cmdline_printf(cl, "\nCleaned up %d ring(s). Exiting...\n\n", count);
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
 * ============================================================================
 */
cmdline_parse_ctx_t main_ctx[] = {
	(cmdline_parse_inst_t *)&cmd_help,
	(cmdline_parse_inst_t *)&cmd_ring_create,
	(cmdline_parse_inst_t *)&cmd_ring_delete,
	(cmdline_parse_inst_t *)&cmd_ring_list,
	(cmdline_parse_inst_t *)&cmd_ring_enqueue,
	(cmdline_parse_inst_t *)&cmd_ring_dequeue,
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

	printf("\n");
	printf("==============================================\n");
	printf("  DPDK Cmdline Example 3: Ring Manager\n");
	printf("==============================================\n");
	printf("\n");
	printf("This example demonstrates ring management:\n");
	printf("- Create and delete rings dynamically\n");
	printf("- Enqueue and dequeue operations\n");
	printf("- Ring statistics and monitoring\n");
	printf("\nType 'help' to see available commands.\n\n");

	/* Create cmdline instance | 创建 cmdline 实例 */
	cl = cmdline_stdin_new(main_ctx, "ring-mgr> ");
	if (cl == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create cmdline instance\n");

	/* Enter interactive loop | 进入交互循环 */
	cmdline_interact(cl);

	/* Cleanup | 清理资源 */
	cmdline_stdin_exit(cl);
	rte_eal_cleanup();

	return 0;
}
