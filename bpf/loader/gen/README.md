This directory holds the compiled eBPF object for the dataplane MVP.

- Source: `bpf/programs/sockops_connect.c`
- Output: `sockops_connect.o`

Regenerate on a host with clang built with the BPF backend:

```bash
go generate ./...
```

Or run clang directly:

```bash
clang -O2 -g -Wall -Werror -target bpf -D__TARGET_ARCH_x86 \
  -I../include -I/usr/include -I/usr/include/x86_64-linux-gnu \
  -c ../programs/sockops_connect.c -o sockops_connect.o
```

Note: the sandbox environment used to author this MVP doesn't include the BPF
codegen backend, so the checked-in `sockops_connect.o` is a placeholder and must
be regenerated before running.
