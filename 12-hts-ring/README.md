# Lesson 12: DPDK Ring HTS模式演示

## 简介

本示例演示DPDK Ring的HTS（Head-Tail Sync）模式，包括性能测试、与MP/MC对比，以及HTS独有的Peek API功能。

## 编译

```bash
# 在项目根目录
cd /home/work/clionProject/dpdk-hands-on
mkdir -p build && cd build
cmake ..
make hts_ring
```

## 运行

### 基础运行（单核）

```bash
sudo ./bin/hts_ring -l 0 --no-pci
```

### 多核运行（推荐）

多核运行可以看到更完整的多线程测试效果：

```bash
sudo ./bin/hts_ring -l 0-3 --no-pci
```

## 测试内容

### Test 1: HTS性能测试
- 测试HTS模式的基本吞吐量
- 100万次入队/出队操作

### Test 2: HTS vs MP/MC对比
- 对比HTS和传统MP/MC模式
- 展示不同场景下的性能差异

### Test 3: Peek API演示
- 演示HTS独有的"先看再取"功能
- 根据消息优先级条件式出队
- 展示accept/reject机制

### Test 4: 多线程HTS测试
- 多个worker线程同时操作
- 验证HTS的同步正确性

## 关键代码

### 创建HTS Ring

```c
struct rte_ring *ring = rte_ring_create(
    "hts_ring",
    1024,
    rte_socket_id(),
    RING_F_MP_HTS_ENQ | RING_F_MC_HTS_DEQ  // HTS标志
);
```

### 使用Peek API

```c
/* 查看队头元素 */
ret = rte_ring_dequeue_bulk_start(ring, &obj, 1, NULL);

if (ret != 0) {
    if (should_accept(obj)) {
        rte_ring_dequeue_finish(ring, 1);  // 确认取出
    } else {
        rte_ring_dequeue_finish(ring, 0);  // 取消，保留在Ring中
    }
}
```

## 性能预期

在物理机上：
- **MP/MC**: ~14-15 Mpps
- **HTS**: ~12-13 Mpps（慢10-20%）

在虚拟机/容器上：
- **HTS性能会更优**（快30-50%）

## 学习要点

1. **HTS适用场景**
   - 虚拟机/容器环境
   - 过度提交场景（线程数 > CPU核心数）
   - 需要Peek API

2. **Peek API用途**
   - 条件过滤消息
   - 实现优先级队列
   - 根据内容决定是否处理

3. **模式选择**
   - 单生产者单消费者：SP/SC（最快）
   - 物理机多线程：MP/MC
   - 虚拟机/容器：HTS
   - 需要Peek：HTS

## 相关文档

- [DPDK-Ring-HTS-Guide.md](../DPDK-Ring-HTS-Guide.md) - HTS模式详细指南
- [lesson12-hts-ring.md](../lesson12-hts-ring.md) - 完整课程文档
- [Lesson 10](../lesson10-ring-spsc-mpmc.md) - SP/SC模式
- [Lesson 11](../lesson11-mp-mc-ring.md) - MP/MC模式

## 故障排除

### "Cannot init EAL"
```bash
# 配置hugepages
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

### "Need at least X lcores"
```bash
# 增加CPU核心数（使用-l参数）
sudo ./bin/hts_ring -l 0-3 --no-pci
```

### 权限错误
```bash
# 使用sudo运行
sudo ./bin/hts_ring -l 0 --no-pci
```
