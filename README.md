# LuoOS

## (LuoOS Utilizes Opensource Operating Systems)

队名：AA5555AA

学校：武汉大学

成员：钟祎诚 谭君宇

指导教师：蔡朝晖

[TOC]

## 设计理念

本次参赛的操作系统由参赛队员的操作系统课设项目持续演进而来，具有如下设计理念。

### 从零开始循序渐进扩充系统功能

现有的操作系统实验众多，但大多是提供了整体框架只留存部分子模块以供实现，而未体现架构设计上的取舍。从零开始实现一个操作系统，我们经历了众多架构上的踩坑和调整，在不同阶段参考了不同的系统设计，由此了解到不同架构的考量。

从零实现系统能够对系统各功能间的依赖有更多的认识，整体路线图遵循先实现运行环境后扩充功能、先实现功能运行后优化性能、封装解耦接口提前考虑重构的原则。初期我们参考软件所RVOS课程实现了M-mode与S-mode的异常与中断，随后参考u-core、xv6和浙江大学OS Lab实现了虚拟内存的管理和简易的用户态进程，支持Linux系统调用的过程中我们又参考Zircon、z-core和Linux修正了内存与进程部分的诸多设计，后续开发中我们也将更多的参考Zircon系与Linux内核的设计。

### 利用现代编程语言简化代码

在开发语言的选型阶段，我们一开始就排除了纯C语言，而是决定采用现代高级语言开发，有如下原因：

- 现代语言能提供更高层次的抽象与更丰富的语义，如`constexpr`、`namespace`、`bitfield`等很适于建模各类控制寄存器和状态属性，避免了C语言所写系统中处处是宏的低可读性，命名空间也使得符号命名更为简洁清晰。
- 现代语言能提供更好的面向对象支持，如STL的数据结构封装、Zircon内核一切皆是内核对象的封装，继承及模板也避免了C语言所写系统中处处是链表节点的冗杂。
- 现代语言能提供更好的资源管理和错误处理特性，如引用语义、RAII、智能指针都有助于减少系统编写中手动管理内存带来的错误。

而在rust与C++中我们最终选择了C++，因为两位同学此前并不具有Rust的开发经验，而内核设计赛时间紧任务重，因此选择了较为熟悉的C++语言。但在后续的开发过程中，我们逐渐意识到Rust语言的一些优势，包括但不限于：

- 标准库设计优良。Rust语言的标准库具有良好的依赖分层，在bare-metal层可使用no_std，在定义了allocator和default allocator之后则可直接使用标准库中的collections；而C++则直到C++23才能有较为可用的freestanding子集，标准库也与系统libc耦合，难以复用，因而我们有许多精力都耗费在重新实现类STL的数据结构、寻找与移植合适的tinySTL上。
- 周边生态丰富。系统分享会中我们才发现，Rust社区具有许多优秀的crates能大大简化OS实现，如virtio、sbi-runtime等。不过自行移植/实现这一部分也使得我们对各项功能有了更多的认识。

最后，我们的项目中尽量避免编写汇编，而是采用宏（`csrRead/Write/Set/Clear`, `ExecInst`）、属性标准`__attribute((naked))__` 等提高可读性。

### 复用开源基础设施

操作系统虽然相对而言较为耦合，但也有许多部分是非常模块化的，且这些部分往往本身实现起来具有较大的工作量。因此我们利用了许多开源基础设施，以简化我们的工作。目前具体有如下三方面：

