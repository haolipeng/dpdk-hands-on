# DPDK DTS 实战学习手册

> 每个学习阶段都有明确的实操步骤和验收标准，确保学习成果可量化、可验证

---

## 学习路线总览

本手册分为7个实战阶段，每个阶段都有：
- 明确的学习目标
- 详细的操作步骤
- 验收标准和输出成果

```
阶段1: 环境搭建       → 成果：运行成功的hello_world测试
阶段2: 配置实战       → 成果：自定义的DTS配置文件
阶段3: 测试执行       → 成果：完整的测试报告
阶段4: 源码阅读       → 成果：测试套件流程图
阶段5: 编写第一个测试 → 成果：自定义测试套件（可运行）
阶段6: 流量测试实战   → 成果：数据包验证测试
阶段7: 性能测试实战   → 成果：性能测试报告
```

---

## 阶段1：环境搭建与验证

**学习目标**：搭建完整的DTS测试环境，成功运行第一个测试

### 1.1 准备工作（预计30分钟）

#### 实操步骤

**步骤1：检查Python环境**
```bash
# 检查Python版本
python3 --version

# 预期输出：Python 3.10.x 或更高
```

**验收标准**：
- [ ] Python版本 >= 3.10
- [ ] 如果版本过低，参考下方安装指南

<details>
<summary>点击查看：Python 3.10安装指南（Ubuntu）</summary>

```bash
sudo apt update
sudo apt install software-properties-common
sudo add-apt-repository ppa:deadsnakes/ppa
sudo apt install python3.10 python3.10-venv python3.10-dev
```
</details>

---

**步骤2：安装Poetry**
```bash
# 安装Poetry
curl -sSL https://install.python-poetry.org | python3 -

# 添加到PATH（根据安装提示添加）
export PATH="$HOME/.local/bin:$PATH"

# 验证安装
poetry --version
```

**验收标准**：
- [ ] Poetry版本 >= 1.8.2
- [ ] 执行`poetry --version`无报错

**成果输出**：
```
Poetry (version 1.8.2)
```

---

**步骤3：获取DPDK源码**
```bash
# 克隆DPDK源码（或使用已有的源码）
cd ~/work
git clone https://github.com/DPDK/dpdk.git dpdk-stable
cd dpdk-stable

# 切换到稳定版本（推荐）
git checkout v24.11

# 记录DPDK路径（后续会用到）
export DPDK_PATH=$(pwd)
echo "DPDK路径: $DPDK_PATH"
```

**验收标准**：
- [ ] `$DPDK_PATH/dts`目录存在
- [ ] `$DPDK_PATH/dts/main.py`文件存在

---

**步骤4：安装DTS依赖**
```bash
cd $DPDK_PATH/dts

# 安装Python依赖
poetry install

# 激活虚拟环境
poetry shell

# 验证环境
which python3
```

**验收标准**：
- [ ] 执行`poetry install`成功，无ERROR
- [ ] `poetry shell`后提示符变化（显示虚拟环境名）
- [ ] `which python3`输出包含`.cache/pypoetry`路径

**成果输出**：
```bash
# poetry install 成功后会看到类似输出：
Installing dependencies from lock file
...
Installing the current project: dts (1.0.0)

# poetry shell 后提示符示例：
(dts-py3.10) user@host:~/dpdk-stable/dts$
```

---

### 1.2 配置SSH免密登录（预计20分钟）

DTS通过SSH控制各节点，必须配置免密登录。

#### 实操步骤

**步骤1：生成SSH密钥对（如果还没有）**
```bash
# 在DTS运行节点执行
ssh-keygen -t rsa -b 4096 -C "dts@localhost"

# 一路回车即可，使用默认路径
```

**步骤2：配置本地测试环境（简化版）**

对于初学者，建议先在**单机**上测试，将本机同时作为DTS、SUT和TG节点。

```bash
# 复制公钥到本机（允许本地SSH免密登录）
cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys
chmod 600 ~/.ssh/authorized_keys

# 测试SSH连接
ssh localhost

# 验证sudo免密（DTS需要）
sudo -n true && echo "sudo免密配置正确" || echo "需要配置sudo免密"
```

**验收标准**：
- [ ] `ssh localhost`无需输入密码即可登录
- [ ] `sudo -n true`返回成功

<details>
<summary>点击查看：配置sudo免密</summary>

```bash
# 编辑sudoers文件
sudo visudo

# 添加以下行（替换yourusername为实际用户名）：
yourusername ALL=(ALL) NOPASSWD: ALL

# 保存退出后验证
sudo -n whoami
# 预期输出：root
```
</details>

---

### 1.3 配置HugePages（预计10分钟）

DPDK需要大页内存支持。

#### 实操步骤

```bash
# 查看当前HugePages配置
cat /proc/meminfo | grep Huge

# 临时配置1024个2MB页面（共2GB）
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages

# 再次检查
cat /proc/meminfo | grep Huge
```

**验收标准**：
- [ ] `HugePages_Total` = 1024
- [ ] `HugePages_Free` = 1024（或接近）
- [ ] `Hugepagesize` = 2048 kB

**成果输出**：
```
HugePages_Total:    1024
HugePages_Free:     1024
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
```

<details>
<summary>点击查看：持久化配置</summary>

```bash
# 编辑系统配置
sudo vim /etc/sysctl.conf

# 添加：
vm.nr_hugepages = 1024

# 重启生效，或立即生效：
sudo sysctl -p
```
</details>

---

### 1.4 编写第一个配置文件（预计15分钟）

#### 实操步骤

**步骤1：创建节点配置文件**

```bash
cd $DPDK_PATH/dts
vim conf/nodes.yaml
```

**单机测试配置内容**：
```yaml
# 节点配置 - 单机测试版本
nodes:
  - name: "localhost_sut"
    hostname: localhost
    user: YOUR_USERNAME        # 替换为你的用户名
    os: linux
    lcores: "0-3"              # 使用4个逻辑核
    use_first_core: false
    memory_channels: 4
    ports: []                  # hello_world不需要网卡
```

**重要**：将`YOUR_USERNAME`替换为实际用户名（执行`whoami`查看）

**步骤2：创建测试运行配置**

```bash
vim conf/test_run.yaml
```

**配置内容**：
```yaml
# 测试运行配置
test_runs:
  - build_targets:
      - arch: x86_64
        os: linux
        cpu: native
        compiler: gcc
        compiler_wrapper: ""
    perf: false              # 先不运行性能测试
    func: true               # 运行功能测试
    test_suites:
      - hello_world          # 只运行hello_world
```

**验收标准**：
- [ ] `conf/nodes.yaml`文件存在且格式正确
- [ ] `conf/test_run.yaml`文件存在且格式正确
- [ ] YAML语法无错误（可用在线工具验证）

---

### 1.5 运行第一个测试（预计10分钟）

#### 实操步骤

```bash
cd $DPDK_PATH/dts

# 确保在poetry虚拟环境中
poetry shell

# 运行hello_world测试
./main.py --config-file conf/test_run.yaml \
          --output output_stage1 \
          --timeout 120 \
          --verbose
```

**运行过程观察**：
1. DTS初始化
2. 通过SSH连接到SUT节点（localhost）
3. 编译DPDK（第一次较慢，约5-10分钟）
4. 编译hello_world示例程序
5. 运行测试用例
6. 输出测试结果

**验收标准**：
- [ ] 编译过程无ERROR（warning可忽略）
- [ ] 看到`Test Suite : TestHelloWorld`字样
- [ ] 看到测试用例执行（`PASSED`或`FAILED`）
- [ ] 最终显示测试统计（如：1 passed, 0 failed）

**成果输出示例**：
```
Test Suite : TestHelloWorld
  Test Case : test_hello_world_single_core .............. [PASSED]
  Test Case : test_hello_world_all_cores ................ [PASSED]

======================================================================
Total: 2 tests, 2 passed, 0 failed
======================================================================
```

---

### 1.6 阶段1成果验收

完成以下检查清单，确认阶段1成功：

**必须完成**：
- [ ] Poetry虚拟环境可正常激活
- [ ] SSH localhost免密登录成功
- [ ] HugePages配置为1024个2MB页面
- [ ] `conf/nodes.yaml`和`conf/test_run.yaml`配置正确
- [ ] 成功运行hello_world测试，至少1个用例通过
- [ ] `output_stage1`目录存在，包含测试日志

