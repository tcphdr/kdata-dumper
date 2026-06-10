# PS5 Kernel Data Dumper (kdata-dumper)

A lightweight payload to extract the kernel data section from a jailbroken console.

## Features
* **Firmware-Aware DMAP Resolution:** Selects the correct `pmap_store` offset at runtime based on the firmware version, then reads `pm_pml4` and `pm_cr3` via the SDK's kernel pipe primitive to establish the DMAP base and CR3 without any hardcoded absolute addresses.
* **Hardware Page-Table Walking:** Manually translates kernel virtual addresses to physical addresses by traversing the 4-level paging structure (PML4 → PDPE → PDE → PTE) through the DMAP, with correct handling of 2MB large pages at the PDE level.
* **Resilient Dumping:** Traverses memory page-by-page through the DMAP, safely falling back to zero-filled buffers if a page mapping is absent, preserving output alignment and file size.
* **Live Progress Reporting:** Emits periodic feedback to stdout for size scanning (every 32MB) and dump progress (every 16MB), with a consolidated notification to the system UI on completion.

---

## Code Overview
* `get_pmap_store_offset(fw)`: Accepts the firmware version integer and returns the known `pmap_store` offset from the kernel data base for that firmware. Returns 0 for unsupported versions.
* `resolve_dmap_base(pmap_store)`: Reads `pm_pml4` and `pm_cr3` from the resolved `pmap_store` address via the SDK's kernel pipe primitive, establishes `g_kernel_cr3`, and returns the DMAP base as `pml4 - cr3`.
* `vaddr_to_paddr(vaddr)`: Traverses the 4-level paging structure (PML4 → PDPE → PDE → PTE) through the DMAP to resolve a kernel virtual address to its backing physical address. Handles 2MB large pages at the PDE level via the PS bit.
* `find_data_segment_size(data_vaddr)`: Walks forward from the data segment base in 4KB increments via `vaddr_to_paddr`, tracking consecutive unmapped pages. Returns the offset at which `GAP_LIMIT` consecutive missing pages are encountered, indicating the segment boundary.
* `dump_segment_dmap(vaddr, size, out_fd)`: Streams the data segment to disk in 4KB chunks. Each page is translated to a physical address and read through the DMAP via `kernel_copyout`. Unmapped pages are zero-filled to preserve alignment. Emits gap and progress diagnostics to stdout.

## Requirements
* Jailbroken PS5
* ELF Loader (https://github.com/ps5-payload-dev/elfldr)
* PS5 Payload SDK (https://github.com/ps5-payload-dev/sdk)
---

## Usage

1. Compile the payload using your local environment configuration.
2. Send or execute the payload within an environment that has kernel read/write capabilities.
3. Monitor progress via netcat or a dedicated kernel logging utility:
   ```bash
   # Example monitoring string output
   [init] FW 11.20
   [init] data_base=0xffffffffdbf60000 pa=0x1bf60000
   [init] dmap_base=0xffff856700000000 cr3=0x1f064000
   [init] pmap_store=0xffffffffded64f18 (offset=0x2e04f18)
   [size] scanning segment size...
   [size] scanned 32 MB...
   [size] scanned 64 MB...
   [size] segment: vaddr=0xffffffffdbf60000 size=0x54a0000 (84 MB)
   [dump] starting dump to /data/kdata.bin
   [dump] progress: 16 / 84 MB
   [dump] progress: 32 / 84 MB
   [dump] progress: 48 / 84 MB
   [dump] progress: 64 / 84 MB
   [dump] progress: 80 / 84 MB
   [done] kdata dump complete
   ...
   kdata dump complete
## Credits
* EchoStretch's ps5-kdata-dumper (https://github.com/echostretch/ps5-kdata-dumper): Inspiration & original concept.
