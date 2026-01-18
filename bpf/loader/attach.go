package loader

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/link"
	"github.com/cilium/ebpf/rlimit"
)

type Loaded struct {
	Coll    *ebpf.Collection
	Link    link.Link
	PinPath string
}

func LoadAndAttach(cgroupPath, pinPath string) (*Loaded, error) {
	if err := rlimit.RemoveMemlock(); err != nil {
		return nil, fmt.Errorf("remove memlock: %w", err)
	}

	spec, err := LoadSpec()
	if err != nil {
		return nil, fmt.Errorf("load spec: %w", err)
	}

	// Load programs and maps.
	coll, err := ebpf.NewCollection(spec)
	if err != nil {
		return nil, fmt.Errorf("new collection: %w", err)
	}

	prog := coll.Programs["nity_connect4"]
	if prog == nil {
		coll.Close()
		return nil, errors.New("program nity_connect4 not found")
	}

	// Attach to the provided cgroup.
	lnk, err := link.AttachCgroup(link.CgroupOptions{
		Path:    cgroupPath,
		Attach:  ebpf.AttachCGroupInet4Connect,
		Program: prog,
	})
	if err != nil {
		coll.Close()
		return nil, fmt.Errorf("attach cgroup/connect4: %w", err)
	}

	// Pin maps for agent/control-plane use.
	if pinPath != "" {
		if err := os.MkdirAll(pinPath, 0o755); err != nil {
			lnk.Close()
			coll.Close()
			return nil, fmt.Errorf("mkdir pin path: %w", err)
		}
		for name, m := range coll.Maps {
			// Pin only maps that are part of the contract surface.
			// (All maps in this object are contract maps or contract counters.)
			dst := filepath.Join(pinPath, name)
			// If a pin already exists, keep it (MVP: we don't hot-replace pinned maps).
			if _, err := os.Stat(dst); err == nil {
				continue
			}
			if err := m.Pin(dst); err != nil {
				lnk.Close()
				coll.Close()
				return nil, fmt.Errorf("pin map %s: %w", name, err)
			}
		}
	}

	return &Loaded{Coll: coll, Link: lnk, PinPath: pinPath}, nil
}

func (l *Loaded) Close() error {
	var err1, err2 error
	if l.Link != nil {
		err1 = l.Link.Close()
	}
	if l.Coll != nil {
		err2 = l.Coll.Close()
	}
	if err1 != nil {
		return err1
	}
	return err2
}
