/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK ACL Demo: IPv4 Firewall Example (简化版)
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include <rte_eal.h>
#include <rte_log.h>
#include <rte_debug.h>
#include <rte_acl.h>
#include <rte_ip.h>

/* 常量定义 */
#define ACL_DENY 0
#define ACL_ALLOW 1
#define MAX_ACL_RULES 10    /*ACL规则数最大值 */
#define NUM_TEST_PACKETS 3  /* 简化为3个测试包 */

/* 字段索引枚举 (field_index) */
enum {
	PROTO_FIELD_IPV4,   /* 协议字段 */
	SRC_FIELD_IPV4,     /* 源IP字段 */
	DST_FIELD_IPV4,     /* 目的IP字段 */
	SRCP_FIELD_IPV4,    /* 源端口字段 */
	DSTP_FIELD_IPV4,     /* 目的端口字段 */
	NUM_FIELDS_IPV4
};

/**
 * input_index 标准布局 (遵循 DPDK IPv4+VLAN 标准)
 * 这些宏定义源自 DPDK 测试代码 (app/test/test_acl.h)
 * 即使不使用 VLAN，也应遵循此标准布局以保持兼容性
 */
enum {
	RTE_ACL_IPV4VLAN_PROTO = 0,  /* 协议字段 */
	RTE_ACL_IPV4VLAN_VLAN = 1,   /* VLAN字段 (本示例不使用) */
	RTE_ACL_IPV4VLAN_SRC = 2,    /* 源IP字段 */
	RTE_ACL_IPV4VLAN_DST = 3,    /* 目的IP字段 */
	RTE_ACL_IPV4VLAN_PORTS = 4,  /* 端口字段 (源端口和目的端口共享) */
	RTE_ACL_IPV4VLAN_NUM = 5
};

/* IPv4五元组结构体 */
struct ipv4_5tuple {
	uint8_t  proto;      /* 协议 */
	uint32_t ip_src;     /* 源IP */
	uint32_t ip_dst;     /* 目的IP */
	uint16_t port_src;   /* 源端口 */
	uint16_t port_dst;   /* 目的端口 */
} __rte_packed;

/* ACL规则结构体 */
RTE_ACL_RULE_DEF(acl_ipv4_rule, NUM_FIELDS_IPV4);

/* 全局ACL上下文 */
static struct rte_acl_ctx *acl_ctx = NULL;

static void
setup_acl_config(struct rte_acl_config *cfg)
{
	static struct rte_acl_field_def ipv4_defs[NUM_FIELDS_IPV4] = {
		/* 协议字段: 1字节, BITMASK类型 */
		{
			.type = RTE_ACL_FIELD_TYPE_BITMASK,
			.size = sizeof(uint8_t),
			.field_index = PROTO_FIELD_IPV4,
			.input_index = RTE_ACL_IPV4VLAN_PROTO,
			.offset = offsetof(struct ipv4_5tuple, proto),
		},
		/* 源IP字段: 4字节, MASK类型(支持CIDR网段) */
		{
			.type = RTE_ACL_FIELD_TYPE_MASK,
			.size = sizeof(uint32_t),
			.field_index = SRC_FIELD_IPV4,
			.input_index = RTE_ACL_IPV4VLAN_SRC,
			.offset = offsetof(struct ipv4_5tuple, ip_src),
		},
		/* 目的IP字段: 4字节, MASK类型 */
		{
			.type = RTE_ACL_FIELD_TYPE_MASK,
			.size = sizeof(uint32_t),
			.field_index = DST_FIELD_IPV4,
			.input_index = RTE_ACL_IPV4VLAN_DST,
			.offset = offsetof(struct ipv4_5tuple, ip_dst),
		},
		/* 源端口字段: 2字节, RANGE类型 */
		{
			.type = RTE_ACL_FIELD_TYPE_RANGE,
			.size = sizeof(uint16_t),
			.field_index = SRCP_FIELD_IPV4,
			.input_index = RTE_ACL_IPV4VLAN_PORTS,
			.offset = offsetof(struct ipv4_5tuple, port_src),
		},
		/* 目的端口字段: 2字节, RANGE类型, 与源端口共享input_index */
		{
			.type = RTE_ACL_FIELD_TYPE_RANGE,
			.size = sizeof(uint16_t),
			.field_index = DSTP_FIELD_IPV4,
			.input_index = RTE_ACL_IPV4VLAN_PORTS,
			.offset = offsetof(struct ipv4_5tuple, port_dst),
		},
	};

	memset(cfg, 0, sizeof(*cfg));
	cfg->num_categories = 1;
	cfg->num_fields = NUM_FIELDS_IPV4;
	memcpy(cfg->defs, ipv4_defs, sizeof(ipv4_defs));
}

