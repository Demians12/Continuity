package loader

import "testing"

func TestContractSurfacePresent(t *testing.T) {
    spec, err := loadNity()
    if err != nil {
        t.Fatalf("load spec: %v", err)
    }

    requiredMaps := []string{
        "slot_table_A",
        "slot_table_B",
        "active_table",
        "epoch",
        "conntrack_lru",
        "last_agent_seen_ts",
        "rt_control",
        "fallback_size",
        "fallback_backends",
        "counters",
    }

    for _, m := range requiredMaps {
        if _, ok := spec.Maps[m]; !ok {
            t.Fatalf("missing required map: %s", m)
        }
    }

    if _, ok := spec.Programs["nity_connect4"]; !ok {
        t.Fatalf("missing program nity_connect4")
    }
}
