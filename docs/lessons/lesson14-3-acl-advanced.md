# Lesson 14-3: DPDK ACL 性能优化与进阶

## 课程简介

在前两节课程中，你已经掌握了 ACL 的基础知识和实战应用：

- **[Lesson 14-1](lesson14-1-acl-basics.md)**: ACL核心概念和数据结构
- **[Lesson 14-2](lesson14-2-acl-practice.md)**: API详解和完整示例代码

本课程将深入探讨 ACL 的性能优化、高级应用和常见问题解决。

**学习目标**:
- 掌握 ACL 性能优化技巧
- 学习多分类器应用
- 了解通配符和范围的高级用法
- 能够解决常见问题
- 为生产环境部署做准备

**前置知识**: 完成 Lesson 14-1 和 14-2

---

## 七、进阶话题

### 7.1 性能优化

#### 7.1.1 批量分类

单个数据包分类的开销较大，建议使用批量分类：

```c
#define BURST_SIZE 32

struct ipv4_5tuple packets[BURST_SIZE];
const uint8_t *data[BURST_SIZE];
uint32_t results[BURST_SIZE];

// 准备批量数据
for (int i = 0; i < BURST_SIZE; i++) {
    data[i] = (uint8_t *)&packets[i];
}

// 一次分类 32 个数据包（性能提升 10-20x）
rte_acl_classify(acl_ctx, data, results, BURST_SIZE, 1);
```

**性能对比：**

| 方式 | 吞吐量 | CPU 占用 |
|------|--------|---------|
| 单个分类 | ~1 Mpps | 100% |
| 批量分类（32） | ~15 Mpps | 100% |
| 提升倍数 | **15x** | - |

**批量大小选择建议：**

| 批量大小 | 吞吐量 | 延迟 | 适用场景 |
|---------|--------|------|---------|
| 1-4 | 低 | 最低 | 低延迟要求 |
| 8-16 | 中 | 低 | 平衡场景 |
| **32-64** | **高** | **中** | **推荐** |
| 128+ | 最高 | 高 | 高吞吐量要求 |

#### 7.1.2 规则排序优化

将最常匹配的规则设置为高优先级，减少平均查找时间：

```
❌ 差的排序：
  规则 1（优先级 100）：极少匹配的规则
  规则 2（优先级 90）： 常见规则
  规则 3（优先级 80）： 最常见规则

✅ 好的排序：
  规则 1（优先级 100）：最常见规则（HTTP）
  规则 2（优先级 90）： 常见规则（HTTPS）
  规则 3（优先级 80）： 不常见规则
```

**实战案例：Web 服务器防火墙**

```c
// 优先级从高到低
make_rule(&rules[0], 100, ACL_ALLOW,
          IPPROTO_TCP, 0xFF,
          0, 0, 0, 0,
          0, 65535,
          80, 80);   // HTTP - 最常见（优先级最高）

make_rule(&rules[1], 90, ACL_ALLOW,
          IPPROTO_TCP, 0xFF,
          0, 0, 0, 0,
          0, 65535,
          443, 443);  // HTTPS - 常见

make_rule(&rules[2], 80, ACL_ALLOW,
          IPPROTO_TCP, 0xFF,
          0, 0, 0, 0,
          0, 65535,
          8080, 8080);  // Alt HTTP - 较少见

make_rule(&rules[3], 10, ACL_DENY,
          0, 0, 0, 0, 0, 0,
          0, 65535,
          0, 65535);  // 默认拒绝 - 最低优先级
```

#### 7.1.3 NUMA 感知

确保 ACL 上下文创建在数据处理的同一 NUMA 节点：

```c
struct rte_acl_param acl_param = {
    .name = "ipv4_acl",
    .socket_id = rte_socket_id(),  // ← 当前 lcore 的 NUMA 节点
    .rule_size = RTE_ACL_RULE_SZ(NUM_FIELDS_IPV4),
    .max_rule_num = 100,
};
```

**NUMA 优化要点：**

1. **ACL 上下文**：创建在处理数据包的 lcore 所在 NUMA 节点
2. **数据包缓冲区**：使用同一 NUMA 节点的 mempool
3. **避免跨 NUMA 访问**：可导致 30-50% 性能下降

**验证 NUMA 配置：**

