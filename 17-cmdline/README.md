# Lesson 17: DPDK Cmdline 交互式命令行

本课程教授如何使用 DPDK cmdline 库创建交互式命令行界面。

## 快速开始

### 编译

```bash
cd /home/work/clionProject/dpdk-hands-on
mkdir -p build && cd build
cmake ..
make
```

### 运行示例

所有示例都需要 root 权限,可以使用 `--no-pci` 参数无需网卡运行。

## 示例 1: 基础命令 (cmdline_basic)

演示基础命令结构和统计追踪。

```bash
sudo ./bin/cmdline_basic -l 0 --no-pci
```

**可用命令:**
- `help` - 显示帮助信息
- `show version` - 显示版本信息
- `show stats` - 显示统计信息
- `set loglevel <0-8>` - 设置日志级别
- `clear stats` - 清空统计
- `quit` - 退出程序

**示例会话:**
```
dpdk-basic> show version
DPDK Cmdline Example v1.0
Built with DPDK

dpdk-basic> show stats
=== Application Statistics ===
Commands executed:  2
Errors occurred:    0
Uptime:             10 seconds (0 minutes)

dpdk-basic> quit
Exiting... (executed 3 commands)
```

## 示例 2: Token 类型展示 (cmdline_tokens)

演示所有标准 Token 类型的解析。

```bash
sudo ./bin/cmdline_tokens -l 0 --no-pci
```

**可用命令:**
- `ipaddr <addr>` - 解析 IPv4/IPv6 地址
- `macaddr <mac>` - 解析 MAC 地址
- `portlist <list>` - 解析端口列表 (如 "0-3,5,7")
- `number8/16/32/64 <num>` - 解析不同位数的数字
- `range <min> <max>` - 解析数字范围
- `choice <add|del|show>` - 固定选项
- `string <text>` - 任意字符串
- `help` - 显示帮助
- `quit` - 退出

**示例会话:**
```
dpdk-tokens> ipaddr 192.168.1.1

=== IP Address Token ===
Type:    IPv4
Address: 192.168.1.1
Hex:     0xc0a80101

dpdk-tokens> macaddr 00:11:22:33:44:55

=== MAC Address Token ===
Address: 00:11:22:33:44:55
Bytes:   00:11:22:33:44:55
Multicast: No
Broadcast: No

dpdk-tokens> portlist 0-3,5,7

=== Port List Token ===
Port list bitmap: 0x000000af
Ports enabled: 0 1 2 3 5 7
Total ports: 6
```

## 示例 3: Ring 管理器 (cmdline_ring_mgr)

演示 Ring 生命周期管理和动态操作。

```bash
sudo ./bin/cmdline_ring_mgr -l 0 --no-pci
```

**可用命令:**
- `ring create <name> <size>` - 创建 Ring (大小必须是 2 的幂)
- `ring delete <name>` - 删除 Ring
- `ring list` - 列出所有 Ring
- `ring enqueue <name> <value>` - 入队元素
- `ring dequeue <name>` - 出队元素
- `help` - 显示帮助
- `quit` - 退出并清理所有 Ring

**示例会话:**
```
ring-mgr> ring create test_ring 1024
Ring 'test_ring' created (size: 1024)

ring-mgr> ring enqueue test_ring 42
Enqueued value 42 to ring 'test_ring' (total: 1)

ring-mgr> ring enqueue test_ring 100
Enqueued value 100 to ring 'test_ring' (total: 2)

ring-mgr> ring list

Name                 Size       Used       Free
--------------------------------------------------------
test_ring            1024       2          1022

Total: 1 ring(s)

ring-mgr> ring dequeue test_ring
Dequeued value 42 from ring 'test_ring' (total: 1)

ring-mgr> ring delete test_ring
Ring 'test_ring' deleted
```

## 示例 4: 完整管理控制台 (cmdline_console)

演示生产级管理控制台，集成多个 DPDK 子系统。

```bash
sudo ./bin/cmdline_console -l 0 --no-pci
```

**命令分类:**

**Ring 管理:**
- `ring create <name> <size>` - 创建 Ring
- `ring list` - 列出所有 Ring

**Mempool 管理:**
- `mempool create <name> <n> <elt_size>` - 创建 Mempool
- 参数: n=元素数量, elt_size=元素大小(字节)

