# Noah Hacking Guide

## Build from Source

```console
$ mkdir build
$ cd build
$ ccmake ..
$ make && make install
```

You can find an install prefix field in the graphical UI of ccmake. The commands above install the `noah` command in your system. The `noah` command is a simple perl script that checks if a Linux system is ready on your system and install it otherwise before executing the "real" noah binary.

The "real" noah binary, which is made from `src/main.c`, requires some mandatory command line options. If you want to execute it directly, you will need to start the binary with the following manner:

```console
$ INSTALL_PREFIX/libexec/noah -m ROOT_PATH PATH_TO_INIT
```
where `ROOT_PATH` is a path to the directory that is treated as the root mount point in the Linux box, and `PATH_TO_INIT` is a path to the first command to be run in the boot sequence, like `/bin/bash`.

`noahstrap` helps you set up a Linux environment on your local machine.
It retrieves a ready-to-use distro image from the Internet and extracts it to a specified directory.
`noahstrap` is installed via homebrew.

```console
$ brew install linux-noah/noah/noahstrap
$ noahstrap --help  # prints help message
```

## Debugging

There are several methods to debug `noah`.

The first option is to use lldb, of course. Since the noah command indirectly invokes the noah binary through a perl script, it would be convenient to modify the script to start up the binary with lldb.

The second option is to use _meta-strace_. Because we still don't have implemented the ptrace system call, we cannot use strace to trace the log inside the Linux box. To tackle this problem, we provide a feature called meta-strace, that logs system call entering and retirement at the `meta level`, which means outside virtual machines. This feature is enabled via `--strace OUTFILE` option for the `noah` command. When you add a new system call and check the behavior, it might be helpful to add new strace {pre,post} hooks in the `src/meta_strace.c`. This should greatly improve the appearance of the trace logs.

The third option is to use the _output_ option. This is mere debug logs that are emit using the `printk` function in noah's source codes. This feature is enabled with `--output OUTFILE` option.

## Source Structure

Sources are placed in the following rules:

- `bin/`: the `noah` perl script inhabits
- `lib/`: sources for the VMM components
- `src/foo/*`: Linux subsystem emulations put in the corresponding directories
- `src/meta_strace.c`: meta-strace, see Debugging section
- `src/base.c`: general virtual kernel primitives, like copy_from_user
- `src/main.c`: the entry point
