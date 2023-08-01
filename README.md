# LuoOS 全国赛设计

## (LuoOS Utilizes Opensource Operating Systems)

队名：AA5555AA

学校：武汉大学

成员：钟祎诚 谭君宇

指导教师：蔡朝晖

[TOC]

## 初赛阶段设计

参见[README.md](docs/初赛文档/README.md)

## 主要的改进与升级

全国赛阶段，我们在初赛设计的基础上重构了内存管理和文件系统两个大系统，分别实现了初赛计划中的VMAR内存管理和VFS虚拟文件系统。同时，为满足全国赛阶段更多样的系统调用需求，添加了信号处理流程、不完全的用户与组管理功能、设备文件支持以及相应的ramfs文件系统。此外，由于全国赛最终需要在硬件平台StarFive开发板上运行，还添加了与`virtio_disk`同层次的sdcard驱动，实现SD卡的读写，并为其使用的sdio协议做了相应的适配。

### 内存管理

主要工作：实现进程及内存对象VMAR的各种功能与接口，实现了初赛计划中的懒加载，并将进程对内存的各种操作统一封装为对VMAR的操作，从而取代重复的页表操作（但仍保留页表供硬件使用），优化内存管理。
更多细节参见[重构内存管理.md](docs/重构内存管理.md)

### 文件系统

主要工作：在原有FAT32的基础上添加了VFS层，原有的FAT32文件系统作为继承自VFS的子类存在，实现VFS的各种接口供`File`层统一调用。为此，对FAT32的架构进行了微调，恢复了`INode`层使之适应VFS的要求。同时，文件系统部分新增了对路径操作的封装Path类，以及支持部分系统设备文件所需的ramfs文件系统。前者大大简化了用于处理路径字符数组的重复代码，后者为新增系统调用所需。
更多细节参见[vfs.md](docs/vfs.md)、[ramfs.md](docs/ramfs.md)

### 信号处理

主要工作：在进程和线程级别，增加了信号处理所需的各种数据结构，并在[trap.cc](kernel/trap.cc)返回用户态的过程中添加了信号处理函数`sigHandler`，处理线程收到的各种信号。
更多细节参见[信号处理.md](docs/信号处理.md)

### 基础设施

主要工作：内核库上采用功能更完备的EASTL代替TINYSTL，引入C++20标准的错误处理机制`expected`处理部分函数的异常值，优化调试日志`kLogger`的表现并增加了转储`dump()`功能。
更多细节参见[基础设施及杂项.md](docs/基础设施及杂项.md)

### 多核支持

主要工作：引入了SMP支持，并通过了区域赛测例。但在后续开发过程及调试过程中出现的新问题迫使我们暂时将其关闭。
更多细节参见[多处理器.md](docs/多处理器.md)

## 路线图

- [x] 实现机器模式下的输出及中断异常处理
- [x] 引导至内核，实现S-mode与M-mode的通信
- [x] 实现内核态异常处理与时钟中断
- [x] 实现虚拟内存管理
- [x] 实现进程与进程调度
- [x] 实现进程克隆
- [x] 实现打印与管道
- [x] 移植文件系统
- [x] 实现 `execve` 系统调用
- [x] 实现区域赛33个系统调用
- [x] 支持SMP
- [x] 整理重构文件系统
- [x] 优化内核库
- [x] 规范化地址空间布局
- [x] 实现CoW机制

初赛时定下的路线图全部完成，但距离国赛预期达到的效果仍有差距：

- [ ] 完全实现所有的国赛系统调用
- [ ] 完全支持多核运行
- [ ] 完全支持用户与组管理
- [ ] 用户态可用的内核日志

### 疑难Bug

大致解决了QEMU环境下遇到的各种疑难BUG，但移植到开发板上时问题多多……包括但不限于：
-SD卡驱动工作不正常
-写入satp寄存器时卡死
-nanosleep、time_test等时间相关程序跑不动
-程序跑完不停机
诸如此类……
移植后的BUG由于时间安排原因，没有记录下详细经历。QEMU环境下的问题与解决方法，参见[问题与解决方法.md](docs/问题与解决方法.md)

## 参考资料

### RISCV标准

- The RISC-V Instruction Set Manual, Volume II: Privileged Architecture, Version 1.10”, Editors Andrew Waterman and Krste Asanovi´c, RISC-V Foundation, May 2017
- 《RISC-V SBI specification》. Makefile. 2018. Reprint, RISC-V Non-ISA Specifications. https://github.com/riscv-non-isa/riscv-sbi-doc.

### OS及实验

- [ISCAS-RVOS](https://gitee.com/unicornx/riscv-operating-system-mooc)
- [操作系统Lab - 知乎专栏](https://www.zhihu.com/column/c_1464733712995184640)
- [Zircon Kernel objects  | Fuchsia](https://fuchsia.dev/fuchsia-src/reference/kernel_objects/objects)
- [GitHub - rcore-os/zCore-Tutorial: Tutorial for zCore kernel.](https://github.com/rcore-os/zCore-Tutorial)
- [Introduction — The Linux Kernel  documentation](https://linux-kernel-labs.github.io/refs/heads/master/lectures/intro.html)
- The Design and Implementation of the FreeBSD Operating System by Marshall Kirk McKusick, George V. Neville-Neil, Robert N.M. Watson
- [GitHub - mit-pdos/xv6-riscv: Xv6 for RISC-V](https://github.com/mit-pdos/xv6-riscv)
- [OSKernel2023-NutOS](https://gitlab.eduxiji.net/202314430101195/oskernel2023-nutos)

### 工具及辅助设施

- [RISC-V: A Baremetal Introduction using C++.](https://www.shincbm.com/embedded/2021/05/03/riscv-and-modern-c++-part1-4.html)
- [five-embeddev](http://five-embeddev.com/)
- [Yale CPSC422/522 lab tools guide](https://flint.cs.yale.edu/cs422/labguide/index.html)
- Microsoft New Bing