- 内存分配：我们从OpenHarmony系统中了解到TLSF(Two-Level Segeregated Fit)算法是一种实时性高（O(1)复杂度）、性能优秀的堆内存分配算法，因此决定采用该种算法。搜索开源社区后发现其实现复杂度极高（动辄800行），但也高度模块化，因此比较后采用了[OlegHahm/tlsf](https://github.com/OlegHahm/tlsf)项目，外加上层的封装作为内核的堆内存分配器。
- 数据结构：C++使用数据结构必然采用类STL的实现，由于标准STL无法使用而其编写难度高，我们评估了许多项目后最终采用了[mendsley/tinystl](https://github.com/mendsley/tinystl)。但其只提供了很少的一部分数据结构，后期可能再移植其他更完整的STL实现。
- 文件系统：文件系统部分代码量较大，受制于工期我们目前移植适配了去年获奖队伍[能run就行队]()的实现（包括fat/bio/部分virtio），而采用C++的重构与精简则正在与VFS一道进行。

## 具体设计

在这一章节，我们首先描述内核运行环境的设计和取舍，再从功能划分的角度概述各模块设计，模块的实现细节在[docs](./docs)子目录中。

### 运行环境

相较于大型应用软件，操作系统内核的难度很大程度上在于在其运行环境的复杂多变，主要体现在三个方面：

- 执行流程：从嵌入式到高性能，trap是操作系统最基础的特性，而这也使得系统的执行流程变得非线性。
- 内存环境：内存环境的复杂性主要体现于进出中断时的切换，和用户态与内核态间的数据传输。
- 并发：区域赛阶段我们尚未实现并行运行，但在设计中充分考虑了对扩展至SMP的支持。具体的，我们将内核几乎所有的全局对象封装至`kGlobObjs`与`kHartObjs`，分别为多核全局的（如内存分配、内核信息、内核日志等）和各核心局部的（主要是当前核心绑定线程及其所依赖的结构），支持SMP时通过运算符重载对其进行加锁以简化代码。此外，我们在trapframe中保存有当前核心的hartid以用于局部资源的辨识。

以下分别从执行流程和内存环境的角度介绍设计。

#### 执行流程

内核的执行流大致有如下四种：

1. 初始化，为避免内核态重入的复杂性，该阶段我们设计为线性执行到底，初始化结束后再开启中断。

2. 用户态发起系统调用，该类执行过程实际上是用户态线程的延续，通常线性执行，但有部分例外：

   - yield或导致优先级抢占的系统调用：抢占是通过写`sip::stip`主动触发时钟中断达成，而yield则是通过一段特殊的代码实现。两者都会将上下文保存在`KContext`中，后续从中恢复上下文继续执行，实现在该内核线程视角的透明性。

   在该类内核线程的视角，所有系统调用的执行均从`uecallHandler`开始和退出。

3. 中断处理，中期我们曾支持过内核态的时钟中断抢占，但在后续优化virtio性能时遇到了难以定位的上下文混乱。考虑到现阶段主要目标并非性能优化，我们将其改回了：

   - 用户态开启时钟与外部中断；
   - 内核态只在virtio驱动中开启外部中断，在kIdle内核线程中开启外部和时钟中断以实现抢占，在用户线程对应的内核线程执行中关闭中断。

4. 纯内核线程，目前实现了kIdle线程，后续计划将日志输出等作为内核worker实现。该类线程主体均为一个事件循环，永远在内核态执行，因而其必须支持内核态的时钟中断。

由这四种内核执行流，我们需要具有如下三类执行栈：

1. 用户栈
2. 内核栈：为支持内核态的执行流切换，我们为每个用户线程分配了独立的内核栈。
3. 中断栈：考虑到兼容性，目前我们的异常处理采用了`Direct vector mode`即单入口，每次的执行栈都需动态获取。我们不能预知下次进入内核是由于中断还是异常，因此采用如下约定：
   * 当Hart在用户态执行，使用当前用户线程的内核栈作为中断栈；
   * 当Hart在内核态执行，使用分配给每个Hart的独立中断栈。

#### 内存环境

内核视角下的内存环境通常有两种选择：shared address space与dedicated kernel space（引述自[The Linux Kernel  documentation](#The Linux Kernel  documentation)）。权衡安全性、性能、实现复杂性后，我们采用了专有内核空间的设计，即所有内核线程共享一个内核地址空间、内核地址空间与用户地址空间可重叠。在陷入的入口处，我们将`satp`切换为内核页表，在出口切换回用户页表。

该种设计需要trampoline，现阶段我们是将整个内核的代码段与静态数据段映射至进程地址空间，将每个进程的trapframe映射至其地址空间，后续可通过链接脚本和`__attribute((section))__`将所用到的部分剥离出来。

目前内核态的内存空间布局如下：

用户态的内存空间布局如下：

其中用户进程的初始栈地址由内核指定，其后用户进程或线程可自行申请空间改变位置；用户堆的起始地址目前采用静态指定，后续需换为根据用户程序的data段设定。

### 功能模块

#### 内存管理



#### 内存分配



#### 中断处理



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