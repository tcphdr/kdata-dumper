# PS5 Kernel Data Dumper (kdata-dumper)

A lightweight payload to extract the kernel data section from a jailbroken console.

## Features

* **Dynamic DMAP Resolution:** Parses `pmap_store` relative to the kernel data base to calculate `g_dmap_base` and `cr3`.
* **Hardware Page-Table Walking:** Manually translates virtual addresses (`vaddr`) to physical addresses (`paddr`) supporting standard 4K pages and 2M large pages.
* **Resilient Dumping:** Traverses memory page-by-page, safely falling back to zero-filled buffers if a page mapping is missing or unreadable to preserve data alignment.
* **Live Progress Reporting:** Emits periodic feedback to kernel logs (`klog`) for every 16 MB written.

---

## Code Overview

* `resolve_dmap_base()`: Inspects `pmap_store` to establish the relationship between PML4 and CR3 registers.
* `vaddr_to_paddr()`: Traverses the 4-level paging structure (PML4 -> PDP -> PD -> PT) to find the physical frame backing the target virtual space.
* `dump_segment_dmap()`: Streams raw bytes from the DMAP directly into the output file descriptor using 4KB chunks.

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
