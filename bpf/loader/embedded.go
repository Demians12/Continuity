package loader

import (
	"bytes"
	_ "embed"

	"github.com/cilium/ebpf"
)

//go:embed gen/sockops_connect.o
var bpfObj []byte

func LoadSpec() (*ebpf.CollectionSpec, error) {
	return ebpf.LoadCollectionSpecFromReader(bytes.NewReader(bpfObj))
}