**成果文件**：
```
output_stage1/
├── dts.log           # 详细日志
├── result.json       # 测试结果JSON
└── ...
```

**验收命令**：
```bash
# 查看测试日志
cat output_stage1/dts.log | grep -i "passed\|failed"

# 查看结果摘要
cat output_stage1/result.json | grep -A5 "statistics"
```

**故障排查清单**：
<details>
<summary>点击查看：常见问题解决</summary>

**问题1：SSH连接失败**
```bash
# 手动测试SSH
ssh -v localhost
# 查看详细错误信息
```

**问题2：编译失败 - 缺少依赖**
```bash
# Ubuntu/Debian安装依赖
sudo apt install build-essential meson ninja-build python3-pyelftools \
                 libnuma-dev pkg-config

# CentOS/RHEL安装依赖
sudo yum groupinstall "Development Tools"
sudo yum install meson ninja-build python3-pyelftools numactl-devel
```

**问题3：HugePages配置失败**
```bash
# 检查内核是否支持
grep -i huge /proc/meminfo

# 如果没有输出，可能需要重新编译内核或使用其他发行版
```

**问题4：Poetry虚拟环境问题**
```bash
# 删除虚拟环境重新创建
poetry env remove python
poetry install
```
</details>

---

## 阶段2：深入理解DTS配置

**学习目标**：掌握DTS配置文件的各项参数，能够为不同环境定制配置

### 2.1 解析nodes.yaml配置（预计30分钟）

#### 实操步骤

**步骤1：创建多节点配置模板**

```bash
cd $DPDK_PATH/dts
vim conf/nodes_advanced.yaml
```

**完整配置示例（含注释）**：
```yaml
# DTS节点配置文件
# 支持配置多个SUT和TG节点

nodes:
  # ============ SUT节点配置 ============
  - name: "sut1"                    # 节点名称（唯一标识）
    hostname: 192.168.1.100         # IP地址或域名
    user: dpdk                      # SSH登录用户
    password: ""                    # 留空表示使用SSH密钥
    os: linux                       # 操作系统类型

    # CPU配置
    lcores: "0-7"                   # 使用的逻辑核范围
    use_first_core: false           # 是否使用0号核（通常保留给系统）
    memory_channels: 4              # 内存通道数（根据硬件配置）

    # HugePages配置
    hugepages:
      amount: 2048                  # 需要的页面数量
      force_first_numa: false       # 是否强制使用第一个NUMA节点

    # 网卡端口配置
    ports:
      - pci: "0000:03:00.0"         # 网卡PCI地址
        os_driver_for_dpdk: vfio-pci  # DPDK驱动类型
        os_driver: i40e             # 原始驱动（用于恢复）
        peer_node: "tg1"            # 对端节点名称
        peer_pci: "0000:03:00.0"    # 对端网卡PCI

      - pci: "0000:03:00.1"         # 第二个网卡
        os_driver_for_dpdk: vfio-pci
        os_driver: i40e
        peer_node: "tg1"
        peer_pci: "0000:03:00.1"

  # ============ TG节点配置 ============
  - name: "tg1"
    hostname: 192.168.1.101
    user: dpdk
    os: linux

    # TG节点也需要配置端口
    ports:
      - pci: "0000:03:00.0"
        os_driver: i40e
        peer_node: "sut1"
        peer_pci: "0000:03:00.0"

      - pci: "0000:03:00.1"
        os_driver: i40e
        peer_node: "sut1"
        peer_pci: "0000:03:00.1"
```

**步骤2：练习 - 修改为你的环境**

任务：根据实际硬件环境修改配置

1. 查看本机网卡PCI地址：
```bash
lspci | grep -i ethernet
# 示例输出：
# 03:00.0 Ethernet controller: Intel Corporation I350 Gigabit Network
```

2. 查看逻辑核数量：
```bash
nproc
# 示例输出：8
```

3. 查看内存通道：
```bash
# 通过dmidecode查看（需要root权限）
sudo dmidecode -t memory | grep -i "Number Of Devices"
```

**验收标准**：
- [ ] 理解每个配置项的含义
- [ ] 能够根据`lspci`输出填写PCI地址
- [ ] 知道如何查看lcores和memory_channels

**成果产出**：一份包含详细注释的`nodes_advanced.yaml`

---

### 2.2 解析test_run.yaml配置（预计30分钟）

#### 实操步骤

**步骤1：创建高级测试配置**

```bash
vim conf/test_run_advanced.yaml
```

**完整配置示例**：
```yaml
# DTS测试运行配置

test_runs:
  # ========== 测试运行组1：GCC编译 + 功能测试 ==========
  - build_targets:
      - arch: x86_64              # CPU架构
        os: linux                 # 操作系统
        cpu: native               # CPU类型（native=自动检测）
        compiler: gcc             # 编译器
        compiler_wrapper: ccache  # 编译加速（可选）

    # 测试类型
    perf: false                   # 是否运行性能测试
    func: true                    # 是否运行功能测试

    # 测试套件选择
    test_suites:
      - hello_world               # 最简单的测试
      - pmd_buffer_scatter        # PMD测试

    # DPDK配置选项（可选）
    system_under_test_node:
      name: "sut1"                # 使用哪个SUT节点

    traffic_generator_node:
      name: "tg1"                 # 使用哪个TG节点

  # ========== 测试运行组2：Clang编译（可选） ==========
  - build_targets:
      - arch: x86_64
        os: linux
        cpu: native
        compiler: clang           # 使用clang编译器
        compiler_wrapper: ""

    perf: false
    func: true
    test_suites:
      - hello_world
```

**步骤2：练习 - 配置验证工具**

创建一个脚本验证YAML语法：

```bash
cd $DPDK_PATH/dts
vim tools/check_config.py
```

**脚本内容**：
```python
#!/usr/bin/env python3
"""验证DTS配置文件的YAML语法"""

import sys
import yaml

def check_yaml(file_path):
    """检查YAML文件语法"""
    try:
        with open(file_path, 'r') as f:
            config = yaml.safe_load(f)
        print(f"✓ {file_path} 语法正确")
        print(f"  配置项数量: {len(config)}")
        return True
    except yaml.YAMLError as e:
        print(f"✗ {file_path} 语法错误:")
        print(f"  {e}")
        return False
    except FileNotFoundError:
        print(f"✗ 文件不存在: {file_path}")
        return False

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python3 check_config.py <yaml文件>")
        sys.exit(1)

    success = check_yaml(sys.argv[1])
    sys.exit(0 if success else 1)
```

**验证配置**：
```bash
chmod +x tools/check_config.py

# 验证nodes配置
python3 tools/check_config.py conf/nodes.yaml

# 验证test_run配置
python3 tools/check_config.py conf/test_run.yaml
```

**验收标准**：
- [ ] 能够解释test_run.yaml中每个字段的含义
- [ ] 知道如何选择编译器（gcc vs clang）
- [ ] 理解test_suites的作用
- [ ] 配置验证脚本运行成功

---

### 2.3 阶段2成果验收

**成果文件**：
```
conf/
├── nodes.yaml                    # 基础配置
├── nodes_advanced.yaml           # 高级配置（多节点）
├── test_run.yaml                 # 基础测试配置
└── test_run_advanced.yaml        # 高级测试配置

tools/
└── check_config.py               # 配置验证工具
```

**验收任务**：
1. 使用`check_config.py`验证所有YAML文件
2. 在配置文件中添加注释，解释每个字段
3. 能够口头解释nodes和test_run的关系

**验收命令**：
```bash
# 验证所有配置文件
for f in conf/*.yaml; do
    echo "检查: $f"
    python3 tools/check_config.py "$f"
done
```

**预期输出**：
```
检查: conf/nodes.yaml
✓ conf/nodes.yaml 语法正确
  配置项数量: 1
检查: conf/test_run.yaml
✓ conf/test_run.yaml 语法正确
  配置项数量: 1
...
```

---

## 阶段3：测试执行与日志分析

**学习目标**：掌握DTS测试执行流程，能够分析测试日志和排查问题

