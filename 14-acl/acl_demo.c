/* SPDX-License-Identifier: BSD-3-Clause
 * DPDK ACL Demo: IPv4 Firewall Example
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

/* 常量定义 / Constants */
#define ACL_DENY 0
#define ACL_ALLOW 1
#define NUM_FIELDS_IPV4 5
#define MAX_ACL_RULES 10
#define NUM_TEST_PACKETS 5

/* IPv4协议号已在 netinet/in.h 中定义 / IPv4 Protocol Numbers defined in netinet/in.h */
/* IPPROTO_TCP = 6, IPPROTO_UDP = 17 */

/* 字段索引枚举 / Field index enumeration */
enum {
	PROTO_FIELD_IPV4,   // 协议字段 / Protocol field
	SRC_FIELD_IPV4,     // 源IP字段 / Source IP field
	DST_FIELD_IPV4,     // 目的IP字段 / Destination IP field
	SRCP_FIELD_IPV4,    // 源端口字段 / Source port field
	DSTP_FIELD_IPV4     // 目的端口字段 / Destination port field
};

/* IPv4五元组结构体 / IPv4 5-tuple structure */
struct ipv4_5tuple {
	uint8_t  proto;      // 协议 / Protocol
	uint32_t ip_src;     // 源IP / Source IP
	uint32_t ip_dst;     // 目的IP / Destination IP
	uint16_t port_src;   // 源端口 / Source port
	uint16_t port_dst;   // 目的端口 / Destination port
} __rte_packed;

/* ACL规则结构体 / ACL rule structure */
RTE_ACL_RULE_DEF(acl_ipv4_rule, NUM_FIELDS_IPV4);

/* 全局ACL上下文 / Global ACL context */
static struct rte_acl_ctx *acl_ctx = NULL;

/* 统计信息 / Statistics */
static struct {
	uint32_t total_packets;
	uint32_t allowed;
	uint32_t denied;
} stats = {0};

/**
 * 配置ACL字段定义 / Setup ACL field definitions
 *
 * 这个函数配置了IPv4 5元组的字段定义，包括：
 * - 字段类型（MASK/RANGE/BITMASK）
 * - 字段大小（1/2/4字节）
 * - 字段在结构体中的偏移量
 * - input_index用于4字节对齐分组
 */
static void
setup_acl_config(struct rte_acl_config *cfg)
{
	static struct rte_acl_field_def ipv4_defs[NUM_FIELDS_IPV4] = {
		/* 协议字段：1字节，BITMASK类型 / Protocol field: 1 byte, BITMASK type */
		{
			.type = RTE_ACL_FIELD_TYPE_BITMASK,
			.size = sizeof(uint8_t),
			.field_index = PROTO_FIELD_IPV4,
			.input_index = 0,
			.offset = offsetof(struct ipv4_5tuple, proto),
		},
		/* 源IP字段：4字节，MASK类型（支持CIDR） / Source IP: 4 bytes, MASK type (CIDR support) */
		{
			.type = RTE_ACL_FIELD_TYPE_MASK,
			.size = sizeof(uint32_t),
			.field_index = SRC_FIELD_IPV4,
			.input_index = 1,
			.offset = offsetof(struct ipv4_5tuple, ip_src),
		},
		/* 目的IP字段：4字节，MASK类型 / Destination IP: 4 bytes, MASK type */
		{
			.type = RTE_ACL_FIELD_TYPE_MASK,
			.size = sizeof(uint32_t),
			.field_index = DST_FIELD_IPV4,
			.input_index = 2,
			.offset = offsetof(struct ipv4_5tuple, ip_dst),
		},
		/* 源端口字段：2字节，RANGE类型 / Source port: 2 bytes, RANGE type */
		{
			.type = RTE_ACL_FIELD_TYPE_RANGE,
			.size = sizeof(uint16_t),
			.field_index = SRCP_FIELD_IPV4,
			.input_index = 3,
			.offset = offsetof(struct ipv4_5tuple, port_src),
		},
		/* 目的端口字段：2字节，RANGE类型（与源端口共享input_index） / Dest port: 2 bytes, RANGE type (shared input_index) */
		{
			.type = RTE_ACL_FIELD_TYPE_RANGE,
			.size = sizeof(uint16_t),
			.field_index = DSTP_FIELD_IPV4,
			.input_index = 3,  // 与源端口共享同一input_index（4字节对齐）/ Shared with src port (4-byte alignment)
			.offset = offsetof(struct ipv4_5tuple, port_dst),
		},
	};

	memset(cfg, 0, sizeof(*cfg));
	cfg->num_categories = 1;  // 使用单一分类器 / Single classifier
	cfg->num_fields = NUM_FIELDS_IPV4;
	memcpy(cfg->defs, ipv4_defs, sizeof(ipv4_defs));
}

