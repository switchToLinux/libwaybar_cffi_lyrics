# GDB调试帮助
> GDB调试是一个非常重要的工具，在开发过程中，我们经常需要调试代码，找到问题所在。本文将介绍GDB调试的基本用法，以及如何使用GDB调试代码。

当你运行waybar时发现它崩溃了，你可以使用coredumpctl命令来查看崩溃信息。coredumpctl命令可以帮助你查看崩溃信息，包括崩溃的原因、堆栈信息、寄存器信息等。

如何使用coredumpctl命令来查看崩溃信息？

在archlinux中，coredumpctl的安装方式如下：
```bash
sudo pacman -S coredumpctl
```

安装后，你可以使用coredumpctl命令来查看崩溃信息。

程序crash后，通常会在 /var/lib/systemd/coredump 目录下生成一个 coredump 文件，你可以使用 coredumpctl 命令来查看崩溃信息。

coredumpctl命令的用法如下：
```bash
coredumpctl list # 列出所有的崩溃信息(包括崩溃的原因、堆栈信息、寄存器信息等)

coredumpctl info <corefile> # 查看指定的崩溃信息(包括崩溃的原因、堆栈信息、寄存器信息等)

coredumpctl debug waybar # 启动gdb调试器，调试第一个匹配的 waybar 崩溃信息core文件
coredumpctl debug <pid> # 启动gdb调试器，调试指定的进程崩溃信息core文件

coredumpctl dump <pid> # 生成指定进程的崩溃信息core文件到标准输出（通常是终端，通常也是没必要执行这个的）


```

接下来详细说下通过`coredumpctl debug waybar`调试查看崩溃信息的方法：

1. 首先，你需要找到崩溃的原因。崩溃的原因通常是在堆栈信息中，你可以使用`bt`命令来查看堆栈信息。
2. 然后，你可以使用`where`命令来查看崩溃的位置。崩溃的位置通常是在堆栈信息中，你可以使用`where`命令来查看崩溃的位置。
3. 最后，你可以使用`frame`命令来查看当前的堆栈帧。你可以使用`frame`命令来查看当前的堆栈帧。

对于多线程程序，你可以使用`thread`命令来查看所有的线程。你可以使用`thread`命令来查看所有的线程。

如何清理coredumpctl生成的崩溃信息core文件？

coredumpctl生成的崩溃信息core文件通常是在 /var/lib/systemd/coredump 目录下，你可以使用以下命令来清理崩溃信息core文件：
```bash

ls -l /var/lib/systemd/coredump/

sudo rm /var/lib/systemd/coredump/core.*

```
> 注意：只有有core文件才能gdb调试定位问题，删除了core文件后，gdb调试器就无法重新定位问题了，但通常还是可以看到崩溃日志的信息。




