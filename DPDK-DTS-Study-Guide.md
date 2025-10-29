# DPDK DTS 测试框架学习指南

## 目录
- [什么是DTS？](#什么是dts)
- [新手学习路线](#新手学习路线)
- [编写测试用例入门](#编写测试用例入门)
- [学习建议](#学习建议)
- [核心概念速记](#核心概念速记)
- [快速上手Checklist](#快速上手checklist)

---

## 什么是DTS？

**DPDK Test Suite（DTS）** 是一个基于Python的自动化测试框架，用于在真实硬件环境中验证DPDK的功能正确性和性能表现。

### 主要功能
- **功能测试**：验证DPDK特性的正确实现
- **性能测试**：衡量DPDK在特定硬件配置下的性能指标
- **自动化测试管理**：通过配置文件统一管理测试执行

---

## 新手学习路线

### 第一步：理解DTS架构

DTS使用**分布式三节点架构**：

```
┌─────────────────┐         ┌──────────────────┐
│  DTS运行环境    │  SSH    │   SUT节点        │
│  (Python框架)   ├────────>│  (DPDK + 网卡)   │
└─────────────────┘         └──────────────────┘
         │                           ▲
         │ SSH                       │ 数据包
         ▼                           │
┌─────────────────┐                 │
│  TG节点         ├─────────────────┘
│  (Scapy流量生成)│
└─────────────────┘
```

#### 节点类型说明

| 节点类型 | 功能说明 |
|---------|---------|
| **DTS运行环境** | 您的开发机器，运行测试脚本和框架 |
| **SUT（System Under Test）** | 运行DPDK的目标服务器，包含被测试的网卡等硬件 |
| **TG（Traffic Generator）** | 发送测试流量的机器，用于生成测试数据包 |

---

### 第二步：准备环境

#### 软件要求

```bash
# 1. 检查Python版本（需要3.10+）
python3 --version

# 2. 安装Poetry（依赖管理工具）
curl -sSL https://install.python-poetry.org | python3 -

# 3. 在DPDK源码目录安装DTS依赖
cd <dpdk-source>/dts
poetry install
poetry shell
```

#### 硬件/网络要求

**SUT节点准备**：
- 配置HugePages（2MB或1GB页面）
- 创建具有无密码sudo权限的用户
- 安装DPDK编译依赖（参考DPDK Getting Started Guide）
- 确保网卡支持DPDK驱动（如vfio-pci）

**TG节点准备**：
- 安装Scapy流量生成工具：
  ```bash
  pip install scapy==2.5.0
  ```
- 创建具有无密码sudo权限的用户
- 确保与SUT节点网络连通

**SSH配置**：
- 所有节点配置SSH公钥认证
- DTS通过SSH自动连接和控制各节点
- 密钥文件位置：`~/.ssh/`

---

### 第三步：配置文件

DTS需要两个YAML配置文件：

#### 1. 节点配置文件（nodes.yaml）

定义SUT和TG节点的硬件和网络信息：

```yaml
# SUT节点配置
- name: "SUT 1"
  hostname: 192.168.1.100      # SUT主机IP或域名
  user: dpdk-user              # SSH登录用户
  os: linux                    # 操作系统类型
  ports:
    - name: port-0
      pci: "0000:00:08.0"      # 网卡PCI地址
      os_driver_for_dpdk: vfio-pci  # DPDK驱动类型
      peer_node: "TG 1"        # 对端TG节点
      peer_pci: "0000:00:08.0" # 对端网卡PCI地址

# TG节点配置
- name: "TG 1"
  hostname: 192.168.1.101
  user: tg-user
  os: linux
  ports:
    - name: port-0
      pci: "0000:00:08.0"
```

#### 2. 测试运行配置文件（test_run.yaml）

定义测试执行参数和要运行的测试套件：

```yaml
# DPDK构建选项
dpdk_location: /path/to/dpdk     # DPDK源码位置
compiler: gcc                     # 编译器选择
compiler_wrapper: ccache          # 编译加速工具（可选）

# 测试执行配置
test_suites:
  - hello_world                   # 要运行的测试套件列表
  - pmd_buffer_scatter

# 虚拟设备配置（可选）
vdevs:
  - "crypto_openssl"
```

---

### 第四步：运行第一个测试

```bash
# 在DPDK源码的dts目录下执行
./main.py --dpdk-tree /path/to/dpdk \
          --nodes-config-file nodes.yaml \
          --test-run-config-file test_run.yaml \
          --test-suite hello_world
```

#### 常用命令行参数

| 参数 | 说明 | 默认值 |
|-----|------|--------|
| `--dpdk-tree` | DPDK源码路径 | 必需 |
| `--nodes-config-file` | 节点配置文件 | conf/nodes.yaml |
| `--test-run-config-file` | 测试运行配置 | conf/test_run.yaml |
| `--test-suite` | 指定运行的测试套件 | 运行所有套件 |
| `--timeout` | 操作超时时间（秒） | 15 |
| `--compile-timeout` | DPDK编译超时（秒） | 1200 |
| `--re-run` | 失败测试重试次数 | 0 |
| `--random-seed` | 随机种子控制 | 随机生成 |

#### 查看测试结果

测试完成后，结果保存在输出目录中：
- 包含详细的测试日志
- 测试统计信息（通过/失败数量）
- DPDK版本和配置信息

---

## 编写测试用例入门

### 基本测试套件结构

```python
from framework.test_suite import TestSuite

class TestHelloWorld(TestSuite):
    """
    Hello World测试套件

    测试DPDK hello world示例应用的基本功能
    """

    def set_up_suite(self):
        """
        测试套件初始化（整个套件运行一次）

        用于：
        - 编译DPDK应用
        - 配置全局资源
        - 初始化测试环境
        """
        self.app_hello_world_path = self.sut_node.build_dpdk_apps("helloworld")

    def tear_down_suite(self):
        """
        测试套件清理（整个套件运行一次）

        用于：
        - 释放资源
        - 清理临时文件
        - 恢复环境配置
        """
        pass

    def set_up_test_case(self):
        """
        单个测试用例前准备（每个test_方法前运行）
        """
        pass

    def tear_down_test_case(self):
        """
        单个测试用例后清理（每个test_方法后运行）
        """
        pass

    def test_hello_world_single_core(self):
        """
        功能测试：单核运行hello world

        步骤：
        1. 在单个lcore上启动hello world应用
        2. 验证应用成功启动
        3. 检查输出包含预期的"hello from core"消息
        """
        # 启动DPDK应用
        eal_params = self.create_eal_params(cores="1")
        result = self.run_dpdk_app(self.app_hello_world_path, eal_params)

        # 验证结果
        self.verify("hello from core" in result, "未找到预期的hello消息")

    def test_perf_throughput(self):
        """
        性能测试：吞吐量测试

        测试在特定配置下的数据包吞吐量性能
        """
        # 配置测试参数
        # 发送测试流量
        # 收集性能指标
        pass
```

### 测试方法命名规范

| 方法前缀 | 用途 | 示例 |
|---------|------|------|
| `test_` | 功能测试用例 | `test_hello_world_single_core` |
| `test_perf_` | 性能测试用例 | `test_perf_nic_single_core` |
| `set_up_suite` | 套件级初始化 | - |
| `tear_down_suite` | 套件级清理 | - |
| `set_up_test_case` | 用例级初始化 | - |
| `tear_down_test_case` | 用例级清理 | - |

### 验证机制

使用 `verify()` 方法进行断言检查：

```python
# 基本用法
self.verify(condition, "失败时的错误信息")

# 示例
self.verify(packet_count > 0, "未收到任何数据包")
self.verify("error" not in output, f"应用输出错误: {output}")
self.verify(throughput >= expected_throughput,
           f"吞吐量 {throughput} 低于预期 {expected_throughput}")
```

### 文档字符串规范

DTS要求使用**Google风格**文档字符串：

```python
def test_example(self):
    """
    简短描述（一行）

    详细描述可以分多段：
    - 测试目标
    - 测试步骤
    - 预期结果

    Args:
        param1: 参数1的描述
        param2: 参数2的描述

    Returns:
        返回值描述

    Raises:
        ExceptionType: 异常情况说明
    """
    pass
```

---

## 学习建议

### 1. 从简单示例开始

推荐学习顺序：

1. **阅读hello_world测试套件**
   ```bash
   # 查看源码
   cat dts/tests/TestSuite_hello_world.py
   ```

2. **运行现有测试套件**
   ```bash
   ./main.py --test-suite hello_world
   ```
   观察日志输出，理解测试执行流程

3. **修改简单测试用例**
   - 改变core数量
   - 修改验证条件
   - 添加调试输出

4. **编写自定义测试**
   - 从hello_world模板开始
   - 逐步增加复杂度

### 2. 关键学习资源

#### 官方文档
```bash
# 编译DPDK文档（包含DTS API文档）
cd <dpdk-source>
meson setup build
ninja -C build doc

# 查看DTS文档
firefox build/doc/api/dts/html/index.html
```

#### 示例配置文件
```bash
# 查看示例配置
cat dts/conf/nodes.example.yaml
cat dts/conf/test_run.example.yaml

# 复制并修改为自己的配置
cp dts/conf/nodes.example.yaml dts/conf/nodes.yaml
cp dts/conf/test_run.example.yaml dts/conf/test_run.yaml
```

#### 现有测试套件源码
```bash
# 浏览所有测试套件
ls dts/tests/TestSuite_*.py

# 推荐学习的测试套件（按难度排序）
# 1. hello_world - 最简单
# 2. pmd_buffer_scatter - 数据包处理
# 3. checksum_offload - 硬件offload特性
```

### 3. 代码质量检查

DTS提供了自动化检查工具：

```bash
# 运行格式检查
./devtools/dts-check-format.sh

# 单独运行各工具
poetry run ruff check .        # 代码风格检查
poetry run ruff format .       # 代码格式化
poetry run mypy .              # 类型检查
```

### 4. 调试技巧

#### 查看详细日志
```bash
# 测试结果默认保存在输出目录
ls output/

# 查看最近的测试日志
cat output/dts.log
```

#### 增加超时避免误判
```bash
# 对于慢速环境，增加超时时间
./main.py --timeout 60 --compile-timeout 3600
```

#### 验证环境配置
```bash
# 1. 先测试SSH连接
ssh dpdk-user@192.168.1.100

# 2. 在SUT上手动编译DPDK
cd /path/to/dpdk
meson setup build
ninja -C build

# 3. 验证网卡PCI地址
lspci | grep Ethernet

# 4. 检查HugePages配置
cat /proc/meminfo | grep Huge
```

#### 单步调试测试用例
```python
# 在测试代码中添加断点
import pdb; pdb.set_trace()

# 或使用日志输出
self.logger.info(f"调试信息: {variable}")
```

### 5. 最佳实践

#### 测试设计原则
- **单一职责**：一个特性对应一个测试套件
- **适度粒度**：单个测试套件运行时间不应过长（可拆分）
- **独立性**：测试用例之间不应相互依赖
- **可重复性**：测试结果应该可重现

#### 配置管理
- 使用版本控制管理配置文件（排除敏感信息）
- 为不同环境创建不同的配置文件
- 使用注释说明配置项的含义

#### 持续集成
- 将DTS集成到CI/CD流程
- 定期运行回归测试
- 监控性能测试结果趋势

---

## 核心概念速记

### 架构组件

| 术语 | 含义 |
|------|------|
| **SUT** | System Under Test - 运行DPDK的被测系统 |
| **TG** | Traffic Generator - 流量生成器（通常用Scapy） |
| **DTS节点** | 任何与DTS通信的主机/服务器 |
| **端口拓扑** | SUT和TG之间的物理网络连接关系 |

### 框架相关

| 术语 | 含义 |
|------|------|
| **TestSuite** | 测试套件基类，所有测试继承此类 |
| **verify()** | 断言方法，失败会记录错误并停止测试 |
| **Poetry** | Python依赖管理工具，确保环境一致性 |
| **EAL** | Environment Abstraction Layer - DPDK环境抽象层 |

### 测试类型

| 类型 | 说明 | 方法前缀 |
|------|------|---------|
| **功能测试** | 验证特性正确性 | `test_` |
| **性能测试** | 测量性能指标 | `test_perf_` |

### 关键方法

| 方法 | 调用时机 | 用途 |
|------|---------|------|
| `set_up_suite()` | 套件开始前（一次） | 全局初始化 |
| `tear_down_suite()` | 套件结束后（一次） | 全局清理 |
| `set_up_test_case()` | 每个测试前 | 单个测试准备 |
| `tear_down_test_case()` | 每个测试后 | 单个测试清理 |

---

## 快速上手Checklist

### 环境准备
- [ ] 安装Python 3.10+
- [ ] 安装Poetry依赖管理工具
- [ ] 准备至少2台机器（或虚拟机）作为SUT和TG
- [ ] 在SUT上安装DPDK编译依赖
- [ ] 在TG上安装Scapy

### 网络配置
- [ ] 确认SUT和TG之间网络连通
- [ ] 记录网卡的PCI地址（使用`lspci`）
- [ ] 配置网卡驱动（如vfio-pci）
- [ ] 配置HugePages

### SSH配置
- [ ] 生成SSH密钥对
- [ ] 配置所有节点的公钥认证
- [ ] 测试SSH无密码登录
- [ ] 确保用户具有无密码sudo权限

### DTS配置
- [ ] 克隆或下载DPDK源码
- [ ] 进入dts目录并运行`poetry install`
- [ ] 复制示例配置文件并修改：
  - [ ] 编写nodes.yaml（节点和网络配置）
  - [ ] 编写test_run.yaml（测试执行配置）

### 验证环境
- [ ] 运行hello_world测试套件验证环境
  ```bash
  ./main.py --test-suite hello_world
  ```
- [ ] 检查测试输出和日志
- [ ] 确认测试通过

### 学习实践
- [ ] 阅读DTS官方文档
- [ ] 阅读hello_world测试套件源码
- [ ] 修改hello_world测试用例，观察变化
- [ ] 尝试编写简单的自定义测试用例
- [ ] 学习其他测试套件（pmd_buffer_scatter等）

### 进阶学习
- [ ] 理解testpmd交互式shell
- [ ] 学习Scapy数据包构造
- [ ] 编写性能测试用例
- [ ] 集成到CI/CD流程

---

## 参考资源

### 官方文档
- DPDK官方文档：https://doc.dpdk.org/
- DTS工具文档：https://doc.dpdk.org/guides/tools/dts.html
- DPDK Getting Started Guide：https://doc.dpdk.org/guides/linux_gsg/

### 源码位置
```
dpdk-source/
├── dts/                    # DTS框架目录
│   ├── framework/          # 框架核心代码
│   ├── tests/              # 测试套件
│   │   ├── TestSuite_hello_world.py
│   │   ├── TestSuite_pmd_buffer_scatter.py
│   │   └── ...
│   ├── conf/               # 配置文件目录
│   │   ├── nodes.example.yaml
│   │   └── test_run.example.yaml
│   └── main.py             # DTS入口脚本
```

### 相关工具
- **Poetry**: https://python-poetry.org/
- **Scapy**: https://scapy.net/
- **Meson**: https://mesonbuild.com/
- **ruff**: https://docs.astral.sh/ruff/

---

## 常见问题排查

### 1. SSH连接失败
```bash
# 检查SSH配置
ssh -v dpdk-user@192.168.1.100

# 确认密钥权限
chmod 600 ~/.ssh/id_rsa
chmod 644 ~/.ssh/id_rsa.pub
```

### 2. DPDK编译失败
```bash
# 检查依赖包
# Ubuntu/Debian:
sudo apt install build-essential meson ninja-build python3-pyelftools libnuma-dev

# CentOS/RHEL:
sudo yum install gcc meson ninja-build python3-pyelftools numactl-devel
```

### 3. HugePages配置问题
```bash
# 查看当前配置
cat /proc/meminfo | grep Huge

# 临时配置2MB hugepages（1024个）
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# 持久化配置（编辑/etc/sysctl.conf）
vm.nr_hugepages = 1024
```

### 4. 网卡绑定失败
```bash
# 查看网卡状态
dpdk-devbind.py --status

# 绑定网卡到vfio-pci
sudo modprobe vfio-pci
sudo dpdk-devbind.py --bind=vfio-pci 0000:00:08.0
```

### 5. Poetry安装问题
```bash
# 清理缓存
poetry cache clear . --all

# 重新安装
poetry install --no-cache
```

---

## 总结

DPDK DTS是一个强大的自动化测试框架，掌握它需要：

1. **理解架构**：分布式三节点模型（DTS、SUT、TG）
2. **配置环境**：Python、Poetry、SSH、HugePages
3. **学习框架**：TestSuite基类、验证机制、生命周期方法
4. **实践编码**：从hello_world开始，逐步编写复杂测试

建议从**hello_world**测试套件开始实践，逐步深入学习其他测试套件的实现方式。

祝学习顺利！