/**
 * 创建ACL上下文 / Create ACL context
 */
static struct rte_acl_ctx *
create_acl_context(void)
{
	struct rte_acl_param acl_param = {
		.name = "ipv4_acl",
		.socket_id = rte_socket_id(),
		.rule_size = RTE_ACL_RULE_SZ(NUM_FIELDS_IPV4),
		.max_rule_num = MAX_ACL_RULES,
	};

	printf("[步骤 1] 创建ACL上下文...\n");
	acl_ctx = rte_acl_create(&acl_param);
	if (acl_ctx == NULL) {
		rte_exit(EXIT_FAILURE, "  错误：无法创建ACL上下文\n");
	}
	printf("  ✓ 成功创建ACL上下文: %s\n\n", acl_param.name);

	return acl_ctx;
}

/**
 * 辅助函数：构造ACL规则 / Helper: construct ACL rule
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

	/* 设置规则元数据 / Set rule metadata */
	rule->data.category_mask = 1;  // 分类器0 / Category 0
	rule->data.priority = priority;
	rule->data.userdata = userdata;  // ACL_ALLOW or ACL_DENY

	/* 字段0：协议 / Field 0: Protocol */
	rule->field[PROTO_FIELD_IPV4].value.u8 = proto;
	rule->field[PROTO_FIELD_IPV4].mask_range.u8 = proto_mask;

	/* 字段1：源IP（主机字节序）/ Field 1: Source IP (host byte order) */
	rule->field[SRC_FIELD_IPV4].value.u32 = src_ip;
	rule->field[SRC_FIELD_IPV4].mask_range.u32 = src_mask_len;  // CIDR位数 / CIDR bits

	/* 字段2：目的IP / Field 2: Destination IP */
	rule->field[DST_FIELD_IPV4].value.u32 = dst_ip;
	rule->field[DST_FIELD_IPV4].mask_range.u32 = dst_mask_len;

	/* 字段3：源端口范围 / Field 3: Source port range */
	rule->field[SRCP_FIELD_IPV4].value.u16 = src_port_low;
	rule->field[SRCP_FIELD_IPV4].mask_range.u16 = src_port_high;

	/* 字段4：目的端口范围 / Field 4: Destination port range */
	rule->field[DSTP_FIELD_IPV4].value.u16 = dst_port_low;
	rule->field[DSTP_FIELD_IPV4].mask_range.u16 = dst_port_high;
}

/**
 * 添加ACL规则 / Add ACL rules
 *
 * 演示5条规则：/ Demonstrates 5 rules:
 * 1. 允许来自192.168.1.0/24的HTTP流量 / Allow HTTP from 192.168.1.0/24
 * 2. 拒绝所有SSH流量 / Deny all SSH
 * 3. 允许DNS查询（UDP） / Allow DNS queries (UDP)
 * 4. 允许高端口范围（1024-65535） / Allow high port range
 * 5. 默认拒绝所有 / Default deny all
 */
static void
add_acl_rules(struct rte_acl_ctx *ctx)
{
	struct acl_ipv4_rule rules[5];
	int ret;

	printf("[步骤 2] 添加防火墙规则...\n");

	/* 规则1：允许HTTP（端口80）来自192.168.1.0/24 [优先级100] */
	/* Rule 1: Allow HTTP (port 80) from 192.168.1.0/24 [Priority 100] */
	make_rule(&rules[0], 100, ACL_ALLOW,
	          IPPROTO_TCP, 0xFF,                    // TCP协议 / TCP protocol
	          RTE_IPV4(192, 168, 1, 0), 24,        // 源IP: 192.168.1.0/24 / Source IP
	          0, 0,                                 // 目的IP: 任意 / Dest IP: any
	          0, 65535,                             // 源端口: 任意 / Source port: any
	          80, 80);                              // 目的端口: 80 / Dest port: 80
	printf("  规则1: 允许 HTTP (端口80) 来自 192.168.1.0/24 [优先级 100]\n");

	/* 规则2：拒绝SSH（端口22）来自任意地址 [优先级90] */
	/* Rule 2: Deny SSH (port 22) from any [Priority 90] */
	make_rule(&rules[1], 90, ACL_DENY,
	          IPPROTO_TCP, 0xFF,
	          0, 0,                                 // 源IP: 任意 / Source IP: any
	          0, 0,                                 // 目的IP: 任意 / Dest IP: any
	          0, 65535,
	          22, 22);                              // 目的端口: 22 / Dest port: 22
	printf("  规则2: 拒绝  SSH (端口22) 来自任意地址    [优先级 90]\n");

	/* 规则3：允许DNS（UDP端口53） [优先级80] */
	/* Rule 3: Allow DNS (UDP port 53) [Priority 80] */
	make_rule(&rules[2], 80, ACL_ALLOW,
	          IPPROTO_UDP, 0xFF,                    // UDP协议 / UDP protocol
	          0, 0,
	          0, 0,
	          0, 65535,
	          53, 53);                              // 目的端口: 53 / Dest port: 53
	printf("  规则3: 允许  DNS (UDP端口53) 来自任意地址  [优先级 80]\n");

	/* 规则4：允许高端口范围（1024-65535） [优先级50] */
	/* Rule 4: Allow high port range (1024-65535) [Priority 50] */
	make_rule(&rules[3], 50, ACL_ALLOW,
	          IPPROTO_TCP, 0xFF,
	          0, 0,
	          0, 0,
	          0, 65535,
	          1024, 65535);                         // 目的端口: 1024-65535 / Dest port: 1024-65535
	printf("  规则4: 允许  高端口范围 (1024-65535)      [优先级 50]\n");

	/* 规则5：默认拒绝所有 [优先级10] */
	/* Rule 5: Default deny all [Priority 10] */
	make_rule(&rules[4], 10, ACL_DENY,
	          0, 0,                                 // 任意协议 / Any protocol
	          0, 0,
	          0, 0,
	          0, 65535,
	          0, 65535);
	printf("  规则5: 拒绝  所有其他流量（默认拒绝）      [优先级 10]\n");

	/* 添加规则到ACL上下文 / Add rules to ACL context */
	ret = rte_acl_add_rules(ctx, (struct rte_acl_rule *)rules, 5);
	if (ret != 0) {
		rte_exit(EXIT_FAILURE, "  错误：添加规则失败: %s\n", strerror(-ret));
	}
	printf("  ✓ 成功添加 5 条规则\n\n");
}