```c
printf("ACL Context on Socket %u\n", rte_socket_id());
printf("Lcore %u on Socket %u\n",
       rte_lcore_id(),
       rte_lcore_to_socket_id(rte_lcore_id()));
```

### 7.2 多分类器应用

使用多个 category 实现独立的规则集：

```c
// 配置两个 category
struct rte_acl_config cfg;
cfg.num_categories = 2;  // Category 0: 入站, Category 1: 出站

// 添加入站规则
rule1.data.category_mask = 0x1;  // Bit 0 = Category 0
rte_acl_add_rules(acl_ctx, &rule1, 1);

// 添加出站规则
rule2.data.category_mask = 0x2;  // Bit 1 = Category 1
rte_acl_add_rules(acl_ctx, &rule2, 1);

// 分类时指定 category 数量
rte_acl_classify(acl_ctx, data, results, num, 2);  // 2 categories
```

**结果数组布局：**

```
results[] = [
    results[0],  // Category 0 结果（入站）
    results[1],  // Category 1 结果（出站）
    results[2],  // 下一个数据包的 Category 0
    results[3],  // 下一个数据包的 Category 1
    // ...
];
```

**应用场景：入站/出站防火墙**

```c
// Category 0: 入站规则（保护服务器）
rule_inbound.data.category_mask = 0x1;
rule_inbound.data.priority = 100;
rule_inbound.data.userdata = ACL_ALLOW;
// 允许入站 HTTP/HTTPS

// Category 1: 出站规则（限制客户端）
rule_outbound.data.category_mask = 0x2;
rule_outbound.data.priority = 100;
rule_outbound.data.userdata = ACL_DENY;
// 拒绝出站敏感端口

// 分类一个数据包
rte_acl_classify(acl_ctx, data, results, 1, 2);

printf("入站检查: %s\n", results[0] == ACL_ALLOW ? "允许" : "拒绝");
printf("出站检查: %s\n", results[1] == ACL_ALLOW ? "允许" : "拒绝");
```

### 7.3 通配符和范围技巧

#### 7.3.1 匹配任意 IP

```c
// 匹配所有源 IP
rule.field[SRC_FIELD_IPV4].value.u32 = 0;
rule.field[SRC_FIELD_IPV4].mask_range.u32 = 0;  // CIDR /0
```

#### 7.3.2 匹配特定子网

```c
// 匹配 10.0.0.0/8（10.0.0.0 - 10.255.255.255）
rule.field[SRC_FIELD_IPV4].value.u32 = RTE_IPV4(10, 0, 0, 0);
rule.field[SRC_FIELD_IPV4].mask_range.u32 = 8;

// 匹配 192.168.1.0/24（192.168.1.0 - 192.168.1.255）
rule.field[SRC_FIELD_IPV4].value.u32 = RTE_IPV4(192, 168, 1, 0);
rule.field[SRC_FIELD_IPV4].mask_range.u32 = 24;

// 匹配私有网络 172.16.0.0/12
rule.field[SRC_FIELD_IPV4].value.u32 = RTE_IPV4(172, 16, 0, 0);
rule.field[SRC_FIELD_IPV4].mask_range.u32 = 12;
```

**常用子网速查表：**

| CIDR | 子网掩码 | 地址数量 | 用途 |
|------|---------|---------|------|
| /32 | 255.255.255.255 | 1 | 单个主机 |
| /24 | 255.255.255.0 | 256 | C类网络 |
| /16 | 255.255.0.0 | 65536 | B类网络 |
| /8 | 255.0.0.0 | 16777216 | A类网络 |
| /0 | 0.0.0.0 | 全部 | 所有IP |

#### 7.3.3 匹配端口范围

```c
// 匹配高端口（1024-65535）
rule.field[DSTP_FIELD_IPV4].value.u16 = 1024;
rule.field[DSTP_FIELD_IPV4].mask_range.u16 = 65535;

// 匹配特定范围（8000-9000）
rule.field[DSTP_FIELD_IPV4].value.u16 = 8000;
rule.field[DSTP_FIELD_IPV4].mask_range.u16 = 9000;

// 匹配知名端口（0-1023）
rule.field[DSTP_FIELD_IPV4].value.u16 = 0;
rule.field[DSTP_FIELD_IPV4].mask_range.u16 = 1023;
```

**常用端口范围：**