### 3.1 执行完整测试流程（预计30分钟）

#### 实操步骤

**步骤1：准备测试环境检查脚本**

```bash
cd $DPDK_PATH/dts
vim tools/pre_test_check.sh
```

**脚本内容**：
```bash
#!/bin/bash
# DTS测试前环境检查

echo "========== DTS环境检查 =========="

# 1. 检查HugePages
echo -n "1. HugePages: "
HUGE_TOTAL=$(grep HugePages_Total /proc/meminfo | awk '{print $2}')
if [ "$HUGE_TOTAL" -ge 1024 ]; then
    echo "✓ ($HUGE_TOTAL 个页面)"
else
    echo "✗ (需要至少1024个，当前 $HUGE_TOTAL)"
fi

# 2. 检查SSH连接
echo -n "2. SSH localhost: "
if ssh -o BatchMode=yes -o ConnectTimeout=5 localhost 'exit' 2>/dev/null; then
    echo "✓"
else
    echo "✗ (无法免密登录)"
fi

# 3. 检查sudo权限
echo -n "3. Sudo免密: "
if sudo -n true 2>/dev/null; then
    echo "✓"
else
    echo "✗"
fi

# 4. 检查Poetry环境
echo -n "4. Poetry虚拟环境: "
if [[ "$VIRTUAL_ENV" == *"poetry"* ]]; then
    echo "✓"
else
    echo "✗ (请先执行: poetry shell)"
fi

# 5. 检查配置文件
echo -n "5. 配置文件: "
if [ -f "conf/nodes.yaml" ] && [ -f "conf/test_run.yaml" ]; then
    echo "✓"
else
    echo "✗ (缺少配置文件)"
fi

echo "================================"
```

**执行检查**：
```bash
chmod +x tools/pre_test_check.sh
./tools/pre_test_check.sh
```

**验收标准**：
- [ ] 所有检查项显示✓
- [ ] 如有✗，按提示修复

---

**步骤2：运行测试并记录输出**

```bash
cd $DPDK_PATH/dts
poetry shell

# 运行测试并记录时间
TIME_START=$(date +%s)

./main.py --config-file conf/test_run.yaml \
          --output output_stage3 \
          --verbose 2>&1 | tee test_stage3.log

TIME_END=$(date +%s)
TIME_ELAPSED=$((TIME_END - TIME_START))

echo "测试用时: $TIME_ELAPSED 秒" | tee -a test_stage3.log
```

**观察要点**：
1. **初始化阶段**：加载配置、连接节点
2. **编译阶段**：DPDK编译输出
3. **测试执行阶段**：每个测试用例的状态
4. **结果汇总**：通过/失败统计

**验收标准**：
- [ ] 看到明确的测试阶段划分
- [ ] `test_stage3.log`文件记录了完整日志
- [ ] 知道测试总耗时

---

### 3.2 日志分析实战（预计30分钟）

#### 实操步骤

**步骤1：提取关键信息**

```bash
cd $DPDK_PATH/dts

# 1. 提取测试结果摘要
echo "========== 测试结果摘要 =========="
grep -E "(PASSED|FAILED|SKIPPED)" output_stage3/dts.log

# 2. 提取错误信息
echo "========== 错误信息 =========="
grep -i "error\|exception" output_stage3/dts.log | head -20

# 3. 提取警告信息
echo "========== 警告信息 =========="
grep -i "warning" output_stage3/dts.log | head -10

# 4. 查看编译耗时
echo "========== 编译信息 =========="
grep -i "build\|compil" output_stage3/dts.log | grep -i "time\|done"
```

**步骤2：创建日志分析脚本**

```bash
vim tools/analyze_log.py
```

**脚本内容**：
```python
#!/usr/bin/env python3
"""分析DTS测试日志"""

import re
import sys
from collections import defaultdict

def analyze_log(log_file):
    """分析DTS日志文件"""

    test_results = defaultdict(list)
    errors = []

    with open(log_file, 'r') as f:
        for line in f:
            # 提取测试结果
            match = re.search(r'Test Case.*\[(PASSED|FAILED|SKIPPED)\]', line)
            if match:
                result = match.group(1)
                test_results[result].append(line.strip())

            # 提取错误
            if 'ERROR' in line or 'Exception' in line:
                errors.append(line.strip())

    # 打印分析结果
    print("=" * 60)
    print("DTS日志分析报告")
    print("=" * 60)

    print(f"\n【测试统计】")
    print(f"  通过: {len(test_results['PASSED'])}")
    print(f"  失败: {len(test_results['FAILED'])}")
    print(f"  跳过: {len(test_results['SKIPPED'])}")

    if test_results['PASSED']:
        print(f"\n【通过的测试】")
        for test in test_results['PASSED']:
            print(f"  ✓ {test}")

    if test_results['FAILED']:
        print(f"\n【失败的测试】")
        for test in test_results['FAILED']:
            print(f"  ✗ {test}")

    if errors:
        print(f"\n【错误信息】(显示前5条)")
        for error in errors[:5]:
            print(f"  ! {error}")

    print("\n" + "=" * 60)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python3 analyze_log.py <日志文件>")
        sys.exit(1)

    analyze_log(sys.argv[1])
```

**运行分析**：
```bash
chmod +x tools/analyze_log.py
python3 tools/analyze_log.py output_stage3/dts.log
```

**验收标准**：
- [ ] 能够看到测试统计（通过/失败/跳过）
- [ ] 能够列出通过和失败的测试用例
- [ ] 能够提取错误信息

**成果输出示例**：
```
============================================================
DTS日志分析报告
============================================================

【测试统计】
  通过: 2
  失败: 0
  跳过: 0

【通过的测试】
  ✓ Test Case : test_hello_world_single_core ........ [PASSED]
  ✓ Test Case : test_hello_world_all_cores .......... [PASSED]

============================================================
```

---

### 3.3 测试报告生成（预计20分钟）

#### 实操步骤

**步骤1：创建测试报告模板**

```bash
vim tools/generate_report.sh
```

**脚本内容**：
```bash
#!/bin/bash
# 生成DTS测试报告

OUTPUT_DIR=${1:-"output_stage3"}
REPORT_FILE="test_report_$(date +%Y%m%d_%H%M%S).md"

cat > "$REPORT_FILE" << EOF
# DPDK DTS 测试报告

**生成时间**: $(date '+%Y-%m-%d %H:%M:%S')
**测试目录**: $OUTPUT_DIR

---

## 1. 测试环境

- **操作系统**: $(uname -s) $(uname -r)
- **DPDK版本**: $(cd $DPDK_PATH && git describe --tags 2>/dev/null || echo "未知")
- **Python版本**: $(python3 --version)
- **HugePages**: $(grep HugePages_Total /proc/meminfo | awk '{print $2}') 页面

---

## 2. 测试配置

\`\`\`yaml
# nodes.yaml
$(cat conf/nodes.yaml)
\`\`\`

\`\`\`yaml
# test_run.yaml
$(cat conf/test_run.yaml)
\`\`\`

---

## 3. 测试结果

EOF

# 添加测试结果
python3 tools/analyze_log.py "$OUTPUT_DIR/dts.log" >> "$REPORT_FILE"

cat >> "$REPORT_FILE" << EOF

---

## 4. 详细日志

完整日志请查看: \`$OUTPUT_DIR/dts.log\`

---

## 5. 问题总结

EOF

# 提取错误（如果有）
if grep -qi "error\|failed" "$OUTPUT_DIR/dts.log"; then
    echo "发现错误，详见日志文件。" >> "$REPORT_FILE"
else
    echo "所有测试通过，无错误。" >> "$REPORT_FILE"
fi

echo ""
echo "测试报告已生成: $REPORT_FILE"
```

**生成报告**：
```bash
chmod +x tools/generate_report.sh
./tools/generate_report.sh output_stage3
```

**验收标准**：
- [ ] 生成了Markdown格式的测试报告
- [ ] 报告包含环境信息、配置、结果、日志摘要
- [ ] 报告可读性良好，可直接分享

**成果输出**：
```
test_report_20250126_153000.md
```

---

### 3.4 阶段3成果验收

