#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause
# DPDK Multiprocess Examples - Setup and Run Script

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 打印信息函数
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查是否有root权限
check_root() {
    if [ "$EUID" -ne 0 ]; then
        print_error "此脚本需要root权限运行"
        print_info "请使用: sudo $0"
        exit 1
    fi
}

# 检查hugepage配置
check_hugepages() {
    print_info "检查Hugepage配置..."

    nr_hugepages=$(cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages)
    print_info "当前2MB hugepage数量: $nr_hugepages"

    if [ "$nr_hugepages" -lt 256 ]; then
        print_warn "Hugepage数量不足,建议至少256个"
        print_info "配置512个hugepage..."
        echo 512 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
        print_info "Hugepage配置完成"
    else
        print_info "Hugepage配置正常"
    fi
}

# 编译程序
build_examples() {
    print_info "编译DPDK多进程示例..."

    # 创建build目录
    if [ ! -d "build" ]; then
        mkdir build
    fi

    cd build
    cmake ..
    make -j$(nproc)

    if [ $? -eq 0 ]; then
        print_info "编译成功!"
        print_info "可执行文件位于: build/bin/"
        ls -lh bin/
    else
        print_error "编译失败!"
        exit 1
    fi

    cd ..
}

# 清理旧的共享内存
cleanup_shm() {
    print_info "清理旧的共享内存..."
    rm -rf /dev/hugepages/rtemap_* 2>/dev/null || true
    print_info "清理完成"
}

# 运行实验1
run_experiment1() {
    print_info "=========================================="
    print_info "实验1: 基础Primary-Secondary示例"
    print_info "=========================================="
    print_info ""
    print_info "此实验演示:"
    print_info "  - Primary进程创建共享对象"
    print_info "  - Secondary进程查找并使用共享对象"
    print_info "  - 单向消息传递 (Primary -> Secondary)"
    print_info ""
    print_info "运行步骤:"
    print_info "  1. 启动Primary进程:"
    print_info "     sudo ./build/bin/mp_basic_primary -l 0 --no-huge"
    print_info ""
    print_info "  2. 在另一个终端启动Secondary进程:"
    print_info "     sudo ./build/bin/mp_basic_secondary -l 1 --proc-type=secondary --no-huge"
    print_info ""
    print_info "  3. 观察消息传递过程"
    print_info "  4. 按Ctrl+C退出两个进程"
    print_info ""
}

# 运行实验2
run_experiment2() {
    print_info "=========================================="
    print_info "实验2: Ring双向通信示例"
    print_info "=========================================="
    print_info ""
    print_info "此实验演示:"
    print_info "  - 创建双向Ring队列"
    print_info "  - Ping-Pong消息交互"
    print_info "  - RTT(往返时延)测量"
    print_info ""
    print_info "运行步骤:"
    print_info "  1. 启动Sender进程:"
    print_info "     sudo ./build/bin/mp_ring_sender -l 0 --no-huge"
    print_info ""
    print_info "  2. 在另一个终端启动Receiver进程:"
    print_info "     sudo ./build/bin/mp_ring_receiver -l 1 --proc-type=secondary --no-huge"
    print_info ""
    print_info "  3. 观察Ping-Pong通信过程和RTT统计"
    print_info "  4. 按Ctrl+C退出两个进程"
    print_info ""
}

# 运行实验3
run_experiment3() {
    print_info "=========================================="
    print_info "实验3: Client-Server架构"
    print_info "=========================================="
    print_info ""
    print_info "此实验演示:"
    print_info "  - Server生成数据包并分发"
    print_info "  - 多个Client并行处理数据包"
    print_info "  - 负载均衡 (Round-Robin)"
    print_info ""
    print_info "运行步骤:"
    print_info "  1. 启动Server进程 (支持2个Client):"
    print_info "     sudo ./build/bin/mp_cs_server -l 0 --no-huge -- -n 2"
    print_info ""
    print_info "  2. 在另一个终端启动Client 0:"
    print_info "     sudo ./build/bin/mp_cs_client -l 1 --proc-type=secondary --no-huge -- -n 0"
    print_info ""
    print_info "  3. 在第三个终端启动Client 1:"
    print_info "     sudo ./build/bin/mp_cs_client -l 2 --proc-type=secondary --no-huge -- -n 1"
    print_info ""
    print_info "  4. 观察数据包分发和处理过程"
    print_info "  5. 按Ctrl+C退出所有进程"
    print_info ""
}

# 显示菜单
show_menu() {
    echo ""
    echo "=========================================="
    echo "DPDK多进程实验 - 快速启动脚本"
    echo "=========================================="
    echo "1. 检查环境和编译"
    echo "2. 运行实验1 (基础Primary-Secondary)"
    echo "3. 运行实验2 (Ring双向通信)"
    echo "4. 运行实验3 (Client-Server架构)"
    echo "5. 清理共享内存"
    echo "0. 退出"
    echo "=========================================="
    echo -n "请选择 [0-5]: "
}

# 主函数
main() {
    check_root

    while true; do
        show_menu
        read choice

        case $choice in
            1)
                check_hugepages
                build_examples
                ;;
            2)
                run_experiment1
                ;;
            3)
                run_experiment2
                ;;
            4)
                run_experiment3
                ;;
            5)
                cleanup_shm
                ;;
            0)
                print_info "退出脚本"
                exit 0
                ;;
            *)
                print_error "无效选择,请重试"
                ;;
        esac

        echo ""
        echo "按Enter继续..."
        read
    done
}

# 如果直接运行脚本,显示菜单
if [ "$1" == "" ]; then
    main
else
    # 支持命令行参数
    case $1 in
        build)
            check_root
            check_hugepages
            build_examples
            ;;
        clean)
            check_root
            cleanup_shm
            ;;
        *)
            print_error "未知命令: $1"
            print_info "用法: $0 [build|clean]"
            exit 1
            ;;
    esac
fi