| 范围 | 名称 | 用途 |
|------|------|------|
| 0-1023 | 知名端口 | HTTP(80), HTTPS(443), SSH(22) |
| 1024-49151 | 注册端口 | 应用程序服务 |
| 49152-65535 | 动态端口 | 客户端临时端口 |

#### 7.3.4 匹配多个协议

使用位掩码匹配 TCP 或 UDP：

```c
// 匹配 TCP (6) 或 UDP (17)
rule.field[PROTO_FIELD_IPV4].value.u8 = 6 | 17;  // 不推荐
// 推荐：创建两条独立规则
```

**推荐方式：**

```c
// 规则 1: TCP
make_rule(&rule1, 100, ACL_ALLOW,
          IPPROTO_TCP, 0xFF,  // 精确匹配 TCP
          ...);

// 规则 2: UDP
make_rule(&rule2, 100, ACL_ALLOW,
          IPPROTO_UDP, 0xFF,  // 精确匹配 UDP
          ...);
```

### 7.4 动态更新规则

ACL 构建后不能直接添加规则，需要重置：

```c
// 重置 ACL（清空所有规则）
rte_acl_reset(acl_ctx);

// 添加新规则
rte_acl_add_rules(acl_ctx, new_rules, num_new);

// 重新构建
rte_acl_build(acl_ctx, &cfg);
```

**注意：** 重新构建会短暂阻塞分类操作，适合规则更新频率低的场景。

**最佳实践：**

```c
// 1. 创建新的 ACL 上下文
struct rte_acl_ctx *new_acl = rte_acl_create(&acl_param);
rte_acl_add_rules(new_acl, new_rules, num_new);
rte_acl_build(new_acl, &cfg);

// 2. 原子切换（使用 RCU 或双缓冲）
struct rte_acl_ctx *old_acl = acl_ctx;
rte_wmb();  // 写内存屏障
acl_ctx = new_acl;  // 原子切换指针

// 3. 等待所有线程完成旧 ACL 使用
rte_eal_mp_wait_lcore();

// 4. 释放旧 ACL
rte_acl_free(old_acl);
```

---

## 八、常见问题

### Q1: 构建 ACL 失败，提示 "Invalid argument"

**原因：** 忘记调用 `rte_acl_build()` 或字段定义错误。

**解决：**
```c
// 确保添加规则后调用 build
rte_acl_add_rules(acl_ctx, rules, num);
rte_acl_build(acl_ctx, &cfg);  // ← 必须调用
```

**检查清单：**
- ✅ 是否调用了 `rte_acl_build()`？
- ✅ 字段定义中的 `input_index` 是否正确？
- ✅ 规则数量是否超过 `max_rule_num`？
- ✅ 字段偏移量是否使用 `offsetof()` 计算？

### Q2: 数据包无法匹配规则

**原因 1：** 字节序错误（规则用主机序，数据包用网络序）。

```c
// ❌ 错误：数据包用主机序
pkt.ip_src = RTE_IPV4(192, 168, 1, 10);

// ✅ 正确：数据包用网络序
pkt.ip_src = htonl(RTE_IPV4(192, 168, 1, 10));
```

**原因 2：** `input_index` 分组错误。

```c
// ❌ 错误：两个端口字段 input_index 不同
{.field_index = 3, .input_index = 3, ...},  // 源端口
{.field_index = 4, .input_index = 4, ...},  // 目的端口

// ✅ 正确：两个端口共享 input_index
{.field_index = 3, .input_index = 3, ...},  // 源端口
{.field_index = 4, .input_index = 3, ...},  // 目的端口（同 3）
```

**调试技巧：**

```c
// 打印数据包信息
printf("Packet: proto=%u, src_ip=%08x, dst_ip=%08x, "
       "src_port=%u, dst_port=%u\n",
       pkt.proto, ntohl(pkt.ip_src), ntohl(pkt.ip_dst),
       ntohs(pkt.port_src), ntohs(pkt.port_dst));

// 打印规则信息
printf("Rule: proto=%u/%u, src_ip=%08x/%u, dst_port=%u-%u\n",
       rule.field[0].value.u8, rule.field[0].mask_range.u8,
       rule.field[1].value.u32, rule.field[1].mask_range.u32,
       rule.field[4].value.u16, rule.field[4].mask_range.u16);
```

### Q3: 性能不如预期

**原因：** 使用单个数据包分类，未使用批量分类。

