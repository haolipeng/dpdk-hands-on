/*
 * DPDK Cmdline Example 2: Token Types Showcase
 * DPDK Cmdline 示例 2：Token 类型展示
 *
 * This example demonstrates:
 * 本示例演示：
 * - All standard token types | 所有标准 Token 类型
 * - Token parsing and validation | Token 解析和验证
 * - Different number types | 不同的数字类型
 * - IP address parsing (IPv4/IPv6) | IP 地址解析（IPv4/IPv6）
 * - MAC address parsing | MAC 地址解析
 * - Port list parsing | 端口列表解析
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>

#include <rte_eal.h>
#include <rte_ether.h>
#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_parse_string.h>
#include <cmdline_parse_num.h>
#include <cmdline_parse_ipaddr.h>
#include <cmdline_parse_etheraddr.h>
#include <cmdline_parse_portlist.h>
#include <cmdline_socket.h>
#include <cmdline.h>

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
	cmdline_printf(cl, "Token Type Examples:\n");
	cmdline_printf(cl, "  ipaddr <addr>       - Parse IPv4/IPv6 address\n");
	cmdline_printf(cl, "  macaddr <mac>       - Parse MAC address\n");
	cmdline_printf(cl, "  portlist <list>     - Parse port list (e.g., 0-3,5,7)\n");
	cmdline_printf(cl, "  number8 <uint8>     - Parse 8-bit unsigned number (0-255)\n");
	cmdline_printf(cl, "  number16 <uint16>   - Parse 16-bit unsigned number\n");
	cmdline_printf(cl, "  number32 <uint32>   - Parse 32-bit unsigned number\n");
	cmdline_printf(cl, "  number64 <uint64>   - Parse 64-bit unsigned number\n");
	cmdline_printf(cl, "  range <min> <max>   - Two numbers with validation\n");
	cmdline_printf(cl, "  choice <opt>        - Fixed choices (add/del/show)\n");
	cmdline_printf(cl, "  string <text>       - Any string\n");
	cmdline_printf(cl, "\nUtility:\n");
	cmdline_printf(cl, "  help                - Display this help\n");
	cmdline_printf(cl, "  quit                - Exit application\n\n");
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
 * Command: ipaddr <addr> - Parse IP address
 * 命令：ipaddr <addr> - 解析 IP 地址
 * ============================================================================
 */
struct cmd_ipaddr_result {
	cmdline_fixed_string_t ipaddr;
	cmdline_ipaddr_t addr;
};

static void cmd_ipaddr_parsed(void *parsed_result,
                               struct cmdline *cl,
                               __rte_unused void *data)
{
	struct cmd_ipaddr_result *res = parsed_result;
	char ip_str[INET6_ADDRSTRLEN];

	cmdline_printf(cl, "\n=== IP Address Token ===\n");

	if (res->addr.family == AF_INET) {
		/* IPv4 address | IPv4 地址 */
		inet_ntop(AF_INET, &res->addr.addr.ipv4, ip_str, sizeof(ip_str));
		cmdline_printf(cl, "Type:    IPv4\n");
		cmdline_printf(cl, "Address: %s\n", ip_str);
		cmdline_printf(cl, "Hex:     0x%08x\n", ntohl(res->addr.addr.ipv4.s_addr));
	} else if (res->addr.family == AF_INET6) {
		/* IPv6 address | IPv6 地址 */
		inet_ntop(AF_INET6, &res->addr.addr.ipv6, ip_str, sizeof(ip_str));
		cmdline_printf(cl, "Type:    IPv6\n");
		cmdline_printf(cl, "Address: %s\n", ip_str);
	}

	cmdline_printf(cl, "\n");
}

cmdline_parse_token_string_t cmd_ipaddr_ipaddr =
	TOKEN_STRING_INITIALIZER(struct cmd_ipaddr_result, ipaddr, "ipaddr");
