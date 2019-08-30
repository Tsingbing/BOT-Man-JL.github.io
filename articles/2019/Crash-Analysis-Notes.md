﻿# 崩溃分析笔记

> 2019/8/4
> 
> We all earn our pay and reputations not by how we debug, but by how quickly and accurately we do it. —— Mark Russinovich

[heading-numbering]

通过崩溃现场排查问题，是一件很有意思的事情 —— 像是 “侦探推理” ——

- 你只能看到崩溃瞬间的状态（崩溃时转储为 Dump 文件），从有限的信息里收集线索
- 你可能看不到崩溃的位置的代码（例如第三方模块），需要利用“逆向”思维来大胆假设
- 你需要掌握更全面的知识（了解 操作系统/编译原理/C++，熟悉被分析的代码），从有限的线索里验证猜想

最近的 Windows/C++ 崩溃分析工作，让我再次学到了如何 “从底层的视角，看上层的事物”。

## 目录 [no-toc] [no-number]

[TOC]

## 崩溃类型

> 如何提高感知力和控制力，就像学游泳一样，要到水中练，去亲近水。——《格蠹汇编》张银奎

### 内存破坏崩溃

- 原因：
  - 野指针：释放资源后使用
  - 指针强转：基类指针 `static_cast<>()` 转成错误的派生类，数据对齐错误
  - 多线程错误：非线程安全对象被其他线程 创建/访问/销毁（例如 UI 数据只允许 UI 线程 **访问**，可能重入字体缓存）
- 排查：
  - Mini Dump 数据不全，只能用 Full Dump 分析（[sec|判断对象是否有效]）
  - 如果没有立即崩溃，由于数据损坏，可能导致 **其他崩溃**
- 辅助：
  - 添加日志/上报
  - 检查堆破坏（例如 Application Verifier / 页堆 Pageheap）
  - 检查栈破坏（例如 secure_cookie `/Gs`）
  - 使用立即崩溃（例如 `assert`/`CHECK`）

### 空指针崩溃

- 原因：
  - 函数的前置条件改变，调用者对条件的假设（例如 `debug_assert`/`DCHECK`）失效
  - 内存破坏导致
- 排查：
  - 回溯调用栈，可以发现空指针首次出现的位置，并推断出现的路径
  - 空指针加上 offset 再访问数据成员，变为野指针崩溃（标识：指针值小于对象大小）

### 除零崩溃

- 原因：
  - 除 `/` 0
  - 取模 `%` 0
- 原理：`cdq; idiv eax, ?` -> `eax` 商 & `edx` 余数

### 栈溢出崩溃

- 原因：
  - 逻辑错误，导致无穷递归调用
  - 导火索可能是非法外部输入（例如 发送 `WM_CLOSE`，导致无限弹出关闭确认对话框）
- 排查：
  - 回溯调用栈，找到递归调用的函数范围，并推断出错原因

### 内存申请崩溃

- 原因：
  - 输入大量字符串
  - 配置文件过大
- 排查：
  - 校验外部输入合法性

### 系统调用崩溃

- 现象：
  - `QueryPerformanceFrequency`/`localtime(&-1)`/`wcsftime(..., nullptr)` 函数调用崩溃
  - `DLLMain`/`WinProc` 回调位置错误（被重定向到无效函数）
- 原因：
  - 系统内部错误
  - 外部代码注入

### 指令/硬件错误崩溃