**解决：**
```c
// ❌ 慢：逐个分类
for (int i = 0; i < n; i++) {
    rte_acl_classify(acl_ctx, &data[i], &results[i], 1, 1);
}

// ✅ 快：批量分类
rte_acl_classify(acl_ctx, data, results, n, 1);
```

**性能优化检查清单：**
- ✅ 使用批量分类（32-64个数据包）
- ✅ 常用规则优先级更高
- ✅ ACL 上下文在正确的 NUMA 节点
- ✅ 规则数量 < 1000

### Q4: 规则数量有限制吗？

**限制：**
- 创建时指定 `max_rule_num`（如 100、1000）
- 实际限制取决于内存和性能需求
- 建议：< 1000 条规则性能最佳

**规则数量 vs 性能：**

| 规则数量 | 查找时间 | 内存占用 | 构建时间 |
|---------|---------|---------|---------|
| < 100 | 快 | 低 | 毫秒级 |
| 100-1000 | 中 | 中 | 秒级 |
| 1000-10000 | 较慢 | 高 | 分钟级 |
| > 10000 | 慢 | 很高 | 长时间 |

**超过限制的解决方案：**
1. 使用多个 ACL 上下文
2. 结合 Hash 表预过滤
3. 优化规则合并相似规则

### Q5: 如何调试匹配结果？

**方法：** 打印数据包和规则的详细信息。

```c
// 打印数据包
printf("数据包: proto=%u, src_ip=0x%08x, dst_ip=0x%08x, "
       "src_port=%u, dst_port=%u\n",
       pkt.proto, ntohl(pkt.ip_src), ntohl(pkt.ip_dst),
       ntohs(pkt.port_src), ntohs(pkt.port_dst));

// 打印规则
printf("规则: proto=%u/%u, src_ip=0x%08x/%u, dst_port=%u-%u\n",
       rule.field[0].value.u8, rule.field[0].mask_range.u8,
       rule.field[1].value.u32, rule.field[1].mask_range.u32,
       rule.field[4].value.u16, rule.field[4].mask_range.u16);
```

**使用 DPDK 日志系统：**

```c
RTE_LOG(DEBUG, USER1, "ACL match result: %u\n", results[0]);

// 运行时启用详细日志
sudo ./app -l 0 --no-pci --log-level=user1:debug
```

### Q6: ACL vs Hash 表，如何选择？

**对比：**

| 特性 | ACL | Hash 表 |
|------|-----|---------|
| **查找方式** | 多字段匹配 | 精确匹配 |
| **通配符** | 支持子网/范围 | 不支持 |
| **优先级** | 支持 | 不支持 |
| **性能** | O(1) ~ O(log n) | O(1) |
| **适用场景** | 防火墙、QoS | 流表、会话跟踪 |

**选择建议：**
- **需要通配符/范围匹配** → 使用 ACL
- **需要精确匹配** → 使用 Hash 表
- **需要优先级** → 使用 ACL
- **规则数量巨大（> 10000）** → 考虑 Hash 表

**组合使用：**

```c
// 1. ACL 粗粒度过滤（子网、端口范围）
rte_acl_classify(acl_ctx, data, acl_results, num, 1);

// 2. Hash 表精确查找（5元组流表）
for (i = 0; i < num; i++) {
    if (acl_results[i] == ACL_ALLOW) {
        flow = hash_lookup(hash_table, &packets[i]);
        // 进一步处理...
    }
}
```

---

## 九、总结

### 关键知识点

1. **ACL 基本概念**
   - 5 元组：src_ip, dst_ip, src_port, dst_port, protocol
   - 用于高性能数据包分类和过滤
   - 使用 trie 结构实现 O(1) 平均查找

2. **核心流程**
   ```
   创建上下文 → 添加规则 → 构建 ACL → 分类数据包
   ```

3. **字段定义要点**
   - `input_index` 用于 4 字节对齐分组
   - 三种匹配类型：MASK（IP）、RANGE（端口）、BITMASK（协议）
   - 偏移量使用 `offsetof()` 计算

4. **字节序关键点**
   - 规则字段：**主机字节序**
   - 数据包数据：**网络字节序**（需 htonl/htons）

5. **性能优化**
   - 批量分类（32-64 个数据包）
   - 常用规则设置高优先级
   - NUMA 感知分配

### API 速查表

