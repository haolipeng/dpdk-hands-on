# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a comprehensive DPDK (Data Plane Development Kit) learning project containing 11 progressive lessons with both tutorial documentation and working code examples. The project teaches DPDK fundamentals from basic initialization through advanced topics like multi-process architectures, timers, and lock-free ring queues.

**Language**: Tutorial documentation is in Chinese (中文), code comments are bilingual.

**Target Audience**: DPDK beginners with C programming experience.

## Build System

The project uses **CMake** (minimum version 3.10) rather than traditional Makefiles.

### Building the Project

```bash
# Initial setup
mkdir -p build
cd build

# Configure (default: Release mode)
cmake ..

# Build all examples
make

# Build specific example
make helloworld
make hash_usage
make capture_packet

# Debug mode (with symbols)
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Clean
make clean
```

### Build Output Structure

- **Build directory**: `build/` - all CMake-generated files
- **Binary directory**: `bin/` - all compiled executables (linked from build output)
- Executables are named according to their lesson (e.g., `bin/helloworld`, `bin/hash_usage`)

## Project Architecture

### Module Organization

The project follows a lesson-based structure where each numbered directory is a self-contained module:

```
1-helloworld/           - Lesson 1: EAL initialization, lcore basics
2-hash_usage/           - Lesson 2: Hash table usage, 5-tuple flow keys
3-capture_packet/       - Lesson 3: Network interface init, packet capture
4-parse_packet/         - Lesson 4: Protocol parsing (Ethernet/IPv4/TCP/UDP)
5-mempool_usage/        - Lesson 5: Memory pool management
6-flow_manager/         - Lesson 6: TCP flow tracking with hash tables
7-multiprocess/         - Lesson 7: Multi-process (primary/secondary, client/server)
8-multiprocess-communicate/ - Lesson 8: Inter-process communication via rings
9-timer/                - Lesson 9: DPDK timer subsystem
10-sp-sc-ring/          - Lesson 10: Single producer/consumer ring (SP/SC)
11-mp-mc-ring/          - Lesson 11: Multi producer/consumer ring (MP/MC)
12-hts-ring/            - Lesson 12: HTS (Head-Tail Sync) ring
13-mbuf_usage/          - Lesson 13: Mbuf usage and operations
14-acl/                 - Lesson 14: ACL (Access Control List) - high-performance packet classification
```

Each lesson directory contains:
- `CMakeLists.txt` - Module build configuration
- One or more `.c` source files
- Some have subdirectories (e.g., `7-multiprocess/basic/`, `7-multiprocess/client_server/`)

### Documentation Structure

Every lesson has an accompanying markdown file:
- `lessonN-<topic>.md` - Tutorial explaining concepts and code
- Additional guides: `DPDK-Ring-Practical-Guide.md`, `DPDK-Mbuf-Practical-Guide.md`, `DPDK-NUMA-Guide.md`, etc.

**Note on Lesson 14 (ACL)**: This lesson has been split into 3 parts for better learning experience:
- `lesson14-1-acl-basics.md` - ACL fundamentals and core concepts
- `lesson14-2-acl-practice.md` - Complete API usage and practical examples
- `lesson14-3-acl-advanced.md` - Performance optimization and advanced topics

## Key DPDK Concepts Used