- `ILLEGAL_INSTRUCTION`/`MISALIGNED_CODE`/`PRIVILEGED_INSTRUCTION`/`SOFTWARE_NX_FAULT`
  - 解释：指令 无效/不对齐/无法执行/不在可执行段内
  - 原因：可能是外部注入导致指令损坏，或内存破坏导致跳转错误
  - 特点：调用栈分散，不易于聚合统计
  - 解决：[禁止第三方注入](https://blog.chromium.org/2017/11/reducing-chrome-crashes-caused-by-third.html)、与第三方沟通、保证 PE 文件完整性
- `IN_PAGE_ERROR`
  - 内存映射文件 `MEM_MAPPED` 读取时，磁盘 `hardware_disk` 错误

## Windbg 常用命令

> 工欲善其事，必先利其器。——《论语·卫灵公》

- `!analyze -v; .ecxr` 定位崩溃位置
- `version`/`|`/`~`/`.frame` 系统/进程/线程/栈帧
- `kPL` 查看调用栈和参数，并隐藏代码位置
- `kvL` 查看调用栈和FPO/调用约定，并隐藏代码位置
- `dv` 列举当前栈帧上 参数、局部变量 的值
- `dt TYPE [ADDR]` 查看 TYPE 类型对象（在 ADDR 上）的内存布局
- `dx ((TYPE *)ADDR)`
  - 使用 [NatVis](https://docs.microsoft.com/en-us/visualstudio/debugger/create-custom-views-of-native-objects) 查看 TYPE 可视化数据（内置 STL 支持）
  - 根据 对象内存布局 + 虚函数表指针，还原 ADDR 上 TYPE 实际的派生类
- `dps __VFN_table` 查看虚函数表指针对应的虚函数地址
- `u ADDR`/`ub ADDR`/`uf ADDR/SYM` 反汇编/往前反汇编/反汇编函数
- `x MOD!SYM*` 匹配函数符号（包括虚函数表符号）
- `s -d 0 L?80000000 VAL` 搜索内存中是否存在 VAL 值
- `!address ADDR` 查看地址属性（堆/栈/代码区，可读/可写/可执行，已提交/保留）
- `!heap -a [SEGMENT]` 查看（堆段 SEGMENT 上）内存分配详情

## 分析技巧

> Dump 文件就像照片，每个文件都可能让我们想起一段故事，时间越长，越值得回味。——《格蠹汇编》张银奎

### 聚合统计维度

- 顶部调用栈：头几个调用指令在模块中的相对位置（哈希值）
- 加载模块/外部进程/安全软件：查找共同的注入模块/外部环境
- 操作系统/硬件情况

### 判断代码是否有效-1

- `uf eip` 查看崩溃代码的指令
- `lm a eip` 查看崩溃代码对应模块的地址范围，记录 `START1`
- `?eip-START1` 计算崩溃代码在模块中的偏移量，记录 `OFFSET`
- 本地运行相同版本的模块 `MODULE`
- `lm m MODULE` 查看正常代码对应模块的地址范围，记录 `START2`
- `uf START2+OFFSET` 查看正常代码的指令，和崩溃代码比较

### 判断代码是否有效-2

- `uf eip` 查看崩溃函数的指令
- 检查是否有明显逻辑错误（例如，`[esi/epb+-10000h]`/不常见的长指令/调用不合理的函数）

### 判断对象是否有效

- 如果对象内存 **没被覆写**：
  - 如果 `__VFN_table` 有效，表示头部完整
  - 如果 [`base::WeakPtrFactory::ptr_ == this`](https://cs.chromium.org/chromium/src/base/memory/weak_ptr.h?q=base::WeakPtrFactory)，表示尾部完整
  - 通过 `ref_count_/size_` 等变量推测数据成员是否有效
  - 判断有没有执行过析构函数的迹象（例如 `ptr_ = nullptr;`）
- 如果对象内存 **已被覆写**：
  - 通过 `db/da/du` 查看是否被写成 字符串
  - 通过 `dps` 查看是否被写成 其他对象
- 检查其他平级的成员对象：
  - 指针值是否临近（指针成员变量，要求在同一构造函数里创建，不一定准确）
  - 连续内存是否完整（非指针成员变量）

### 查找内存中的对象

- `x` 列出类的（所有）虚函数表指针
- `s -d` 搜索内存中的虚函数表指针（多重继承取最小指针）
- `dt` 还原对象内存布局
- 通过再次搜索（成员指针）或 减去偏移量（成员对象），找到对象的持有者

### 排查异步回调任务

- 通过 [`base::Location base::PendingTask::posted_from;`](https://cs.chromium.org/chromium/src/base/pending_task.h?q=base::PendingTask) 定位任务抛出的来源

### 考虑崩溃时机

- `main()` 前初始化（全局对象）
- 启动时初始化（消息循环栈对象）
- 消息循环/定时器阶段（静态对象 + `LazySingleton`）
- 退出时反初始化（消息循环栈对象 + `AtExitManager`）
- `main()` 后反初始化（静态对象 + 全局对象）

### 排查外部因素

- 发布线上运营任务，触达之前的不常用路径
- 第三方注入经常导致 指令错误崩溃
- 修复某些崩溃，引发后续执行代码的崩溃

### 进程与线程

- 由于崩溃以进程为边界，[Chromium 多进程架构](https://developers.google.cn/web/updates/2018/09/inside-browser-part1) _(1 Browser + n Renderer + 1 GPU + n Extension + x Util)_ 能有效的避免因为某一进程崩溃导致的程序不可用。（例如，页面崩溃后仍然可以使用浏览器）
- 由于线程共享进程的内存，[Chromium 多线程架构](https://github.com/chromium/chromium/blob/master/docs/threading_and_tasks.md#threads) _(1 UI-Browser/Main-Renderer + 1 IO-IPC + x Spec + n Worker-Pool)_ 中某个线程出错，可能导致其他线程的崩溃，难以直接排查。（例如，逻辑错误导致内存损坏）

## 参考 [no-number]

- [C++ 反汇编笔记](Cpp-Disassembly-Notes.md)（必备知识点）
- [Windbg 调试命令详解 - 张佩](http://yiiyee.cn/blog/2013/08/23/windbg/)
- [利用 C++ 类对象的虚拟函数表指针在内存中搜索对象 - 张佩](https://blog.csdn.net/blog_index/article/details/7016696)
- [从堆里寻找丢失的数据 - 张银奎](http://advdbg.org/blogs/advdbg_system/articles/3413.aspx)

如果有什么问题，**欢迎交流**。😄

Delivered under MIT License &copy; 2019, BOT Man