# noah(1) -- Linux ABI implementation (aka Execution Flavour) for OSX

## SYNOPSIS

`noah` `-h` \| [_-o output_file_] \[_-w warning_file_] \[_-s strace_file_] `-m /virtual/filesystem/root` `program` \[_..._]

## DESCRIPTION

Noah implements Linux Application Binary Interface (ABI) for OSX through its
Hypervisor Framework based on Intel(R) VTX technology.

For convenience, a wrapper script written in PERL is provided for launching,
as well as debugging the tool (it has to be edited manually). It is that script
that is normally executed from path, providing _~/.noah/tree_ as the
virtual root.

On the first invocation the script uses noahstrap(1) to populate this virtual
root with a default Ubuntu suite, see noahstrap-suites(1).

#### Options

  _-h_, _--help_ output a short help message.

  _-w file_, _--warning file_ optional, specifies the warning capture file. 

  _-o file_, _--output file_ optional, specifies the output capture file.

  _-s file_, _--strace file_ optional, specifies the strace capture file.

  _-m /virtual/filesystem/root_, _--mnt /virtual/filesystem/root_ mandatory, specifies the virtual filesystem root where the target
  application, as well as the ELF interpreter and the rest of dynamic libraries
  reside.

  _program_ the target program within the virtual filesystem root.

## FILES

  _~/.noah/tree_

    Default virtual filesystem root.

## REFERENCES:

- [xhyve](https://github.com/mist64/xhyve)
- [Linux Darling project](http://www.darlinghq.org/source-code/)
- [FreeBSD Linuxolator aka Linux ABI](https://www.freebsd.org/doc/handbook/linuxemu-advanced.html)
- [Bash on Ubuntu on macOS](https://hagi.is.s.u-tokyo.ac.jp/~yuichi/papers/apsys2017.pdf)
- [Noah Hypervisor-Based Darwin Subsystem for Linux](http://events17.linuxfoundation.org/sites/events/files/slides/Noah%20Hypervisor-Based%20Darwin%20Subsystem%20for%20Linux-pdf.pdf)

## SEE ALSO:

noahstrap(1), noahstrap-suites(1)