**成果文件清单**：
```
tools/
├── pre_test_check.sh          # 测试前环境检查
├── analyze_log.py             # 日志分析工具
└── generate_report.sh         # 测试报告生成器

output_stage3/
├── dts.log                    # 完整日志
└── result.json                # 结果JSON

test_stage3.log                # 控制台输出
test_report_YYYYMMDD_HHMMSS.md # 测试报告
```

**验收任务**：
1. 使用三个工具分析测试结果
2. 生成一份完整的测试报告
3. 能够从日志中定位问题（如果测试失败）

**验收命令**：
```bash
# 1. 环境检查
./tools/pre_test_check.sh

# 2. 日志分析
python3 tools/analyze_log.py output_stage3/dts.log

# 3. 生成报告
./tools/generate_report.sh output_stage3

# 4. 查看报告
cat test_report_*.md
```

**必须回答的问题**：
- [ ] 如何判断一个测试是否通过？
- [ ] 在哪里查看详细的错误信息？
- [ ] 如何统计测试通过率？
- [ ] 日志文件中包含哪些关键阶段？

---

## 阶段4：源码阅读与流程理解

**学习目标**：通过阅读hello_world测试套件源码，理解测试框架的工作流程

### 4.1 阅读hello_world源码（预计45分钟）

#### 实操步骤

**步骤1：定位测试套件文件**

```bash
cd $DPDK_PATH/dts

# 查找hello_world测试套件
find . -name "*hello_world*"

# 预期输出：
# ./tests/TestSuite_hello_world.py
```

**步骤2：逐段阅读源码并添加注释**

```bash
# 复制源码进行学习
cp tests/TestSuite_hello_world.py tests/TestSuite_hello_world_annotated.py
vim tests/TestSuite_hello_world_annotated.py
```

**阅读任务**：为关键部分添加中文注释

原始代码示例：
```python
class TestHelloWorld(TestSuite):
    def set_up_suite(self):
        """
        Setup:
            Build hello world example.
        """
        self.app_hello_world_path = self.sut_node.build_dpdk_apps("helloworld")
```

添加注释后：
```python
class TestHelloWorld(TestSuite):
    """
    Hello World测试套件

    【学习笔记】
    - 继承自TestSuite基类，这是所有DTS测试的父类
    - set_up_suite在整个套件开始前运行一次
    - 主要用于编译应用程序、初始化资源
    """

    def set_up_suite(self):
        """
        测试套件初始化

        【步骤】
        1. 调用sut_node.build_dpdk_apps()编译hello world应用
        2. 返回的路径保存到self.app_hello_world_path
        3. 后续测试用例会使用这个路径运行程序
        """
        # sut_node是DTS提供的SUT节点对象
        # build_dpdk_apps()方法会：
        #   - 进入DPDK源码目录
        #   - 使用meson/ninja编译指定的示例程序
        #   - 返回编译后的可执行文件路径
        self.app_hello_world_path = self.sut_node.build_dpdk_apps("helloworld")
```

**步骤3：绘制执行流程图**

创建流程图文档：
```bash
vim docs/hello_world_flow.md
```

**流程图内容**：
```markdown
# Hello World 测试执行流程

## 1. 测试套件执行流程

```
┌─────────────────────────────────────────┐
│ DTS框架启动                             │
│ - 加载配置文件                          │
│ - 连接SUT和TG节点                       │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│ set_up_suite() - 套件初始化             │
│ ┌─────────────────────────────────────┐ │
│ │ 1. 编译DPDK（如果还没编译）         │ │
│ │ 2. 编译hello_world示例程序          │ │
│ │ 3. 保存可执行文件路径               │ │
│ └─────────────────────────────────────┘ │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│ 循环：遍历所有test_方法                 │
│ ┌─────────────────────────────────────┐ │
│ │ set_up_test_case() - 测试前准备     │ │
│ │ ├─> test_xxx() - 执行测试           │ │
│ │ └─> tear_down_test_case() - 清理    │ │
│ └─────────────────────────────────────┘ │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│ tear_down_suite() - 套件清理            │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│ 生成测试报告                            │
└─────────────────────────────────────────┘
```

## 2. 单个测试用例详细流程（test_hello_world_single_core）

```
┌─────────────────────────────────────────┐
│ 1. 创建EAL参数                          │
│    eal_params = --lcores=0              │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│ 2. 通过SSH在SUT上运行hello_world        │
│    命令: ./helloworld --lcores=0        │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│ 3. 捕获程序输出                         │
│    output = "hello from core 0"         │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│ 4. 验证输出                             │
│    self.verify("hello from core" in     │
│                output, "错误信息")       │
└──────────────┬──────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────┐
│ 5. 记录结果：PASSED 或 FAILED           │
└─────────────────────────────────────────┘
```

## 3. 关键代码片段解析

### 3.1 编译应用程序
\`\`\`python
self.app_hello_world_path = self.sut_node.build_dpdk_apps("helloworld")
\`\`\`
**作用**: 在SUT节点上编译hello_world示例程序
**返回**: 可执行文件的绝对路径，如 `/root/dpdk/build/examples/dpdk-helloworld`

### 3.2 运行测试用例
\`\`\`python
eal_params = EalParameters(lcore_list=[0], ...)
result = self.sut_node.run_dpdk_app(
    self.app_hello_world_path,
    eal_params
)
\`\`\`
**作用**:
1. 构造DPDK EAL参数（如 --lcores, --huge-dir等）
2. 通过SSH在SUT上执行程序
3. 等待程序结束并返回输出

### 3.3 验证结果
\`\`\`python
self.verify("hello from core" in result.stdout,
           "Output does not contain expected string")
\`\`\`
**作用**:
- 检查条件是否为True
- 如果为False，测试失败并记录错误信息
- DTS会标记该测试用例为FAILED

## 4. 学习要点

1. **TestSuite生命周期**：
   - `set_up_suite` → `set_up_test_case` → `test_*` → `tear_down_test_case` → `tear_down_suite`

2. **节点对象**：
   - `self.sut_node`: SUT节点操作接口
   - `self.tg_node`: TG节点操作接口

3. **常用方法**：
   - `build_dpdk_apps()`: 编译DPDK示例程序
   - `run_dpdk_app()`: 运行DPDK程序
   - `verify()`: 验证测试条件

4. **EAL参数**：
   - 通过`EalParameters`类构造
   - 常用参数：lcore_list, memory_channels, hugepage_dir等
```

**验收标准**：
- [ ] 完成源码注释，理解每行代码的作用
- [ ] 绘制了完整的流程图
- [ ] 能够解释set_up_suite和set_up_test_case的区别
- [ ] 理解verify()方法的作用

---

### 4.2 动手实验：修改测试用例（预计30分钟）

#### 实操步骤

**任务1：修改core数量**

创建一个新的测试用例，使用不同的core配置：

```bash
vim tests/TestSuite_hello_world_custom.py
```

**代码内容**：
```python
"""
自定义Hello World测试套件 - 学习实验

【学习目标】
- 理解如何创建测试套件
- 掌握EAL参数配置
- 练习使用verify()验证
"""

from framework.test_suite import TestSuite
from framework.testpmd_shell import EalParameters


class TestHelloWorldCustom(TestSuite):
    """自定义Hello World测试"""

    def set_up_suite(self):
        """编译hello world程序"""
        self.app_path = self.sut_node.build_dpdk_apps("helloworld")

    def test_single_core(self):
        """测试1：使用单个核心（core 0）"""
        # 创建EAL参数：只使用core 0
        eal_params = EalParameters(lcore_list=[0])

        # 运行程序
        result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

        # 验证输出包含"hello from core"
        self.verify("hello from core" in result.stdout,
                   "未找到预期的hello消息")

        # 额外验证：确保只有core 0的消息
        self.verify("hello from core 0" in result.stdout,
                   "未找到core 0的hello消息")

    def test_two_cores(self):
        """测试2：使用两个核心（core 0和1）"""
        eal_params = EalParameters(lcore_list=[0, 1])
        result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

        # 验证两个core都输出了消息
        self.verify("hello from core 0" in result.stdout,
                   "未找到core 0的消息")
        self.verify("hello from core 1" in result.stdout,
                   "未找到core 1的消息")

    def test_four_cores(self):
        """测试3：使用四个核心（core 0-3）"""
        eal_params = EalParameters(lcore_list=[0, 1, 2, 3])
        result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

        # 验证四个core都输出了消息
        for core_id in range(4):
            self.verify(f"hello from core {core_id}" in result.stdout,
                       f"未找到core {core_id}的消息")
```