cmdline_parse_token_ipaddr_t cmd_ipaddr_addr =
	TOKEN_IPADDR_INITIALIZER(struct cmd_ipaddr_result, addr);

cmdline_parse_inst_t cmd_ipaddr = {
	.f = cmd_ipaddr_parsed,
	.data = NULL,
	.help_str = "ipaddr <addr> - Parse IPv4 or IPv6 address",
	.tokens = {
		(void *)&cmd_ipaddr_ipaddr,
		(void *)&cmd_ipaddr_addr,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: macaddr <mac> - Parse MAC address
 * 命令：macaddr <mac> - 解析 MAC 地址
 * ============================================================================
 */
struct cmd_macaddr_result {
	cmdline_fixed_string_t macaddr;
	struct rte_ether_addr addr;
};

static void cmd_macaddr_parsed(void *parsed_result,
                                struct cmdline *cl,
                                __rte_unused void *data)
{
	struct cmd_macaddr_result *res = parsed_result;
	char mac_str[RTE_ETHER_ADDR_FMT_SIZE];

	rte_ether_format_addr(mac_str, sizeof(mac_str), &res->addr);

	cmdline_printf(cl, "\n=== MAC Address Token ===\n");
	cmdline_printf(cl, "Address: %s\n", mac_str);
	cmdline_printf(cl, "Bytes:   %02x:%02x:%02x:%02x:%02x:%02x\n",
	               res->addr.addr_bytes[0], res->addr.addr_bytes[1],
	               res->addr.addr_bytes[2], res->addr.addr_bytes[3],
	               res->addr.addr_bytes[4], res->addr.addr_bytes[5]);
	cmdline_printf(cl, "Multicast: %s\n",
	               rte_is_multicast_ether_addr(&res->addr) ? "Yes" : "No");
	cmdline_printf(cl, "Broadcast: %s\n",
	               rte_is_broadcast_ether_addr(&res->addr) ? "Yes" : "No");
	cmdline_printf(cl, "\n");
}

cmdline_parse_token_string_t cmd_macaddr_macaddr =
	TOKEN_STRING_INITIALIZER(struct cmd_macaddr_result, macaddr, "macaddr");
cmdline_parse_token_etheraddr_t cmd_macaddr_addr =
	TOKEN_ETHERADDR_INITIALIZER(struct cmd_macaddr_result, addr);

cmdline_parse_inst_t cmd_macaddr = {
	.f = cmd_macaddr_parsed,
	.data = NULL,
	.help_str = "macaddr <mac> - Parse MAC address (format: XX:XX:XX:XX:XX:XX)",
	.tokens = {
		(void *)&cmd_macaddr_macaddr,
		(void *)&cmd_macaddr_addr,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: portlist <list> - Parse port list
 * 命令：portlist <list> - 解析端口列表
 * ============================================================================
 */
struct cmd_portlist_result {
	cmdline_fixed_string_t portlist;
	cmdline_portlist_t ports;
};

static void cmd_portlist_parsed(void *parsed_result,
                                 struct cmdline *cl,
                                 __rte_unused void *data)
{
	struct cmd_portlist_result *res = parsed_result;
	int port;
	int count = 0;

	cmdline_printf(cl, "\n=== Port List Token ===\n");
	cmdline_printf(cl, "Port list bitmap: 0x%08x\n", res->ports.map);
	cmdline_printf(cl, "Ports enabled: ");

	for (port = 0; port < 32; port++) {
		if (res->ports.map & (1 << port)) {
			cmdline_printf(cl, "%d ", port);
			count++;
		}
	}

	cmdline_printf(cl, "\nTotal ports: %d\n\n", count);
}

cmdline_parse_token_string_t cmd_portlist_portlist =
	TOKEN_STRING_INITIALIZER(struct cmd_portlist_result, portlist, "portlist");
cmdline_parse_token_portlist_t cmd_portlist_ports =
	TOKEN_PORTLIST_INITIALIZER(struct cmd_portlist_result, ports);

cmdline_parse_inst_t cmd_portlist = {
	.f = cmd_portlist_parsed,
	.data = NULL,
	.help_str = "portlist <list> - Parse port list (e.g., 0-3,5,7-9)",
	.tokens = {
		(void *)&cmd_portlist_portlist,
		(void *)&cmd_portlist_ports,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: number8/16/32/64 - Parse different number types
 * 命令：number8/16/32/64 - 解析不同的数字类型
 * ============================================================================
 */

/* UINT8 */
struct cmd_number8_result {
	cmdline_fixed_string_t number8;
	uint8_t value;
};

static void cmd_number8_parsed(void *parsed_result,
                                struct cmdline *cl,
                                __rte_unused void *data)
{
	struct cmd_number8_result *res = parsed_result;

	cmdline_printf(cl, "\n=== UINT8 Number Token ===\n");
	cmdline_printf(cl, "Value (dec): %u\n", res->value);
	cmdline_printf(cl, "Value (hex): 0x%02x\n", res->value);
	cmdline_printf(cl, "Range: 0 - 255\n\n");
}

cmdline_parse_token_string_t cmd_number8_number8 =
	TOKEN_STRING_INITIALIZER(struct cmd_number8_result, number8, "number8");
cmdline_parse_token_num_t cmd_number8_value =
	TOKEN_NUM_INITIALIZER(struct cmd_number8_result, value, RTE_UINT8);

cmdline_parse_inst_t cmd_number8 = {
	.f = cmd_number8_parsed,
	.data = NULL,
	.help_str = "number8 <uint8> - Parse 8-bit unsigned number (0-255)",
	.tokens = {
		(void *)&cmd_number8_number8,
		(void *)&cmd_number8_value,
		NULL,
	},
};

/* UINT16 */
struct cmd_number16_result {
	cmdline_fixed_string_t number16;
	uint16_t value;
};

static void cmd_number16_parsed(void *parsed_result,
                                 struct cmdline *cl,
                                 __rte_unused void *data)
{
	struct cmd_number16_result *res = parsed_result;

	cmdline_printf(cl, "\n=== UINT16 Number Token ===\n");
	cmdline_printf(cl, "Value (dec): %u\n", res->value);
	cmdline_printf(cl, "Value (hex): 0x%04x\n", res->value);
	cmdline_printf(cl, "Range: 0 - 65535\n\n");
}

cmdline_parse_token_string_t cmd_number16_number16 =
	TOKEN_STRING_INITIALIZER(struct cmd_number16_result, number16, "number16");
cmdline_parse_token_num_t cmd_number16_value =
	TOKEN_NUM_INITIALIZER(struct cmd_number16_result, value, RTE_UINT16);

cmdline_parse_inst_t cmd_number16 = {
	.f = cmd_number16_parsed,
	.data = NULL,
	.help_str = "number16 <uint16> - Parse 16-bit unsigned number",
	.tokens = {
		(void *)&cmd_number16_number16,
		(void *)&cmd_number16_value,
		NULL,
	},
};

/* UINT32 */
struct cmd_number32_result {
	cmdline_fixed_string_t number32;
	uint32_t value;
};

static void cmd_number32_parsed(void *parsed_result,
                                 struct cmdline *cl,
                                 __rte_unused void *data)
{
	struct cmd_number32_result *res = parsed_result;

	cmdline_printf(cl, "\n=== UINT32 Number Token ===\n");
	cmdline_printf(cl, "Value (dec): %u\n", res->value);
	cmdline_printf(cl, "Value (hex): 0x%08x\n", res->value);
	cmdline_printf(cl, "Supports: decimal, hex (0x), octal (0)\n\n");
}

cmdline_parse_token_string_t cmd_number32_number32 =
	TOKEN_STRING_INITIALIZER(struct cmd_number32_result, number32, "number32");
cmdline_parse_token_num_t cmd_number32_value =
	TOKEN_NUM_INITIALIZER(struct cmd_number32_result, value, RTE_UINT32);

cmdline_parse_inst_t cmd_number32 = {
	.f = cmd_number32_parsed,
	.data = NULL,
	.help_str = "number32 <uint32> - Parse 32-bit unsigned number",
	.tokens = {
		(void *)&cmd_number32_number32,
		(void *)&cmd_number32_value,
		NULL,
	},
};

/* UINT64 */
struct cmd_number64_result {
	cmdline_fixed_string_t number64;
	uint64_t value;
};

static void cmd_number64_parsed(void *parsed_result,
                                 struct cmdline *cl,
                                 __rte_unused void *data)
{
	struct cmd_number64_result *res = parsed_result;

	cmdline_printf(cl, "\n=== UINT64 Number Token ===\n");
	cmdline_printf(cl, "Value (dec): %lu\n", res->value);
	cmdline_printf(cl, "Value (hex): 0x%016lx\n", res->value);
	cmdline_printf(cl, "Supports large numbers up to 2^64-1\n\n");
}

cmdline_parse_token_string_t cmd_number64_number64 =
	TOKEN_STRING_INITIALIZER(struct cmd_number64_result, number64, "number64");
cmdline_parse_token_num_t cmd_number64_value =
	TOKEN_NUM_INITIALIZER(struct cmd_number64_result, value, RTE_UINT64);

cmdline_parse_inst_t cmd_number64 = {
	.f = cmd_number64_parsed,
	.data = NULL,
	.help_str = "number64 <uint64> - Parse 64-bit unsigned number",
	.tokens = {
		(void *)&cmd_number64_number64,
		(void *)&cmd_number64_value,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: range <min> <max> - Parse range with validation
 * 命令：range <min> <max> - 解析范围并验证
 * ============================================================================
 */
struct cmd_range_result {
	cmdline_fixed_string_t range;
	uint32_t min;
	uint32_t max;
};

static void cmd_range_parsed(void *parsed_result,
                              struct cmdline *cl,
                              __rte_unused void *data)
{
	struct cmd_range_result *res = parsed_result;

	cmdline_printf(cl, "\n=== Range Validation ===\n");
	cmdline_printf(cl, "Min value: %u\n", res->min);
	cmdline_printf(cl, "Max value: %u\n", res->max);

	if (res->min > res->max) {
		cmdline_printf(cl, "Warning: min > max (invalid range!)\n");
	} else {
		cmdline_printf(cl, "Range size: %u\n", res->max - res->min + 1);
	}

	cmdline_printf(cl, "\n");
}

cmdline_parse_token_string_t cmd_range_range =
	TOKEN_STRING_INITIALIZER(struct cmd_range_result, range, "range");
cmdline_parse_token_num_t cmd_range_min =
	TOKEN_NUM_INITIALIZER(struct cmd_range_result, min, RTE_UINT32);
cmdline_parse_token_num_t cmd_range_max =
	TOKEN_NUM_INITIALIZER(struct cmd_range_result, max, RTE_UINT32);

cmdline_parse_inst_t cmd_range = {
	.f = cmd_range_parsed,
	.data = NULL,
	.help_str = "range <min> <max> - Parse two numbers",
	.tokens = {
		(void *)&cmd_range_range,
		(void *)&cmd_range_min,
		(void *)&cmd_range_max,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: choice <option> - Parse fixed choices
 * 命令：choice <option> - 解析固定选项
 * ============================================================================
 */
struct cmd_choice_result {
	cmdline_fixed_string_t choice;
	cmdline_fixed_string_t option;
};

static void cmd_choice_parsed(void *parsed_result,
                               struct cmdline *cl,
                               __rte_unused void *data)
{
	struct cmd_choice_result *res = parsed_result;

	cmdline_printf(cl, "\n=== Fixed Choice Token ===\n");
	cmdline_printf(cl, "You selected: %s\n", res->option);

	if (strcmp(res->option, "add") == 0) {
		cmdline_printf(cl, "Action: Add a new item\n");
	} else if (strcmp(res->option, "del") == 0) {
		cmdline_printf(cl, "Action: Delete an item\n");
	} else if (strcmp(res->option, "show") == 0) {
		cmdline_printf(cl, "Action: Show items\n");
	}

	cmdline_printf(cl, "\n");
}

cmdline_parse_token_string_t cmd_choice_choice =
	TOKEN_STRING_INITIALIZER(struct cmd_choice_result, choice, "choice");
cmdline_parse_token_string_t cmd_choice_option =
	TOKEN_STRING_INITIALIZER(struct cmd_choice_result, option, "add#del#show");

cmdline_parse_inst_t cmd_choice = {
	.f = cmd_choice_parsed,
	.data = NULL,
	.help_str = "choice <add|del|show> - Select from fixed options",
	.tokens = {
		(void *)&cmd_choice_choice,
		(void *)&cmd_choice_option,
		NULL,
	},
};

/*
 * ============================================================================
 * Command: string <text> - Parse any string
 * 命令：string <text> - 解析任意字符串
 * ============================================================================
 */
struct cmd_string_result {
	cmdline_fixed_string_t string_cmd;
	cmdline_fixed_string_t text;
};

static void cmd_string_parsed(void *parsed_result,
                               struct cmdline *cl,
                               __rte_unused void *data)
{
	struct cmd_string_result *res = parsed_result;

	cmdline_printf(cl, "\n=== String Token ===\n");
	cmdline_printf(cl, "String: \"%s\"\n", res->text);
	cmdline_printf(cl, "Length: %lu characters\n", strlen(res->text));
	cmdline_printf(cl, "\n");
}

cmdline_parse_token_string_t cmd_string_string_cmd =
	TOKEN_STRING_INITIALIZER(struct cmd_string_result, string_cmd, "string");
cmdline_parse_token_string_t cmd_string_text =
	TOKEN_STRING_INITIALIZER(struct cmd_string_result, text, NULL);

cmdline_parse_inst_t cmd_string = {
	.f = cmd_string_parsed,
	.data = NULL,
	.help_str = "string <text> - Parse any string",
	.tokens = {
		(void *)&cmd_string_string_cmd,
		(void *)&cmd_string_text,
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
	cmdline_printf(cl, "\nExiting...\n\n");
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
	(cmdline_parse_inst_t *)&cmd_ipaddr,
	(cmdline_parse_inst_t *)&cmd_macaddr,
	(cmdline_parse_inst_t *)&cmd_portlist,
	(cmdline_parse_inst_t *)&cmd_number8,
	(cmdline_parse_inst_t *)&cmd_number16,
	(cmdline_parse_inst_t *)&cmd_number32,
	(cmdline_parse_inst_t *)&cmd_number64,
	(cmdline_parse_inst_t *)&cmd_range,
	(cmdline_parse_inst_t *)&cmd_choice,
	(cmdline_parse_inst_t *)&cmd_string,
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

	printf("\n");
	printf("==============================================\n");
	printf("  DPDK Cmdline Example 2: Token Types\n");
	printf("==============================================\n");
	printf("\n");
	printf("This example demonstrates all token types:\n");
	printf("- IP addresses (IPv4/IPv6)\n");
	printf("- MAC addresses\n");
	printf("- Port lists\n");
	printf("- Numbers (8/16/32/64 bit)\n");
	printf("- Fixed choices\n");
	printf("- Strings\n");
	printf("\nType 'help' to see available commands.\n\n");

	/* Create cmdline instance | 创建 cmdline 实例 */
	cl = cmdline_stdin_new(main_ctx, "dpdk-tokens> ");
	if (cl == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create cmdline instance\n");

	/* Enter interactive loop | 进入交互循环 */
	cmdline_interact(cl);

	/* Cleanup | 清理资源 */
	cmdline_stdin_exit(cl);
	rte_eal_cleanup();

	return 0;
}
