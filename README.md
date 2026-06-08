# PS5 Kernel Data Dumper (kdata-dumper)

A lightweight payload to extract the kernel data section from a jailbroken console. It dynamically resolves the Direct Map (DMAP) base address, walks the page tables to map virtual addresses to physical space, and dumps the specified segment to disk via userland logs.

## Features

* **Dynamic DMAP Resolution:** Parses `pmap_store` relative to the kernel data base to calculate `g_dmap_base` and `cr3`.
* **Hardware Page-Table Walking:** Manually translates virtual addresses (`vaddr`) to physical addresses (`paddr`) supporting standard 4K pages and 2M large pages.
* **Resilient Dumping:** Traverses memory page-by-page, safely falling back to zero-filled buffers if a page mapping is missing or unreadable to preserve data alignment.
* **Live Progress Reporting:** Emits periodic feedback to kernel logs (`klog`) for every 16 MB written.

---

## Technical Specifications

| Parameter | Value | Description |
| :--- | :--- | :--- |
| **Target Firmware** | >= 6.00 | Confirmed working on 11.20; results may vary on other firmware versions. |
| **`PMAP_STORE_OFF`** | `0x02E04F18` | Offset to `pmap_store`, 11.20 specific, needs to be changed for other firmware versions. |
| **Default Dump Size** | `0x8800000` (~136 MB) | Standard kernel data segment size allocation |
| **Output Path** | `/data/kdata.bin` | Direct destination on the console filesystem |

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
   data_base=0xffffffffxxxxxxxx dmap_base=0x00000xxxxxxxxxxx kernel_cr3=0x00000xxxxxxx
   dumping 0x8800000 bytes to /data/kdata.bin
   progress: 16 / 136 MB
   progress: 32 / 136 MB
   ...
   kdata dump complete
## Credits
* EchoStretch's ps5-kdata-dumper (https://github.com/echostretch/ps5-kdata-dumper): Inspiration & original concept.