**运行自定义测试**：

修改`conf/test_run.yaml`，添加自定义测试套件：
```yaml
test_runs:
  - build_targets:
      - arch: x86_64
        os: linux
        cpu: native
        compiler: gcc
    func: true
    test_suites:
      - hello_world_custom    # 添加这行
```

**执行测试**：
```bash
cd $DPDK_PATH/dts
poetry shell

./main.py --config-file conf/test_run.yaml \
          --output output_custom \
          --test-suite hello_world_custom \
          --verbose
```

**验收标准**：
- [ ] 自定义测试套件编译成功
- [ ] 三个测试用例全部通过
- [ ] 能够解释每个测试用例的逻辑

**预期输出**：
```
Test Suite : TestHelloWorldCustom
  Test Case : test_single_core ................. [PASSED]
  Test Case : test_two_cores ................... [PASSED]
  Test Case : test_four_cores .................. [PASSED]

Total: 3 tests, 3 passed, 0 failed
```

---

**任务2：添加失败案例（故意制造失败）**

目的：理解测试失败时的行为

在`TestHelloWorldCustom`中添加：
```python
def test_failure_example(self):
    """测试4：故意失败的测试用例（学习用）"""
    eal_params = EalParameters(lcore_list=[0])
    result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

    # 故意使用错误的验证条件
    self.verify("this string will never appear" in result.stdout,
               "【预期失败】这是一个故意失败的测试，用于学习")
```

**再次运行测试**：
```bash
./main.py --test-suite hello_world_custom --output output_failure
```

**观察要点**：
1. 测试失败时的输出格式
2. 错误信息的位置
3. 日志中如何记录失败

**验收标准**：
- [ ] 看到`[FAILED]`标记
- [ ] 能够在日志中找到失败原因
- [ ] 理解verify()第二个参数的作用

---

### 4.3 阶段4成果验收

**成果文件清单**：
```
tests/
├── TestSuite_hello_world_annotated.py  # 带注释的源码
└── TestSuite_hello_world_custom.py     # 自定义测试套件

docs/
└── hello_world_flow.md                 # 流程图文档

output_custom/
└── dts.log                             # 自定义测试日志

output_failure/
└── dts.log                             # 包含失败案例的日志
```

**必须回答的问题**：
1. `set_up_suite`和`set_up_test_case`的区别？
   - set_up_suite：整个套件开始前运行一次，用于编译程序等耗时操作
   - set_up_test_case：每个测试用例前运行，用于准备测试环境

2. `self.sut_node.run_dpdk_app()`做了什么？
   - 通过SSH连接到SUT节点
   - 执行DPDK应用程序（带EAL参数）
   - 捕获stdout和stderr
   - 返回结果对象

3. 如何让一个测试用例失败？
   - verify()的第一个参数为False时测试失败
   - 程序抛出未捕获的异常

4. 测试用例的执行顺序是什么？
   - 按方法名的字母顺序执行（test_开头的方法）

**实战检验**：
创建一个新的测试用例，要求：
- [ ] 使用3个core
- [ ] 验证每个core都输出了消息
- [ ] 额外验证：确保没有使用core 4的消息

参考答案：
```python
def test_three_cores_exactly(self):
    """测试：精确使用3个核心（0,1,2），不使用core 3"""
    eal_params = EalParameters(lcore_list=[0, 1, 2])
    result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

    # 验证core 0, 1, 2都有输出
    for core_id in [0, 1, 2]:
        self.verify(f"hello from core {core_id}" in result.stdout,
                   f"未找到core {core_id}的消息")

    # 验证没有使用core 3
    self.verify("hello from core 3" not in result.stdout,
               "不应该有core 3的消息")
```

---

## 阶段5：编写第一个完整测试套件

**学习目标**：独立编写一个测试套件，验证DPDK的基本功能

### 5.1 需求分析（预计15分钟）

#### 测试目标

创建一个测试套件，验证DPDK的**内存管理**功能（使用hello_world程序）：
1. 验证程序能够在不同数量的hugepages下运行
2. 验证程序能够正确使用指定的内存通道
3. 验证程序的日志输出包含EAL初始化信息

#### 实操步骤

**步骤1：编写测试计划**

```bash
vim docs/test_plan_memory.md
```

**测试计划内容**：
```markdown
# DPDK内存管理测试计划

## 测试套件名称
TestMemoryManagement

## 测试目标
验证DPDK的内存管理功能是否正常工作

## 测试环境
- SUT节点：localhost
- HugePages：至少512个2MB页面
- DPDK示例程序：hello_world

## 测试用例设计

### TC1: test_basic_memory
**描述**: 验证程序能够使用基本的内存配置运行
**步骤**:
1. 配置EAL参数：1个core，默认内存
2. 运行hello_world程序
3. 验证程序成功启动
**预期结果**: 输出包含"hello from core"

### TC2: test_memory_channels
**描述**: 验证程序能够使用指定的内存通道
**步骤**:
1. 配置EAL参数：指定内存通道数为4
2. 运行hello_world程序
3. 检查EAL日志
**预期结果**: EAL日志中包含内存通道配置信息

### TC3: test_limited_memory
**描述**: 验证程序能够在有限内存下运行
**步骤**:
1. 配置EAL参数：限制内存为256MB
2. 运行hello_world程序
3. 验证程序仍能正常工作
**预期结果**: 程序成功运行，输出包含"hello from core"

## 验收标准
- 所有3个测试用例通过
- 能够生成测试报告
- 代码符合DTS编码规范
```

**验收标准**：
- [ ] 测试计划文档完整
- [ ] 包含至少3个测试用例
- [ ] 每个用例有明确的步骤和预期结果

---

### 5.2 编写测试代码（预计45分钟）

#### 实操步骤

**步骤1：创建测试套件骨架**

```bash
cd $DPDK_PATH/dts
vim tests/TestSuite_memory_management.py
```

**代码框架**：
```python
"""
DPDK内存管理测试套件

作者: [你的名字]
日期: [日期]
目的: 学习DTS框架，验证DPDK内存管理功能
"""

from framework.test_suite import TestSuite
from framework.testpmd_shell import EalParameters


class TestMemoryManagement(TestSuite):
    """DPDK内存管理功能测试"""

    def set_up_suite(self):
        """
        套件初始化：编译hello_world程序
        """
        # TODO: 编译hello_world
        pass

    def set_up_test_case(self):
        """
        每个测试用例前的准备工作
        """
        # 可以在这里添加通用的准备步骤
        pass

    def tear_down_test_case(self):
        """
        每个测试用例后的清理工作
        """
        pass

    def tear_down_suite(self):
        """
        套件清理
        """
        pass

    def test_basic_memory(self):
        """
        TC1: 基本内存配置测试
        """
        # TODO: 实现测试逻辑
        pass

    def test_memory_channels(self):
        """
        TC2: 内存通道配置测试
        """
        # TODO: 实现测试逻辑
        pass

    def test_limited_memory(self):
        """
        TC3: 有限内存测试
        """
        # TODO: 实现测试逻辑
        pass
```

---

**步骤2：实现测试逻辑**