**系统信息:**
- `lcore list` - 列出所有 lcore 和所属 NUMA socket
- `memory info` - 显示内存使用情况

**统计:**
- `stats show` - 显示全局统计
- `stats export <filename>` - 导出统计到文件

**工具:**
- `help` - 显示帮助
- `quit` - 退出

**示例会话:**
```
dpdk-console> ring create my_ring 2048
Ring 'my_ring' created (size: 2048)

dpdk-console> mempool create my_pool 512 64
Mempool 'my_pool' created (n: 512, elt_size: 64)

dpdk-console> lcore list

Lcore    Socket
-------------------
0        0

dpdk-console> memory info

=== Memory Information ===

Socket 0:
  Total:     2048 MB
  Allocated: 10 MB
  Free:      2038 MB

dpdk-console> stats show

=== Console Statistics ===
Total commands:    5
Ring operations:   1
Mempool operations: 1
Uptime:            30 seconds
Start time:        Tue Dec 17 15:30:00 2024

dpdk-console> stats export /tmp/stats.txt
Statistics exported to '/tmp/stats.txt'
```

## 交互技巧

### 自动补全
- 按 **Tab** 键自动补全命令
- 再次按 Tab 显示所有可能的选项

### 命令历史
- **↑** (上箭头) - 上一条命令
- **↓** (下箭头) - 下一条命令

### 行编辑
- **Ctrl+A** - 移动到行首
- **Ctrl+E** - 移动到行尾
- **Ctrl+K** - 删除光标到行尾的内容
- **Ctrl+Y** - 粘贴之前删除的内容
- **Ctrl+C** - 中断当前输入(某些版本)

### 帮助
- 输入 `help` 查看所有命令
- 输入部分命令后按 Tab 查看选项

## 常见问题

### Q: 编译失败,提示找不到 DPDK?
**A:** 确保 DPDK 已正确安装,并且 pkg-config 可以找到它:
```bash
pkg-config --exists libdpdk && echo "OK" || echo "NOT FOUND"
```

### Q: 运行时提示权限不足?
**A:** Cmdline 示例需要 root 权限:
```bash
sudo ./bin/cmdline_basic -l 0 --no-pci
```

### Q: 如何添加新命令?
**A:** 遵循五步法:
1. 定义结果结构体 (`struct cmd_xxx_result`)
2. 实现回调函数 (`cmd_xxx_parsed()`)
3. 定义 Token (`TOKEN_XXX_INITIALIZER`)
4. 创建 Instruction (`cmdline_parse_inst_t`)
5. 注册到 Context (`main_ctx[]`)

参考示例代码中的现有命令实现。

### Q: Ring 大小必须是 2 的幂?
**A:** 是的,DPDK Ring 要求大小必须是 2 的幂(如 256, 512, 1024, 2048)。这是为了优化性能。

### Q: 如何退出程序?
**A:** 输入 `quit` 命令即可退出。程序会自动清理所有资源。

## 学习路径

建议按以下顺序学习:

1. **示例 1 (cmdline_basic)** - 理解基本概念和命令结构
2. **示例 2 (cmdline_tokens)** - 掌握所有 Token 类型
3. **示例 3 (cmdline_ring_mgr)** - 学习 DPDK 集成和动态补全
4. **示例 4 (cmdline_console)** - 了解生产级控制台设计

## 扩展阅读

- 完整教程: [docs/lessons/lesson17-cmdline.md](../docs/lessons/lesson17-cmdline.md)
- DPDK 官方文档: https://doc.dpdk.org/guides/prog_guide/cmdline.html
- DPDK cmdline 源码: `/home/work/dpdk-stable-24.11.1/lib/cmdline/`
- 官方示例: `/home/work/dpdk-stable-24.11.1/examples/cmdline/`

## 与其他课程的关联

- **Lesson 1 (Helloworld)** - EAL 初始化基础
- **Lesson 5 (Mempool)** - Mempool 操作 (示例 4 使用)
- **Lesson 10-11 (Ring)** - Ring 队列操作 (示例 3-4 使用)

掌握 cmdline 后,可以为任何 DPDK 应用添加交互式调试和管理功能!