/**
 * 构建ACL / Build ACL
 *
 * 必须在添加完所有规则后调用，构建优化的trie结构
 * Must be called after adding all rules to build the optimized trie structure
 */
static void
build_acl(struct rte_acl_ctx *ctx)
{
	struct rte_acl_config cfg;
	int ret;

	printf("[步骤 3] 构建ACL...\n");

	setup_acl_config(&cfg);

	ret = rte_acl_build(ctx, &cfg);
	if (ret != 0) {
		rte_exit(EXIT_FAILURE, "  错误：构建ACL失败: %s\n", strerror(-ret));
	}
	printf("  ✓ ACL构建成功\n\n");
}

/**
 * 创建测试数据包 / Create test packets
 *
 * 创建5个测试数据包，每个数据包应匹配不同的规则
 * Create 5 test packets, each should match a different rule
 */
static void
create_test_packets(struct ipv4_5tuple *packets)
{
	/* 数据包1：HTTP from 192.168.1.10 → 应该允许（规则1） */
	/* Packet 1: HTTP from 192.168.1.10 → should ALLOW (Rule 1) */
	packets[0].proto = IPPROTO_TCP;
	packets[0].ip_src = htonl(RTE_IPV4(192, 168, 1, 10));   // 网络字节序！/ Network byte order!
	packets[0].ip_dst = htonl(RTE_IPV4(10, 0, 0, 1));
	packets[0].port_src = htons(12345);
	packets[0].port_dst = htons(80);

	/* 数据包2：SSH from 10.0.0.5 → 应该拒绝（规则2） */
	/* Packet 2: SSH from 10.0.0.5 → should DENY (Rule 2) */
	packets[1].proto = IPPROTO_TCP;
	packets[1].ip_src = htonl(RTE_IPV4(10, 0, 0, 5));
	packets[1].ip_dst = htonl(RTE_IPV4(10, 0, 0, 1));
	packets[1].port_src = htons(54321);
	packets[1].port_dst = htons(22);

	/* 数据包3：DNS query → 应该允许（规则3） */
	/* Packet 3: DNS query → should ALLOW (Rule 3) */
	packets[2].proto = IPPROTO_UDP;
	packets[2].ip_src = htonl(RTE_IPV4(8, 8, 8, 8));
	packets[2].ip_dst = htonl(RTE_IPV4(10, 0, 0, 1));
	packets[2].port_src = htons(33445);
	packets[2].port_dst = htons(53);

	/* 数据包4：高端口连接 → 应该允许（规则4） */
	/* Packet 4: High port connection → should ALLOW (Rule 4) */
	packets[3].proto = IPPROTO_TCP;
	packets[3].ip_src = htonl(RTE_IPV4(172, 16, 0, 100));
	packets[3].ip_dst = htonl(RTE_IPV4(10, 0, 0, 1));
	packets[3].port_src = htons(44556);
	packets[3].port_dst = htons(8080);

	/* 数据包5：低端口未知服务 → 应该拒绝（规则5） */
	/* Packet 5: Low port unknown service → should DENY (Rule 5) */
	packets[4].proto = IPPROTO_TCP;
	packets[4].ip_src = htonl(RTE_IPV4(203, 0, 113, 5));
	packets[4].ip_dst = htonl(RTE_IPV4(10, 0, 0, 1));
	packets[4].port_src = htons(11223);
	packets[4].port_dst = htons(445);  // 低于1024的端口 / Port below 1024
}