/**
 * 构造ACL规则
 * 参数说明:
 * rule: 规则结构体指针 priority: 优先级 userdata: 用户数据
 * proto: 协议 proto_mask: 协议掩码
 * src_ip: 源IP src_mask_len: 源IP掩码长度
 * dst_ip: 目的IP dst_mask_len: 目的IP掩码长度
 * src_port_low: 源端口低 src_port_high: 源端口高
 * dst_port_low: 目的端口低 dst_port_high: 目的端口高
 */
static void
make_rule(struct acl_ipv4_rule *rule, uint32_t priority, uint32_t userdata,
          uint8_t proto, uint8_t proto_mask,
          uint32_t src_ip, uint32_t src_mask_len,
          uint32_t dst_ip, uint32_t dst_mask_len,
          uint16_t src_port_low, uint16_t src_port_high,
          uint16_t dst_port_low, uint16_t dst_port_high)
{
	memset(rule, 0, sizeof(*rule));

	rule->data.category_mask = 1;
	rule->data.priority = priority;     /* 优先级: 数字越大越先匹配 */
	rule->data.userdata = userdata;     /* 用户数据: 可存储规则ID或动作 */

	/* 字段0: 协议 */
	rule->field[PROTO_FIELD_IPV4].value.u8 = proto;//proto协议为uint8_t类型
	rule->field[PROTO_FIELD_IPV4].mask_range.u8 = proto_mask;

	/* 字段1: 源IP (主机字节序) */
	rule->field[SRC_FIELD_IPV4].value.u32 = src_ip;//ip地址为uint32_t类型
	rule->field[SRC_FIELD_IPV4].mask_range.u32 = src_mask_len;  /* CIDR位数 */

	/* 字段2: 目的IP */
	rule->field[DST_FIELD_IPV4].value.u32 = dst_ip;
	rule->field[DST_FIELD_IPV4].mask_range.u32 = dst_mask_len;

	/* 字段3: 源端口范围 */
	rule->field[SRCP_FIELD_IPV4].value.u16 = src_port_low;//端口为uint16_t类型
	rule->field[SRCP_FIELD_IPV4].mask_range.u16 = src_port_high;

	/* 字段4: 目的端口范围 */
	rule->field[DSTP_FIELD_IPV4].value.u16 = dst_port_low;
	rule->field[DSTP_FIELD_IPV4].mask_range.u16 = dst_port_high;
}

/**
 * 添加ACL规则 (简化版: 2条规则)
 *
 * 规则设计:
 * 1. 允许HTTP流量 (TCP端口80)        [优先级100]
 * 2. 默认拒绝所有其他流量            [优先级10]
 */
static void
add_acl_rules(struct rte_acl_ctx *ctx)
{
	struct acl_ipv4_rule rules[2];
	int ret;

	printf("[步骤2] 添加防火墙规则...\n");

	/* 规则1: 允许HTTP (端口80) */
	uint32_t ruleID = 1;
	make_rule(&rules[0], 100, ruleID,              /* priority=100, userdata=规则ID 1 */
	          IPPROTO_TCP, 0xFF,              /* TCP协议 */
	          0, 0,                           /* 源IP: 任意 */
	          0, 0,                           /* 目的IP: 任意 */
	          0, 65535,                       /* 源端口: 任意 */
	          80, 80);                        /* 目的端口: 80 */
	printf("  规则1: 允许 HTTP (端口80)           [优先级 100]\n");

	/* 规则2: 默认拒绝所有 */
	ruleID = 2;
	make_rule(&rules[1], 10, ruleID,               /* priority=10, userdata=规则ID 2 */
	          0, 0,                           /* 任意协议 */
	          0, 0,
	          0, 0,
	          0, 65535,
	          0, 65535);                      /* 任意端口 */
	printf("  规则2: 拒绝 所有其他流量 (默认拒绝)  [优先级 10]\n");

	/* 添加规则到ACL上下文 */
	ret = rte_acl_add_rules(ctx, (struct rte_acl_rule *)rules, 2);
	if (ret != 0) {
		rte_exit(EXIT_FAILURE, "  错误: 添加规则失败: %s\n", strerror(-ret));
	}
	printf("  ✓ 成功添加 2 条规则\n\n");
}

/**
 * 创建测试数据包 (简化版: 3个数据包)
 *
 * 字节序注意事项:
 * - 规则定义使用主机字节序 (方便定义和读取)
 * - 数据包使用网络字节序 (真实网络数据的格式)
 * - ACL引擎内部会自动处理字节序转换
 */
