# DTB Dump Feature

The VM_Arm component dumps the guest VM's Device Tree Blob (DTB) to the
console before starting the VM. This is useful for debugging device tree
issues or verifying the generated DTB content.

## Output Format

The DTB is dumped as base64-encoded data with clear markers:

```
DTB_DUMP_START size=12345
AAAAAAAAABBBBBBBBBBBBCCCCCCCCCCC...
GGGGGGGGGGGHHHHHHHHHHHIIIIIIIII...
DTB_DUMP_END
```

The `size=` field shows the actual DTB size in bytes (not the buffer size).

## Extracting the DTB

From captured console output, extract and decode:

```bash
# Extract base64 lines and decode
grep -A9999 'DTB_DUMP_START' console.log | \
    grep -B9999 'DTB_DUMP_END' | \
    grep -v 'DTB_DUMP' | \
    tr -d '\n' | \
    base64 -d > guest.dtb

# View the device tree
fdtdump guest.dtb

# Or convert to DTS format
dtc -I dtb -O dts guest.dtb -o guest.dts
```

## Implementation Notes

- Only the actual DTB content is dumped (not trailing zeros in the buffer)
- Size is determined by `fdt_totalsize()` from libfdt which reads the FDT header
- Output is chunked into 76-character lines for compatibility with terminal logging
- The dump occurs after `load_vm_images()` completes and before `vcpu_start()`

## Source Location

The implementation is in `projects/vm/components/VM_Arm/src/main.c`:
- `base64_encode_buf()` - Base64 encoding helper
- `dump_dtb_base64()` - DTB dump function
- Called from `main_continued()` before VM startup