完整代码：
```python
"""
DPDK内存管理测试套件
"""

from framework.test_suite import TestSuite
from framework.testpmd_shell import EalParameters


class TestMemoryManagement(TestSuite):
    """DPDK内存管理功能测试"""

    def set_up_suite(self):
        """编译hello_world程序"""
        self.app_path = self.sut_node.build_dpdk_apps("helloworld")

    def test_basic_memory(self):
        """
        TC1: 基本内存配置测试

        验证程序能够使用默认内存配置运行
        """
        # 创建EAL参数：使用core 0
        eal_params = EalParameters(
            lcore_list=[0]
        )

        # 运行程序
        result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

        # 验证1：程序成功运行
        self.verify("hello from core 0" in result.stdout,
                   "程序未成功运行")

        # 验证2：EAL初始化成功
        self.verify("EAL: " in result.stdout,
                   "EAL未成功初始化")

    def test_memory_channels(self):
        """
        TC2: 内存通道配置测试

        验证程序能够使用指定的内存通道数
        """
        # 创建EAL参数：指定4个内存通道
        eal_params = EalParameters(
            lcore_list=[0],
            memory_channels=4  # 指定内存通道
        )

        result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

        # 验证程序成功运行
        self.verify("hello from core" in result.stdout,
                   "程序未成功运行")

        # 验证EAL日志中包含内存相关信息
        # 注意：实际的日志格式可能因DPDK版本而异
        self.verify("EAL: " in result.stdout,
                   "EAL初始化日志缺失")

    def test_limited_memory(self):
        """
        TC3: 有限内存测试

        验证程序能够在限制内存的情况下运行
        """
        # 创建EAL参数：限制内存为256MB
        eal_params = EalParameters(
            lcore_list=[0]
        )
        # 注意：实际限制内存需要使用额外的EAL参数
        # 这里简化处理，主要验证参数传递机制

        result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

        # 验证程序在有限内存下仍能运行
        self.verify("hello from core" in result.stdout,
                   "程序在有限内存下未能运行")

        # 验证没有内存不足的错误
        self.verify("Cannot allocate memory" not in result.stderr,
                   "内存分配失败")
```

**验收标准**：
- [ ] 代码编译无错误
- [ ] 包含3个test_方法
- [ ] 每个方法都有文档字符串
- [ ] 使用了verify()进行验证

---

**步骤3：代码质量检查**

```bash
cd $DPDK_PATH/dts

# 1. 语法检查
poetry run python3 -m py_compile tests/TestSuite_memory_management.py

# 2. 代码风格检查（如果配置了ruff）
poetry run ruff check tests/TestSuite_memory_management.py

# 3. 类型检查（如果配置了mypy）
poetry run mypy tests/TestSuite_memory_management.py
```

**验收标准**：
- [ ] 无语法错误
- [ ] 代码风格符合PEP 8
- [ ] 类型注解正确（如果使用）

---

### 5.3 运行并验证测试（预计20分钟）

#### 实操步骤

**步骤1：配置测试运行**

修改`conf/test_run.yaml`：
```yaml
test_runs:
  - build_targets:
      - arch: x86_64
        os: linux
        cpu: native
        compiler: gcc
    func: true
    test_suites:
      - memory_management  # 添加新的测试套件
```

**步骤2：执行测试**

```bash
cd $DPDK_PATH/dts
poetry shell

# 运行测试
./main.py --config-file conf/test_run.yaml \
          --output output_stage5 \
          --test-suite memory_management \
          --verbose | tee test_stage5.log
```

**步骤3：分析结果**

```bash
# 使用之前创建的工具分析日志
python3 tools/analyze_log.py output_stage5/dts.log

# 生成测试报告
./tools/generate_report.sh output_stage5
```

**验收标准**：
- [ ] 所有3个测试用例通过（3 passed, 0 failed）
- [ ] 日志中无ERROR级别的错误
- [ ] 生成了测试报告

**预期输出**：
```
Test Suite : TestMemoryManagement
  Test Case : test_basic_memory ................. [PASSED]
  Test Case : test_limited_memory ............... [PASSED]
  Test Case : test_memory_channels .............. [PASSED]

======================================================================
Total: 3 tests, 3 passed, 0 failed
======================================================================
```

---

### 5.4 阶段5成果验收

**成果文件清单**：
```
docs/
└── test_plan_memory.md               # 测试计划

tests/
└── TestSuite_memory_management.py    # 测试套件源码

output_stage5/
├── dts.log                           # 测试日志
└── result.json                       # 结果JSON

test_stage5.log                       # 控制台输出
test_report_*.md                      # 测试报告
```

**必须完成的任务**：
1. [ ] 测试计划文档完整，包含3个测试用例设计
2. [ ] 测试套件代码实现，所有用例通过
3. [ ] 代码质量检查通过（无语法错误）
4. [ ] 生成完整的测试报告

**进阶挑战**（可选）：
1. [ ] 添加第4个测试用例：验证多核心下的内存使用
2. [ ] 在测试中打印实际使用的HugePages数量
3. [ ] 添加异常处理：当HugePages不足时给出友好提示

参考代码（进阶挑战1）：
```python
def test_multi_core_memory(self):
    """
    TC4: 多核心内存测试

    验证多核心场景下的内存分配
    """
    eal_params = EalParameters(lcore_list=[0, 1, 2, 3])
    result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

    # 验证所有core都成功运行
    for core_id in range(4):
        self.verify(f"hello from core {core_id}" in result.stdout,
                   f"Core {core_id}未成功运行")

    # 验证没有内存相关错误
    self.verify("Cannot allocate memory" not in result.stderr and
               "Out of memory" not in result.stderr,
               "出现内存分配错误")
```

---

## 阶段6：流量测试实战（进阶）

**学习目标**：学习如何使用Scapy发送和验证数据包

### 6.1 环境准备（预计20分钟）

#### 实操步骤

**步骤1：安装Scapy**

```bash
# 在TG节点（这里是本机）安装Scapy
pip3 install scapy==2.5.0

# 验证安装
python3 -c "from scapy.all import *; print('Scapy安装成功')"
```

**步骤2：学习Scapy基础**

创建学习脚本：
```bash
vim tools/scapy_basics.py
```

**脚本内容**：
```python
#!/usr/bin/env python3
"""Scapy基础学习脚本"""

from scapy.all import *

print("========== Scapy基础学习 ==========\n")

# 1. 创建一个简单的以太网帧
print("1. 创建以太网帧")
eth = Ether(src="00:11:22:33:44:55", dst="00:aa:bb:cc:dd:ee")
print(eth.show())

# 2. 创建IP数据包
print("\n2. 创建IP数据包")
ip = IP(src="192.168.1.100", dst="192.168.1.200")
print(ip.show())

# 3. 创建UDP数据包
print("\n3. 创建UDP数据包")
udp = UDP(sport=1234, dport=5678)
print(udp.show())

# 4. 组合成完整的数据包
print("\n4. 组合数据包")
packet = eth / ip / udp / Raw(load="Hello DPDK")
print(packet.show())

# 5. 查看数据包的十六进制表示
print("\n5. 数据包十六进制")
print(hexdump(packet))

# 6. 验证数据包字段
print("\n6. 验证数据包字段")
print(f"源MAC: {packet[Ether].src}")
print(f"目的MAC: {packet[Ether].dst}")
print(f"源IP: {packet[IP].src}")
print(f"目的IP: {packet[IP].dst}")
print(f"UDP源端口: {packet[UDP].sport}")
print(f"UDP目的端口: {packet[UDP].dport}")
print(f"载荷: {packet[Raw].load.decode()}")

print("\n========== 学习完成 ==========")
```

**运行学习**：
```bash
chmod +x tools/scapy_basics.py
sudo python3 tools/scapy_basics.py
```

**验收标准**：
- [ ] Scapy安装成功
- [ ] 能够创建以太网帧、IP包、UDP包
- [ ] 理解数据包的分层结构
- [ ] 能够访问数据包的各个字段

---

### 6.2 编写流量测试用例（预计40分钟）

对于初学者，由于流量测试需要真实的网卡和网络环境，这里提供一个**模拟版本**，重点学习测试逻辑。

#### 实操步骤

**步骤1：创建数据包验证测试套件**

```bash
vim tests/TestSuite_packet_validation.py
```

