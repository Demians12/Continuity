//go:build linux

package loader

import (
	"os"
	"testing"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/rlimit"
)

// This is a best-effort verifier check.
//
// It will be skipped unless:
//   - you are running as root (or have CAP_BPF/CAP_SYS_ADMIN as required)
//   - you have generated a valid BPF ELF (go generate ./...)
func TestVerifierCanLoadProgram(t *testing.T) {
	if os.Geteuid() != 0 {
		t.Skip("needs root/CAP_BPF to load eBPF into the kernel")
	}

	spec, err := LoadSpec()
	if err != nil {
		t.Skipf("LoadSpec failed (run `go generate ./...`): %v", err)
	}

	if err := rlimit.RemoveMemlock(); err != nil {
		t.Skipf("remove memlock failed: %v", err)
	}

	coll, err := ebpf.NewCollection(spec)
	if err != nil {
		t.Fatalf("verifier load failed: %v", err)
	}
	_ = coll.Close()
}