/**
 * 分类数据包并打印结果 / Classify packets and print results
 */
static void
classify_and_print(struct rte_acl_ctx *ctx, struct ipv4_5tuple *packets, int num_packets)
{
	const uint8_t *data[NUM_TEST_PACKETS];
	uint32_t results[NUM_TEST_PACKETS] = {0};
	int i, ret;

	printf("[步骤 4] 分类测试数据包...\n");

	/* 准备数据指针数组 / Prepare data pointer array */
	for (i = 0; i < num_packets; i++) {
		data[i] = (const uint8_t *)&packets[i];
	}

	/* 批量分类 / Batch classification */
	ret = rte_acl_classify(ctx, data, results, num_packets, 1);
	if (ret != 0) {
		rte_exit(EXIT_FAILURE, "  错误：分类失败: %s\n", strerror(-ret));
	}

	/* 打印分类结果 / Print classification results */
	for (i = 0; i < num_packets; i++) {
		char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
		const char *proto_str, *action_str;
		uint32_t rule_matched;

		/* 转换IP地址为可读格式 / Convert IP to readable format */
		inet_ntop(AF_INET, &packets[i].ip_src, src_ip, sizeof(src_ip));
		inet_ntop(AF_INET, &packets[i].ip_dst, dst_ip, sizeof(dst_ip));

		/* 协议字符串 / Protocol string */
		proto_str = (packets[i].proto == IPPROTO_TCP) ? "TCP" : "UDP";

		/* 判断动作和匹配的规则 / Determine action and matched rule */
		if (results[i] == ACL_ALLOW) {
			action_str = "允许";
			stats.allowed++;

			/* 根据端口判断具体规则 / Determine specific rule based on port */
			if (ntohs(packets[i].port_dst) == 80 &&
			    (ntohl(packets[i].ip_src) & 0xFFFFFF00) == RTE_IPV4(192, 168, 1, 0)) {
				rule_matched = 1;
			} else if (ntohs(packets[i].port_dst) == 53 && packets[i].proto == IPPROTO_UDP) {
				rule_matched = 3;
			} else if (ntohs(packets[i].port_dst) >= 1024) {
				rule_matched = 4;
			} else {
				rule_matched = 0;  // 未知 / Unknown
			}
		} else {
			action_str = "拒绝 ";
			stats.denied++;

			/* 判断是SSH拒绝还是默认拒绝 / Determine if SSH deny or default deny */
			if (ntohs(packets[i].port_dst) == 22) {
				rule_matched = 2;
			} else {
				rule_matched = 5;
			}
		}

		printf("  数据包%d: %s:%-5d → %s:%-5d (%s)  → %s (规则%d)\n",
		       i + 1,
		       src_ip, ntohs(packets[i].port_src),
		       dst_ip, ntohs(packets[i].port_dst),
		       proto_str,
		       action_str,
		       rule_matched);

		stats.total_packets++;
	}
	printf("\n");
}

/**
 * 打印统计信息 / Print statistics
 */
static void
print_statistics(void)
{
	printf("[统计信息]\n");
	printf("  总数据包: %u\n", stats.total_packets);
	printf("  允许: %u\n", stats.allowed);
	printf("  拒绝: %u\n\n", stats.denied);
}

/**
 * 主函数 / Main function
 */
int
main(int argc, char **argv)
{
	int ret;
	struct ipv4_5tuple test_packets[NUM_TEST_PACKETS];

	/* 初始化EAL / Initialize EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		rte_panic("无法初始化EAL: %s\n", rte_strerror(rte_errno));
	}

	printf("=== DPDK ACL 演示：IPv4防火墙 ===\n\n");

	/* 1. 创建ACL上下文 / Create ACL context */
	acl_ctx = create_acl_context();

	/* 2. 添加规则 / Add rules */
	add_acl_rules(acl_ctx);

	/* 3. 构建ACL / Build ACL */
	build_acl(acl_ctx);

	/* 4. 创建测试数据包 / Create test packets */
	create_test_packets(test_packets);

	/* 5. 分类并打印结果 / Classify and print results */
	classify_and_print(acl_ctx, test_packets, NUM_TEST_PACKETS);

	/* 6. 打印统计信息 / Print statistics */
	print_statistics();

	/* 7. 清理资源 / Cleanup */
	printf("[清理]\n");
	if (acl_ctx != NULL) {
		rte_acl_free(acl_ctx);
		printf("  ✓ ACL上下文已释放\n");
	}

	rte_eal_cleanup();
	printf("  ✓ EAL已清理\n\n");

	printf("=== 演示结束 ===\n");
	return 0;
}