### 1. EAL (Environment Abstraction Layer)
All programs start with `rte_eal_init(argc, argv)` and end with `rte_eal_cleanup()`. Common EAL parameters:
- `-l <core_list>` - Specify CPU cores (e.g., `-l 0-3`)
- `--no-pci` - Run without PCI devices (for examples that don't need NICs)
- `--log-level=8` - Detailed debug logging
- `--proc-type=primary|secondary` - For multi-process examples

### 2. Hugepages Requirement
DPDK requires hugepages configured before running:
```bash
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

### 3. Ring Queues (Core Data Structure)
DPDK rings are lock-free, fixed-size FIFO queues used for:
- Inter-thread communication
- Inter-process communication
- Memory pool management (underlying mempool)
- Pipeline architectures

**Synchronization Modes**:
- **SP/SC** (Single Producer/Single Consumer): Fastest, no CAS operations
- **MP/MC** (Multi Producer/Multi Consumer): Default, uses CAS atomics
- **HTS** (Head-Tail Sync): Fully serialized, good for overcommit scenarios
- **RTS** (Relaxed Tail Sync): Balance between MP/MC and HTS

**API Patterns**:
- `rte_ring_enqueue()` / `rte_ring_dequeue()` - Single element
- `rte_ring_enqueue_bulk()` / `rte_ring_dequeue_bulk()` - All-or-nothing batch
- `rte_ring_enqueue_burst()` / `rte_ring_dequeue_burst()` - Best-effort batch

### 4. Multi-Process Architecture
DPDK supports two models:
- **Primary-Secondary**: Primary creates resources, secondaries attach
- **Client-Server**: Server manages resources, clients request operations

Shared resources (rings, mempools) created by primary are accessible to secondaries via shared memory.

### 5. Memory Management
- **Mempool**: Pre-allocated object pools for high-performance allocation
- **Mbuf**: Network packet buffer structure
- Use `rte_malloc()` for DPDK-managed memory, not standard `malloc()`

## Running Examples

All examples require `sudo` for hugepages and device access.

### Examples Not Requiring NICs
These can run with `--no-pci`:
```bash
sudo ./bin/helloworld -l 0 --no-pci
sudo ./bin/hash_usage -l 0 --no-pci
sudo ./bin/mempool_usage -l 0 --no-pci
sudo ./bin/sp_sc_ring -l 0-1 --no-pci
```

### Examples Requiring NICs
Need network interface bound to DPDK driver:
```bash
sudo ./bin/capture_packet -l 0
sudo ./bin/parse_packet -l 0
sudo ./bin/flow_manager -l 0
```

### Multi-Process Examples
Run primary first, then secondaries in separate terminals:

**Lesson 7 - Basic Primary/Secondary**:
```bash
# Terminal 1
sudo ./bin/mp_basic_primary -l 0-1 --proc-type=primary

# Terminal 2
sudo ./bin/mp_basic_secondary -l 2 --proc-type=secondary
```

**Lesson 7 - Client/Server**:
```bash
# Terminal 1 (server)
sudo ./bin/mp_cs_server -l 0-1 --proc-type=primary

# Terminal 2 (client)
sudo ./bin/mp_cs_client -l 2 --proc-type=secondary --proc-id=0
```

**Lesson 8 - Multi-Process Communication**:
```bash
# Terminal 1
sudo ./bin/mp_comm_server -l 0-1 --proc-type=primary

# Terminal 2
sudo ./bin/mp_comm_client -l 2 --proc-type=secondary
```

## Development Guidelines

### When Adding New Lessons

1. **Create directory**: `N-lesson_name/`
2. **Add CMakeLists.txt** following the pattern:
   ```cmake
   cmake_minimum_required(VERSION 3.10)
   find_package(PkgConfig REQUIRED)
   pkg_check_modules(DPDK REQUIRED libdpdk)

   add_executable(target_name source.c)
   target_include_directories(target_name PRIVATE ${DPDK_INCLUDE_DIRS})
   target_link_libraries(target_name ${DPDK_LIBRARIES})
   set_target_properties(target_name PROPERTIES
       RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
   ```
3. **Update root CMakeLists.txt**: Add `add_subdirectory(N-lesson_name)`
4. **Create markdown doc**: `lessonN-topic.md` with theory and usage

### Code Style Conventions

- Use `__rte_cache_aligned` for structures accessed by multiple threads
- Initialize EAL before any DPDK operations
- Always check return values from DPDK functions
- Use `rte_exit()` or `rte_panic()` for fatal errors
- Free resources in reverse order of allocation
- Signal handlers should set volatile flags, not directly exit

### Common Patterns

**EAL Initialization**:
```c
int ret = rte_eal_init(argc, argv);
if (ret < 0)
    rte_panic("Cannot init EAL\n");
```

**Creating Ring**:
```c
struct rte_ring *ring = rte_ring_create(
    "ring_name",
    RING_SIZE,           // Must be power of 2
    rte_socket_id(),     // NUMA-local socket
    RING_F_SP_ENQ | RING_F_SC_DEQ  // Flags
);
```

**Launching Worker Threads**:
```c
RTE_LCORE_FOREACH_WORKER(lcore_id) {
    rte_eal_remote_launch(worker_func, arg, lcore_id);
}
rte_eal_mp_wait_lcore();  // Wait for all to finish
```

## Troubleshooting Common Issues

### Build Issues

**DPDK not found**:
```bash
# Check DPDK installation
pkg-config --exists libdpdk && echo "OK" || echo "NOT FOUND"

# Install on Ubuntu
sudo apt-get install dpdk dpdk-dev

# Set PKG_CONFIG_PATH if manual install
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

### Runtime Issues

**Hugepages not configured**:
```bash
# Check current
grep Huge /proc/meminfo

# Configure
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

**Permission denied**:
- Always use `sudo` for DPDK applications
- Or configure permissions: `sudo chmod 777 /var/run/dpdk`

**No available Ethernet device**:
- For examples without NIC: use `--no-pci` flag
- For examples with NIC: bind interface to DPDK driver using `dpdk-devbind.py`

**Multi-process "Cannot create lock"**:
- Ensure primary process runs first
- Check file prefix matches between processes
- Verify no stale lock files in `/var/run/dpdk/`

## Performance Considerations

### NUMA Awareness
- Create rings/mempools on the same socket as the lcore that uses them
- Use `rte_socket_id()` to get current socket

### Batch Operations
- Always use bulk/burst APIs instead of single element operations
- Typical batch sizes: 32-64 elements
- Amortizes per-operation overhead

### Ring Sizing
- SP/SC: 1024-2048 is usually sufficient
- MP/MC: Use 4096-8192 to reduce contention
- Must be power of 2 (or use `RING_F_EXACT_SZ` flag)

### Cache Line Alignment
- Use `__rte_cache_aligned` for shared data structures
- Prevents false sharing between cores

## Dependencies

- **DPDK**: Version 24.11.2+ (22.11+ should work)
- **CMake**: 3.10+
- **GCC**: 7.0+
- **pkg-config**: Required for DPDK detection
- **Linux**: Ubuntu 20.04+ recommended
- **Hugepages**: 2MB or 1GB pages required

## Testing

Each example is self-contained and can be tested independently. Success criteria:
- Program runs without crashes
- EAL initializes successfully
- Resource allocation succeeds (rings, mempools)
- Statistics show expected behavior (packet counts, message counts)
- For multi-process: secondary can attach and communicate with primary

## Reference Documentation

- **DPDK Official**: https://doc.dpdk.org/
- **Programming Guide**: https://doc.dpdk.org/guides/prog_guide/
- **API Reference**: https://doc.dpdk.org/api/
- **Sample Applications**: https://doc.dpdk.org/guides/sample_app_ug/

## Learning Path

Follow lessons in order:
1. Lessons 1-2: Basic DPDK concepts (EAL, data structures)
2. Lessons 3-6: Packet processing fundamentals
3. Lessons 7-8: Multi-process architectures
4. Lesson 9: Timers and event scheduling
5. Lessons 10-12: Advanced ring queue modes (SP/SC, MP/MC, HTS)
6. Lesson 13: Mbuf operations and packet buffer management
7. Lesson 14 (ACL trilogy): High-performance packet classification
   - 14-1: ACL basics (concepts and data structures)
   - 14-2: ACL practice (API usage and complete examples)
   - 14-3: ACL advanced (performance tuning and troubleshooting)

Each lesson builds on previous concepts.