| API | 功能 | 调用时机 |
|-----|------|---------|
| `rte_acl_create()` | 创建 ACL 上下文 | 初始化阶段 |
| `rte_acl_add_rules()` | 添加规则 | 构建前 |
| `rte_acl_build()` | 构建 trie 结构 | 添加完规则后 |
| `rte_acl_classify()` | 分类数据包 | 运行时 |
| `rte_acl_reset()` | 重置规则 | 更新规则时 |
| `rte_acl_free()` | 释放上下文 | 清理阶段 |

### ACL vs 其他查找方法

| 方法 | 查找时间 | 通配符 | 优先级 | 适用场景 |
|------|---------|--------|--------|---------|
| **ACL** | O(1) ~ O(log n) | ✅ | ✅ | 防火墙、QoS、DDoS 防护 |
| **Hash** | O(1) | ❌ | ❌ | 流表、精确匹配 |
| **LPM** | O(log n) | ✅ | ❌ | IP 路由查找 |
| **线性** | O(n) | ✅ | ✅ | 规则少（< 10） |

### 完整学习路径回顾

通过完成 ACL 三部曲，你已经全面掌握：

1. **[Lesson 14-1](lesson14-1-acl-basics.md)**: 理论基础
   - ACL 原理和优势
   - 五元组和匹配类型
   - 数据结构设计

2. **[Lesson 14-2](lesson14-2-acl-practice.md)**: 实战应用
   - API 详细使用
   - 完整示例代码
   - 编译、运行、调试

3. **Lesson 14-3** (本课): 性能优化
   - 批量分类和NUMA优化
   - 多分类器应用
   - 常见问题解决

### 下一步学习

- **Lesson 15**：DPDK LPM（最长前缀匹配）- IP 路由查找
- **高级主题**：结合 ACL + Hash 表实现复杂分类
- **性能调优**：使用 `perf` 分析 ACL 性能瓶颈

### 生产环境部署建议

1. **规则设计**
   - 规则数量控制在 1000 以内
   - 高频规则优先级更高
   - 定期审计和优化规则

2. **性能监控**
   - 监控分类吞吐量和延迟
   - 使用批量分类（32-64）
   - 关注 CPU 占用和缓存命中率

3. **可靠性保证**
   - 使用双缓冲实现无缝规则更新
   - 添加规则验证和测试
   - 记录规则变更历史

4. **安全考虑**
   - 默认拒绝策略
   - 限制规则复杂度防止DoS
   - 日志记录关键匹配事件

### 参考资料

- [DPDK 官方文档：Packet Classification and Access Control](https://doc.dpdk.org/guides/prog_guide/packet_classif_access_ctrl.html)
- [DPDK API 参考：rte_acl.h](https://doc.dpdk.org/api/rte__acl_8h.html)
- [示例代码：l3fwd-acl](https://doc.dpdk.org/guides/sample_app_ug/l3_forward_access_ctrl.html)
- [Intel 白皮书：DPDK ACL Performance Optimization](https://www.intel.com/content/www/us/en/architecture-and-technology/data-plane-development-kit.html)

---

## 实践建议

### 进阶练习

1. **性能测试**
   - 测试不同批量大小的性能差异
   - 比较单 category 和多 category 的开销
   - 测量规则数量对性能的影响

2. **真实场景**
   - 结合 Lesson 3-4 实现真实防火墙
   - 从 pcap 文件读取流量测试
   - 实现流量统计和日志记录

3. **高级功能**
   - 实现动态规则更新（无阻塞）
   - 结合 Hash 表实现流状态跟踪
   - 添加规则管理 API（增删改查）

### 性能调优步骤

1. **Baseline 测试**
   ```bash
   # 测试单个分类性能
   sudo ./acl_demo -l 0 --no-pci
   ```

2. **批量优化**
   - 修改 `NUM_TEST_PACKETS` 为 32/64/128
   - 测量吞吐量提升

3. **NUMA 优化**
   ```bash
   # 查看 NUMA 拓扑
   numactl --hardware

   # 绑定到特定 NUMA 节点
   sudo numactl --cpunodebind=0 --membind=0 ./acl_demo -l 0 --no-pci
   ```

4. **使用 perf 分析**
   ```bash
   sudo perf record -g ./acl_demo -l 0 --no-pci
   sudo perf report
   ```

**恭喜！** 🎉 你已经完成了 DPDK ACL 的全部学习。现在你可以在生产环境中自信地使用 ACL 进行高性能数据包分类和过滤了！
