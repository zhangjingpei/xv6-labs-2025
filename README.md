# MIT 6.828 xv6 操作系统内核学习项目  

## 一、项目简介  
本项目围绕 **MIT 6.828 课程**的 **xv6 教学内核**展开，旨在帮助开发者**系统剖析操作系统核心原理**（进程、内存、文件系统、调度等）。通过 **代码精读、实验扩展、文档沉淀**，将晦涩的 OS 理论转化为可实操的学习路径，适合 OS 初学者或想深化底层认知的工程师。  


## 二、为什么选择 xv6？  
xv6 是 **极简版类 UNIX 内核**，专为教学设计，具备以下独特优势：  
- ✨ **代码轻量**：核心代码仅 ~1 万行，剔除复杂特性（如网络、多CPU），聚焦 **“进程、内存、文件系统、中断”四大核心**，比 Linux 更易拆解。  
- 🔍 **原理纯粹**：用 C 和少量汇编实现，模块边界清晰（如 `proc.c` 管进程、`vm.c` 管内存），是理解 OS 设计的“解剖标本”。  
- 🎓 **生态完善**：MIT 6.828 课程提供 **实验指导、测试用例、文档**，可直接复用学习资源，降低入门门槛。  


## 三、环境搭建（Linux 环境，以 Ubuntu 为例）  

### 1. 安装依赖工具  
```bash
# 安装交叉编译工具链、QEMU 模拟器、GDB 等
sudo apt update
sudo apt install git gcc-riscv64-linux-gnu gdb-multiarch qemu-system-misc make libglib2.0-dev
```  

### 2. 获取 xv6 代码（官方仓库）  
```bash
# 克隆 MIT 6.828 实验代码（含 xv6 内核）
git clone https://github.com/mit-pdos/xv6-labs-2020.git
cd xv6-labs-2020
```  

### 3. 编译并运行 xv6  
```bash
# 编译内核（生成 xv6.img 和内核二进制）
make qemu
# 成功启动后，进入 xv6 命令行，可测试 `ls` `echo hello` 等命令
```  


## 四、核心学习资源  
| 资源类型          | 链接/说明                                                                 |
|-------------------|--------------------------------------------------------------------------|
| **官方课程**      | [MIT 6.828 2020 课程网站](https://pdos.csail.mit.edu/6.828/2020/)（含实验文档、视频） |
| **xv6 代码**      | 项目根目录 `kernel/` 下，核心文件：<br>- `proc.c`：进程管理（调度、fork/exec）<br>- `vm.c`：内存管理（分页、物理内存分配）<br>- `fs.c`：文件系统（inode、目录、块设备）<br>- `trap.c`：中断与系统调用 |
| **推荐教材**      | 《Operating Systems: Three Easy Pieces》（免费在线版：https://pages.cs.wisc.edu/~remzi/OSTEP/） |
| **调试指南**      | 使用 `make qemu-gdb` 启动调试模式，配合 GDB 单步调试内核：<br>1. 终端1：`make qemu-gdb`（启动 QEMU，等待调试连接）<br>2. 终端2：`gdb-multiarch` → `target remote localhost:26000`（连接 QEMU） |  


## 五、xv6 核心模块解析  
### 1. 进程管理（`kernel/proc.c`）  
- **数据结构**：`struct proc` 存储进程状态（`RUNNING`/`RUNNABLE`/`SLEEPING`）、页表、内核栈、PID 等。  
- **核心逻辑**：  
  - `fork()`：复制父进程地址空间和进程控制块，实现“进程创建”。  
  - `schedule()`：采用 **Round-Robin 轮转调度**，从就绪队列中选择下一个运行的进程。  


### 2. 内存管理（`kernel/vm.c`）  
- **物理内存**：通过 `kalloc()`/`kfree()` 管理空闲页帧（基于**空闲链表**）。  
- **虚拟内存**：  
  - 每个进程有独立页表，通过 **分页机制** 映射虚拟地址到物理地址，实现“用户空间隔离”。  
  - 内核空间（`KERNBASE` 以上）全局共享，用户空间（`KERNBASE` 以下）进程私有。  


### 3. 文件系统（`kernel/fs.c`）  
- **存储模型**：基于 **磁盘镜像（fs.img）** 模拟块设备，支持inode（文件元数据）、目录、文件读写。  
- **核心操作**：  
  - `open()`：根据路径查找 inode，初始化文件描述符。  
  - `read()`/`write()`：通过 **缓冲区缓存（buffer cache）** 优化磁盘IO。  


### 4. 中断与系统调用（`kernel/trap.c`）  
- **陷阱（Trap）**：处理时钟中断（触发调度）、系统调用（用户态→内核态的入口）、异常（如页错误）。  
- **系统调用流程**：  
  1. 用户态通过 `ecall` 指令触发中断，进入内核态。  
  2. 内核根据 `a7` 寄存器的系统调用号，分发到对应处理函数（如 `sys_read`/`sys_write`）。  


## 六、实验示例：添加 `sys_yield` 系统调用  
**目标**：让进程主动放弃CPU，触发调度器切换。  

### 1. 定义系统调用（`kernel/syscall.c`）  
```c
// 1. 添加系统调用号（需唯一，如32）
enum {
  ...
  SYS_yield = 32, // 新增系统调用号
};

// 2. 系统调用名称表（方便调试）
static char *syscallname[] = {
  ...
  [SYS_yield] "yield",
};

// 3. 系统调用实现
uint64 sys_yield(void) {
  yield(); // 调用调度逻辑
  return 0;
}
```  

### 2. 实现 `yield` 逻辑（`kernel/proc.c`）  
```c
void yield(void) {
  struct proc *p = myproc(); // 获取当前进程
  p->state = RUNNABLE;       // 标记为“可运行”
  schedule();                // 触发调度，切换进程
}
```  

### 3. 用户态测试程序（`user/yield.c`）  
```c
#include "syscall.h"
#include "printf.h"
#include "proc.h"

int main(void) {
  printf("Process %d yields CPU...\n", getpid());
  sys_yield(); // 调用自定义系统调用
  printf("Process %d resumes.\n", getpid());
  exit(0);
}
```  

### 4. 编译与运行  
```bash
# 修改 Makefile，添加 yield 测试程序
echo 'yield: yield.c' >> Makefile
make qemu
# 在 xv6 命令行输入 `yield`，观察进程切换
```  