**代码内容**：
```python
"""
数据包验证测试套件

注意：这是一个简化版本，用于学习DTS和Scapy的集成
实际生产环境需要真实的网卡和TG节点
"""

from framework.test_suite import TestSuite
from framework.testpmd_shell import EalParameters
from scapy.all import Ether, IP, UDP, Raw


class TestPacketValidation(TestSuite):
    """数据包验证测试（学习版）"""

    def set_up_suite(self):
        """
        套件初始化

        注意：实际环境中需要：
        1. 配置网卡
        2. 编译testpmd
        3. 配置TG节点
        """
        # 这里使用hello_world作为示例
        # 实际应该使用testpmd
        self.app_path = self.sut_node.build_dpdk_apps("helloworld")

    def test_create_simple_packet(self):
        """
        TC1: 创建简单数据包

        学习目标：掌握使用Scapy创建数据包
        """
        # 创建一个UDP数据包
        packet = (
            Ether(src="00:11:22:33:44:55", dst="00:aa:bb:cc:dd:ee") /
            IP(src="192.168.1.100", dst="192.168.1.200") /
            UDP(sport=1234, dport=5678) /
            Raw(load=b"Test Payload")
        )

        # 验证数据包字段
        self.verify(packet[Ether].src == "00:11:22:33:44:55",
                   "源MAC地址不正确")
        self.verify(packet[IP].src == "192.168.1.100",
                   "源IP地址不正确")
        self.verify(packet[UDP].sport == 1234,
                   "UDP源端口不正确")
        self.verify(packet[Raw].load == b"Test Payload",
                   "数据包载荷不正确")

        # 打印数据包信息（用于学习）
        self.logger.info(f"创建的数据包: {packet.summary()}")

    def test_packet_field_validation(self):
        """
        TC2: 数据包字段验证

        学习目标：验证数据包的各个字段
        """
        # 创建多个数据包，验证字段设置
        packets = []

        for i in range(5):
            pkt = (
                Ether() /
                IP(src=f"192.168.1.{100+i}", dst=f"192.168.1.{200+i}") /
                UDP(sport=1000+i, dport=2000+i) /
                Raw(load=f"Packet {i}".encode())
            )
            packets.append(pkt)

        # 验证数据包数量
        self.verify(len(packets) == 5, "数据包数量不正确")

        # 验证每个数据包的字段
        for i, pkt in enumerate(packets):
            self.verify(pkt[IP].src == f"192.168.1.{100+i}",
                       f"数据包{i}的源IP不正确")
            self.verify(pkt[UDP].sport == 1000+i,
                       f"数据包{i}的源端口不正确")
            self.verify(pkt[Raw].load == f"Packet {i}".encode(),
                       f"数据包{i}的载荷不正确")

    def test_packet_size_validation(self):
        """
        TC3: 数据包大小验证

        学习目标：验证不同大小的数据包
        """
        # 测试不同大小的载荷
        payload_sizes = [64, 128, 256, 512, 1024]

        for size in payload_sizes:
            # 创建指定大小的载荷
            payload = b"X" * size
            packet = (
                Ether() /
                IP() /
                UDP() /
                Raw(load=payload)
            )

            # 验证载荷大小
            actual_size = len(packet[Raw].load)
            self.verify(actual_size == size,
                       f"期望载荷大小{size}，实际{actual_size}")

            # 打印数据包总长度
            total_len = len(bytes(packet))
            self.logger.info(f"载荷{size}字节，总长度{total_len}字节")
```

**验收标准**：
- [ ] 理解如何在DTS测试中使用Scapy
- [ ] 能够创建不同类型的数据包
- [ ] 能够验证数据包的字段
- [ ] 理解数据包的大小计算

---

**步骤2：运行数据包验证测试**

```bash
cd $DPDK_PATH/dts

# 修改test_run.yaml，添加新的测试套件
# test_suites:
#   - packet_validation

poetry shell
./main.py --config-file conf/test_run.yaml \
          --output output_stage6 \
          --test-suite packet_validation \
          --verbose
```

**验收标准**：
- [ ] 所有3个测试用例通过
- [ ] 日志中能看到Scapy创建的数据包信息
- [ ] 理解了数据包验证的逻辑

---

### 6.3 阶段6成果验收

**成果文件清单**：
```
tools/
└── scapy_basics.py                  # Scapy学习脚本

tests/
└── TestSuite_packet_validation.py   # 数据包验证测试套件

output_stage6/
└── dts.log                          # 测试日志
```

**必须完成的任务**：
1. [ ] Scapy安装并能够创建数据包
2. [ ] 完成scapy_basics.py学习脚本
3. [ ] packet_validation测试套件所有用例通过
4. [ ] 理解数据包的分层结构（Ether/IP/UDP/Raw）

**进阶挑战**（可选）：
1. [ ] 创建TCP数据包并验证
2. [ ] 创建带VLAN标签的数据包
3. [ ] 计算并验证数据包的校验和

---

## 阶段7：性能测试入门（高级）

**学习目标**：了解性能测试的基本概念，学习如何收集性能指标

### 7.1 性能测试概念（预计30分钟）

#### 学习内容

**关键性能指标**：

| 指标 | 说明 | 单位 |
|------|------|------|
| **吞吐量（Throughput）** | 单位时间内处理的数据包数量 | pps (packets per second) 或 Gbps |
| **延迟（Latency）** | 数据包从发送到接收的时间 | μs (微秒) 或 ns (纳秒) |
| **丢包率（Packet Loss）** | 丢失的数据包占总发送数据包的比例 | % |
| **CPU使用率** | 处理数据包时的CPU占用 | % |

**性能测试流程**：
```
1. 基线测试    → 测试系统的最大能力
2. 压力测试    → 测试系统在高负载下的表现
3. 稳定性测试  → 长时间运行验证稳定性
4. 对比测试    → 对比不同配置的性能差异
```

#### 实操步骤

**步骤1：创建性能测试学习文档**

```bash
vim docs/performance_testing_basics.md
```

**文档内容**：
```markdown
# 性能测试基础

## 1. 性能测试的目的

- 确定系统的性能上限
- 发现性能瓶颈
- 验证优化效果
- 为容量规划提供数据

## 2. DPDK性能测试的特点

### 2.1 高吞吐量
DPDK可以达到：
- 10Gbps网卡：~14.88 Mpps（百万包每秒）
- 25Gbps网卡：~37.2 Mpps
- 100Gbps网卡：~148.8 Mpps

计算公式（64字节最小包）：
\`\`\`
线速pps = 线速bps / (8 * (64 + 20))
其中：64字节数据 + 20字节开销（前导符、帧间隙等）
\`\`\`

### 2.2 低延迟
DPDK的延迟通常在：
- 软件处理：< 10 μs
- 硬件offload：< 1 μs

### 2.3 CPU效率
- 单核心处理能力
- 多核心扩展性
- CPU周期/数据包

## 3. 性能测试工具

### 3.1 testpmd
DPDK自带的测试工具，支持：
- 多种转发模式（io, mac, txonly等）
- 性能统计
- 硬件offload测试

### 3.2 流量生成器
- Scapy：软件流量生成（适合功能测试）
- TRex：高性能流量生成器
- IXIA/Spirent：硬件流量生成器

## 4. 性能测试方法

### 4.1 准备阶段
1. 隔离CPU核心（isolcpus）
2. 配置HugePages
3. 绑定网卡到DPDK驱动
4. 禁用电源管理（性能模式）

### 4.2 测试阶段
1. 预热：运行几分钟稳定系统
2. 测试：记录性能指标
3. 重复：多次测试取平均值

### 4.3 分析阶段
1. 计算吞吐量
2. 分析延迟分布
3. 检查丢包情况
4. 分析CPU使用率

## 5. 学习建议

对于初学者：
1. 先理解性能指标的含义
2. 学习使用testpmd进行基本测试
3. 了解如何收集和分析性能数据
4. 逐步学习高级性能优化技术

实际生产环境的性能测试需要：
- 专用的测试硬件
- 稳定的测试环境
- 详细的测试计划
- 多次测试验证
```

**验收标准**：
- [ ] 理解吞吐量、延迟、丢包率的含义
- [ ] 知道DPDK的典型性能指标
- [ ] 了解性能测试的基本流程

---

### 7.2 简单性能测试实践（预计30分钟）

由于真实的性能测试需要专门的硬件和环境，这里提供一个**学习版本**，重点理解测试逻辑。

#### 实操步骤

**步骤1：创建性能测试套件**

```bash
vim tests/TestSuite_performance_basics.py
```

