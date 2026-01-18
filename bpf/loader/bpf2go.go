//go:build tools

package loader

// This file wires go:generate to build the BPF object and generate Go bindings.
// Run from repo root:
//   go generate ./...
//
// Note: adjust -target if your build host differs. For kind nodes on amd64, amd64 is typical.
// The C code is UAPI-only (no vmlinux.h), so it is expected to be portable across kernels.

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go \
//   -cc clang \
//   -target amd64 \
//   -cflags "-O2 -g -Wall -Werror -I../include -I/usr/include -I/usr/include/x86_64-linux-gnu" \
//   -output-dir gen \
//   nity ../programs/sockops_connect.c -- -I../include