static void
create_test_packets(struct ipv4_5tuple *packets)
{
	/* 数据包1: HTTP请求 -> 应匹配规则1 (允许) */
	packets[0].proto = IPPROTO_TCP;
	packets[0].ip_src = htonl(RTE_IPV4(192, 168, 1, 10));   /* 网络字节序 */
	packets[0].ip_dst = htonl(RTE_IPV4(192, 168, 1, 100));
	packets[0].port_src = htons(12345);
	packets[0].port_dst = htons(80);

	/* 数据包2: 其他端口 -> 应匹配规则3 (拒绝) */
	packets[2].proto = IPPROTO_TCP;
	packets[2].ip_src = htonl(RTE_IPV4(1, 2, 3, 4));
	packets[2].ip_dst = htonl(RTE_IPV4(5, 6, 7, 8));
	packets[2].port_src = htons(9999);
	packets[2].port_dst = htons(8080);
}

/**
 * 分类数据包并打印结果 (简化版)
 *
 * 使用userdata直接存储规则ID, 简化输出逻辑
 */
static void
classify_and_print(struct rte_acl_ctx *ctx, struct ipv4_5tuple *packets, int num_packets)
{
	const uint8_t *data[NUM_TEST_PACKETS];
	uint32_t results[NUM_TEST_PACKETS] = {0};
	int i, ret;

	printf("[步骤4] 分类测试数据包...\n");

	/* 准备数据指针数组 */
	for (i = 0; i < num_packets; i++) {
		data[i] = (const uint8_t *)&packets[i];
	}

	/* 批量分类 */
	ret = rte_acl_classify(ctx, data, results, num_packets, 1);
	if (ret != 0) {
		rte_exit(EXIT_FAILURE, "  错误: 分类失败: %s\n", strerror(-ret));
	}

	/* 打印分类结果 */
	for (i = 0; i < num_packets; i++) {
		char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
		const char *proto_str, *action_str;

		/* 转换IP地址为可读格式 */
		inet_ntop(AF_INET, &packets[i].ip_src, src_ip, sizeof(src_ip));
		inet_ntop(AF_INET, &packets[i].ip_dst, dst_ip, sizeof(dst_ip));

		proto_str = (packets[i].proto == IPPROTO_TCP) ? "TCP" : "UDP";

		/* 根据规则ID确定动作 (规则1允许, 规则2和3拒绝) */
		if (results[i] == 1) {
			action_str = "允许";
		} else {
			action_str = "拒绝";
		}

		printf("  包%d: %s:%-5d -> %s:%-5d (%s)  => %s (规则%u)\n",i + 1,src_ip, ntohs(packets[i].port_src), dst_ip, ntohs(packets[i].port_dst),
		       proto_str, action_str, results[i]);  /* results存储的就是userdata (规则ID) */
	}
	printf("\n");
}

/**
 * 主函数
 */
int
main(int argc, char **argv)
{
	int ret;
	struct ipv4_5tuple test_packets[NUM_TEST_PACKETS];

	/* 初始化EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		rte_panic("无法初始化EAL: %s\n", rte_strerror(rte_errno));
	}

	printf("\n=== DPDK ACL 演示: IPv4防火墙 (简化版) ===\n\n");

	/* 步骤1: 创建ACL上下文 */
	//初始化acl参数内容
	struct rte_acl_param acl_param = {
		.name = "ipv4_acl",
		.socket_id = rte_socket_id(),
		.rule_size = RTE_ACL_RULE_SZ(NUM_FIELDS_IPV4),
		.max_rule_num = MAX_ACL_RULES,
	};

	printf("[步骤1] 创建ACL上下文...\n");
	acl_ctx = rte_acl_create(&acl_param);
	if (acl_ctx == NULL) {
		rte_exit(EXIT_FAILURE, "  错误: 无法创建ACL上下文\n");
	}
	printf("  ✓ 成功创建ACL上下文: %s\n\n", acl_param.name);

	/* 步骤2: 添加规则 */
	add_acl_rules(acl_ctx);

	/* 步骤3: 构建ACL */
	printf("[步骤3] 构建ACL...\n");
	struct rte_acl_config cfg;
	setup_acl_config(&cfg);
	ret = rte_acl_build(acl_ctx, &cfg);
	if (ret != 0) {
		rte_exit(EXIT_FAILURE, "  错误: 构建ACL失败: %s\n", strerror(-ret));
	}
	printf("  ✓ ACL构建成功\n\n");

	/* 步骤4: 创建测试数据包 */
	create_test_packets(test_packets);

	/* 步骤5: 分类并打印结果 */
	classify_and_print(acl_ctx, test_packets, NUM_TEST_PACKETS);

	/* 步骤6: 清理资源 */
	printf("[清理]\n");
	if (acl_ctx != NULL) {
		rte_acl_free(acl_ctx);
		printf("  ✓ ACL上下文已释放\n");
	}

	rte_eal_cleanup();
	printf("  ✓ EAL已清理\n\n");

	printf("=== 演示结束 ===\n\n");
	return 0;
}
