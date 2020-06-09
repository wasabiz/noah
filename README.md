# Noah

Noah is a Darwin subsystem for Linux, or "Bash on Ubuntu on Mac OS X". Noah is implemented as a hypervisor that traps linux system calls and translates them into Darwin's system calls. Noah also has an interpreter of ELF files so that binary executables of Linux run directly and flawlessly without any modifications.

__Noah is an experimental product__. Most Linux apps don't work well due to missing system calls. For the technical details, please refer to [its academic paper](https://dl.acm.org/doi/abs/10.1145/3381052.3381327). Currently, this repository is not actively maintained. It might not run on the latest macOS.

<img src="https://github.com/linux-noah/noah/blob/master/images/screenshot.png" width="600">

## Quick Start

Noah is installed via Homebrew or MacPorts. On the first run, noah automatically downloads and installs a comprehensive linux environment in your home directory (by default, ubuntu 16.04 is installed in `~/.noah/tree`).
macOS Sierra or higher is required.

### Homebrew

```console
$ brew install linux-noah/noah/noah
$ noah
```
### MacPorts

```console
$ sudo port install noah
$ noah
```

## Hacking

See [HACKING.md](HACKING.md).

## LICENSE

Dual MITL/GPL, for all files without explicit notation.