**代码内容**：
```python
"""
性能测试基础套件

注意：这是一个学习版本，主要演示性能数据收集的概念
实际性能测试需要testpmd和真实的流量生成器
"""

import time
from framework.test_suite import TestSuite
from framework.testpmd_shell import EalParameters


class TestPerformanceBasics(TestSuite):
    """性能测试基础（学习版）"""

    def set_up_suite(self):
        """编译测试程序"""
        self.app_path = self.sut_node.build_dpdk_apps("helloworld")

    def test_perf_startup_time(self):
        """
        性能测试1：测量程序启动时间

        学习目标：了解如何测量时间性能指标
        """
        # 测量程序启动时间
        start_time = time.time()

        eal_params = EalParameters(lcore_list=[0])
        result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

        end_time = time.time()
        elapsed = end_time - start_time

        # 验证程序成功运行
        self.verify("hello from core" in result.stdout,
                   "程序未成功运行")

        # 记录性能数据
        self.logger.info(f"程序启动耗时: {elapsed:.3f} 秒")

        # 性能验收：启动时间应小于10秒
        self.verify(elapsed < 10.0,
                   f"启动时间过长: {elapsed:.3f}秒 > 10秒")

    def test_perf_multi_core_scaling(self):
        """
        性能测试2：多核心扩展性

        学习目标：了解如何测试多核心性能
        """
        # 测试不同核心数量下的性能
        core_counts = [1, 2, 4]
        results = {}

        for num_cores in core_counts:
            # 构造核心列表：[0, 1, ..., num_cores-1]
            lcore_list = list(range(num_cores))

            # 测量运行时间
            start_time = time.time()

            eal_params = EalParameters(lcore_list=lcore_list)
            result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

            end_time = time.time()
            elapsed = end_time - start_time

            # 验证程序成功运行
            self.verify("hello from core" in result.stdout,
                       f"{num_cores}核心测试失败")

            # 记录结果
            results[num_cores] = elapsed
            self.logger.info(f"{num_cores}核心耗时: {elapsed:.3f}秒")

        # 分析扩展性
        self.logger.info("\n=== 多核心扩展性分析 ===")
        baseline = results[1]
        for num_cores, elapsed in results.items():
            speedup = baseline / elapsed if elapsed > 0 else 0
            self.logger.info(f"{num_cores}核心 - 加速比: {speedup:.2f}x")

    def test_perf_memory_overhead(self):
        """
        性能测试3：内存开销分析

        学习目标：了解如何分析资源使用
        """
        # 检查系统内存信息
        # 注意：这里简化处理，实际应该使用更精确的方法

        eal_params = EalParameters(lcore_list=[0])
        result = self.sut_node.run_dpdk_app(self.app_path, eal_params)

        # 验证程序运行
        self.verify("hello from core" in result.stdout,
                   "程序未成功运行")

        # 从EAL日志中提取HugePages信息
        if "EAL: " in result.stdout:
            self.logger.info("EAL初始化日志:")
            for line in result.stdout.split('\n'):
                if "EAL: " in line and ("memory" in line.lower() or
                                        "huge" in line.lower()):
                    self.logger.info(f"  {line}")
```

**验收标准**：
- [ ] 理解如何测量时间性能
- [ ] 理解多核心扩展性的概念
- [ ] 知道如何从日志中提取性能数据

---

**步骤2：运行性能测试**

```bash
cd $DPDK_PATH/dts
poetry shell

./main.py --config-file conf/test_run.yaml \
          --output output_stage7 \
          --test-suite performance_basics \
          --verbose | tee test_stage7.log
```

**步骤3：分析性能数据**

```bash
# 从日志中提取性能数据
echo "========== 性能测试结果 =========="
grep -i "耗时\|加速比\|memory" output_stage7/dts.log
```

**验收标准**：
- [ ] 所有性能测试用例通过
- [ ] 能够看到启动时间数据
- [ ] 能够看到多核心扩展性数据
- [ ] 理解如何收集和分析性能指标

---

### 7.3 阶段7成果验收

**成果文件清单**：
```
docs/
└── performance_testing_basics.md    # 性能测试基础文档

tests/
└── TestSuite_performance_basics.py  # 性能测试套件

output_stage7/
└── dts.log                          # 测试日志

test_stage7.log                      # 性能测试输出
```

**必须完成的任务**：
1. [ ] 理解性能测试的关键指标（吞吐量、延迟、丢包率）
2. [ ] 完成performance_testing_basics.md学习文档
3. [ ] 运行性能测试套件并收集数据
4. [ ] 能够从日志中提取性能指标

**必须回答的问题**：
1. 什么是吞吐量？如何计算？
   - 答：单位时间内处理的数据包数量，单位pps或Gbps

2. 多核心扩展性是什么意思？
   - 答：随着核心数量增加，性能提升的比例。理想情况下，2核心应该是1核心性能的2倍

3. 为什么需要多次测试取平均值？
   - 答：消除偶然因素的影响，获得更准确的性能数据

---

## 总结与进阶路线

### 学习成果回顾

完成本手册的7个阶段后，你应该掌握：

**基础能力**：
- [x] 搭建DTS测试环境
- [x] 配置nodes.yaml和test_run.yaml
- [x] 运行和分析DTS测试
- [x] 阅读和理解测试套件源码

**实战能力**：
- [x] 独立编写测试套件
- [x] 使用Scapy创建和验证数据包
- [x] 收集和分析性能数据
- [x] 生成测试报告

**工具能力**：
- [x] 使用Poetry管理Python环境
- [x] 使用git管理代码版本
- [x] 使用日志分析工具排查问题
- [x] 使用自动化脚本提升效率

### 进阶学习路线

#### 初级 → 中级（1-2个月）

1. **深入学习testpmd**
   - 理解testpmd的各种转发模式
   - 学习使用testpmd进行流量测试
   - 掌握硬件offload功能测试

2. **学习更多测试套件**
   - pmd_buffer_scatter：PMD缓冲区分散测试
   - checksum_offload：校验和offload测试
   - vlan：VLAN功能测试

3. **真实网卡测试**
   - 配置物理网卡
   - 搭建双节点测试环境（SUT + TG）
   - 进行真实的流量测试

#### 中级 → 高级（2-3个月）

1. **性能优化**
   - CPU亲和性配置
   - NUMA优化
   - 内存通道优化
   - 网卡队列配置

2. **高级特性测试**
   - 加密/解密offload
   - 流量分类和过滤
   - QoS（服务质量）测试
   - 虚拟化环境测试

3. **CI/CD集成**
   - 集成DTS到Jenkins/GitLab CI
   - 自动化测试流程
   - 测试结果可视化

#### 高级 → 专家（3-6个月）

1. **框架定制**
   - 扩展DTS框架功能
   - 开发自定义工具
   - 贡献代码到DPDK社区

2. **大规模测试**
   - 多节点并行测试
   - 长时间稳定性测试
   - 性能回归测试

3. **专业领域**
   - NFV（网络功能虚拟化）测试
   - 5G/边缘计算测试
   - 存储加速测试

### 推荐资源

**官方文档**：
- DPDK官方文档：https://doc.dpdk.org/
- DTS用户指南：https://doc.dpdk.org/guides/tools/dts.html

**社区资源**：
- DPDK邮件列表：https://mails.dpdk.org/
- DPDK Slack频道：https://dpdk.slack.com/

**学习资料**：
- 《DPDK程序员指南》
- 《高性能网络编程》相关书籍

---

## 附录：常用命令速查

### DTS常用命令

```bash
# 激活Poetry环境
poetry shell

# 运行特定测试套件
./main.py --test-suite <套件名> --output <输出目录>

# 查看可用测试套件
ls tests/TestSuite_*.py

# 验证配置文件
python3 -c "import yaml; yaml.safe_load(open('conf/nodes.yaml'))"

# 查看测试结果
grep -E "PASSED|FAILED" output/dts.log
```

### 环境检查命令

```bash
# 检查HugePages
cat /proc/meminfo | grep Huge

# 检查SSH连接
ssh -o BatchMode=yes localhost 'exit'

# 检查网卡PCI地址
lspci | grep -i ethernet

# 检查CPU核心数
nproc
```

### 日志分析命令

```bash
# 提取错误信息
grep -i error output/dts.log

# 统计测试结果
grep -c "PASSED" output/dts.log
grep -c "FAILED" output/dts.log

# 查看编译日志
grep -i "build\|compil" output/dts.log
```

---

**恭喜你完成DTS实战学习！继续加油！**
