/*
 * kdata-dumper: gpu.c
 */

#include "gpu.h"

static int gc_fd = -1;
static u64 victim_va  = 0;
static u64 transfer_va = 0;
static u64 victim_ptbe_va = 0;
static u64 cleared_ptbe_for_ro = 0;
static u64 g_dmap_base = 0;
static u64 g_kernel_cr3 = 0;

// Read kernel qword via kernel_copyout
static u64 kread64(u64 addr) {
    u64 val = 0;
    kernel_copyout(addr, &val, sizeof(val));
    return val;
}

static u64 resolve_dmap_and_cr3(u64 *cr3_out) {
    // pmap_store is at data_base + FW-specific offset
    // For 11.20: DATA_BASE_KERNEL_PMAP_STORE offset = 0x02E04F18
    u64 pmap_store = (u64)KERNEL_ADDRESS_DATA_BASE + 0x02E04F18ULL;
    u64 pml4 = kread64(pmap_store + 0x20); // PMAP_PML4 offset
    u64 cr3  = kread64(pmap_store + 0x28); // PMAP_CR3 offset
    if (cr3_out) *cr3_out = cr3;
    return pml4 - cr3; // dmap_base
}

static u64 phys_to_dmap(u64 phys) {
    return g_dmap_base + phys;
}

// Walk CPU page tables to resolve virtual -> physical
static u64 virt_to_phys(u64 vaddr, u64 cr3) {
    u64 pml4e_idx = (vaddr >> 39) & 0x1ff;
    u64 pdpe_idx  = (vaddr >> 30) & 0x1ff;
    u64 pde_idx   = (vaddr >> 21) & 0x1ff;
    u64 pte_idx   = (vaddr >> 12) & 0x1ff;

    u64 pml4e = kread64(phys_to_dmap(cr3) + pml4e_idx * 8);
    if (!(pml4e & 1)) return 0;

    u64 pdp_pa = pml4e & CPU_PG_PHYS_FRAME;
    u64 pdpe   = kread64(phys_to_dmap(pdp_pa) + pdpe_idx * 8);
    if (!(pdpe & 1)) return 0;

    u64 pd_pa = pdpe & CPU_PG_PHYS_FRAME;
    u64 pde   = kread64(phys_to_dmap(pd_pa) + pde_idx * 8);
    if (!(pde & 1)) return 0;

    // Check PS bit — 2MB large page
    if (pde & (1 << 7))
        return (pde & CPU_PG_PS_FRAME) | (vaddr & 0x1fffff);

    u64 pt_pa = pde & CPU_PG_PHYS_FRAME;
    u64 pte   = kread64(phys_to_dmap(pt_pa) + pte_idx * 8);
    if (!(pte & 1)) return 0;

    return (pte & CPU_PG_PHYS_FRAME) | (vaddr & 0x3fff);
}

// Get curproc vmid from kernel
static u32 get_curproc_vmid(u64 curproc, u64 proc_vmspace_off) {
    u64 vmspace = kread64(curproc + proc_vmspace_off);
    u32 vmid = 0;
    kernel_copyout(vmspace + VMSPACE_VM_VMID_OFF, &vmid, sizeof(vmid));
    return vmid;
}

static u64 get_gvmspace(u32 vmid) {
    return DATA_BASE_GVMSPACE + (u64)vmid * SIZEOF_GVMSPACE;
}

static u64 get_pdb2_addr(u32 vmid) {
    return kread64(get_gvmspace(vmid) + GVMSPACE_PAGE_DIR);
}

// Walk GPU page tables — returns [pte_va, page_size] or 0 on failure
static u64 gpu_walk_pt(u32 vmid, u64 rel_va, u64 *page_size_out) {
    u64 pdb2  = get_pdb2_addr(vmid);
    u64 l3_idx = (rel_va >> 39) & 0x1ff;
    u64 l2_idx = (rel_va >> 30) & 0x1ff;
    u64 l1_idx = (rel_va >> 21) & 0x1ff;

    u64 l3e = kread64(pdb2 + l3_idx * 8);
    if (!(l3e & GPU_VALID)) return 0;

    u64 l2_pa  = l3e & GPU_PDE_ADDR_MASK;
    u64 l2e    = kread64(phys_to_dmap(l2_pa) + l2_idx * 8);
    if (!(l2e & GPU_VALID)) return 0;

    u64 l1_pa  = l2e & GPU_PDE_ADDR_MASK;
    u64 l1e    = kread64(phys_to_dmap(l1_pa) + l1_idx * 8);
    if (!(l1e & GPU_VALID)) return 0;

    u64 l1e_va = phys_to_dmap(l1_pa) + l1_idx * 8;

    if (l1e & GPU_IS_PTE) {
        if (page_size_out) *page_size_out = 0x200000;
        return l1e_va;
    }

    u64 frag = (l1e & GPU_FRAG_MASK) >> GPU_FRAG_SHIFT;
    u64 pt_pa = l1e & GPU_PDE_ADDR_MASK;
    u64 offset = rel_va & 0x1fffff;
    u64 pte_va, psz;

    if (frag == 4) {
        u64 ti = offset >> 16;
        u64 tv = kread64(phys_to_dmap(pt_pa) + ti * 8);
        if ((tv & GPU_VALID) && (tv & GPU_TF)) {
            ti = (rel_va & 0xffff) >> 13;
            pte_va = phys_to_dmap(pt_pa) + ti * 8;
            psz = 0x2000;
        } else {
            pte_va = phys_to_dmap(pt_pa) + ti * 8;
            psz = 0x10000;
        }
    } else {
        u64 ti = offset >> 13;
        pte_va = phys_to_dmap(pt_pa) + ti * 8;
        psz = 0x2000;
    }

    if (page_size_out) *page_size_out = psz;
    return pte_va;
}

// Submit a PM4 DMA_DATA command to copy src_va -> dst_va on the GPU
static void gpu_submit_dma(u64 dst_va, u64 src_va, u32 size_bytes) {
    // Build PM4 DMA_DATA packet (opcode 0x50, 6 dwords payload)
    u32 pm4[7];
    u32 dma_hdr = (0 << 0)  |  // ENGINE_SEL src=memory
                  (2 << 13) |  // DST_SEL=dst_addr
                  (1 << 15) |  // src cache policy
                  (2 << 25) |  // DST cache policy
                  (1 << 27) |  // DST_VOLATILE
                  (1u << 31);  // raw wait

    pm4[0] = PM4_TYPE3_HEADER(0x50, 6);
    pm4[1] = dma_hdr;
    pm4[2] = (u32)(src_va & 0xffffffff);
    pm4[3] = (u32)(src_va >> 32);
    pm4[4] = (u32)(dst_va & 0xffffffff);
    pm4[5] = (u32)(dst_va >> 32);
    pm4[6] = size_bytes & 0x1fffff;

    // Write PM4 into cmd_va (reuse transfer_va + offset for cmd buf)
    u64 cmd_va = transfer_va + GPU_DMA_SIZE - 0x1000;
    for (int i = 0; i < 7; i++) {
        u32 v = pm4[i];
        // write pm4 dword into cmd_va via mapped memory
        *((volatile u32 *)(uintptr_t)(cmd_va + i * 4)) = v;
    }

    // Build command descriptor and submit via ioctl 0xC0108102
    struct { u32 pipe_id; u32 cmd_count; u64 desc_ptr; } submit;
    u64 desc[2];
    u32 sz_dwords = 7;
    desc[0] = ((cmd_va & 0xffffffff) << 32) | 0xC0023F00u;
    desc[1] = (((u64)sz_dwords & 0xfffff) << 32) | ((cmd_va >> 32) & 0xffff);

    u64 desc_va = transfer_va + GPU_DMA_SIZE - 0x100;
    memcpy((void *)(uintptr_t)desc_va, desc, sizeof(desc));

    submit.pipe_id   = 0;
    submit.cmd_count = 1;
    submit.desc_ptr  = desc_va;
    ioctl(gc_fd, 0xC0108102, &submit);

    usleep(500000); // 500ms settle — same as JS nanosleep_ms(500)
}

// Map victim buffer's GPU PTE to point at target physical page
static void gpu_remap_victim(u64 target_phys) {
    u64 new_ptbe = cleared_ptbe_for_ro | (target_phys & ~(GPU_DMA_SIZE - 1));
    u64 val = 0;
    kernel_copyout(victim_ptbe_va, &val, 8);
    val = new_ptbe;
    // Write new PTE via kernel_copyout inverse — use pipe kwrite primitive
    // kernel_copyout is read; for write you need your kwrite from the jailbreak
    // This assumes you've wired up kernel_copyin or equivalent
    kernel_copyin(&val, victim_ptbe_va, 8);

    // Re-protect victim as RW so GPU can write into it
    mprotect((void *)(uintptr_t)victim_va, GPU_DMA_SIZE,
             PROT_READ | PROT_WRITE);
}

int gpu_dma_setup(u64 curproc, u64 proc_vmspace_off) {
    gc_fd = open("/dev/gc", O_RDWR);
    if (gc_fd < 0) { NOTIFY("gpu: open /dev/gc failed"); return -1; }

    g_dmap_base = resolve_dmap_and_cr3(&g_kernel_cr3);
    if (!g_dmap_base || !g_kernel_cr3) {
        NOTIFY("gpu: failed to resolve dmap/cr3");
        return -1;
    }
    NOTIFY("gpu: dmap_base=0x%llx cr3=0x%llx", g_dmap_base, g_kernel_cr3);

    // Allocate 2x 2MB direct memory regions
    u64 victim_phys = 0, transfer_phys = 0;
    u64 sz = GPU_DMA_SIZE;

    // sceKernelAllocateMainDirectMemory(size, alignment, type, out_phys)
    extern int sceKernelAllocateMainDirectMemory(size_t, size_t, int, u64 *);
    extern int sceKernelMapNamedDirectMemory(u64 *, size_t, int, int, u64, size_t, const char *);

    if (sceKernelAllocateMainDirectMemory(sz, sz, 1, &victim_phys) != 0 ||
        sceKernelAllocateMainDirectMemory(sz, sz, 1, &transfer_phys) != 0) {
        NOTIFY("gpu: dmem alloc failed"); return -1;
    }

    int prot_rw = PROT_READ | PROT_WRITE;
    if (sceKernelMapNamedDirectMemory(&victim_va,   sz, prot_rw, 0, victim_phys,   sz, "vic") != 0 ||
        sceKernelMapNamedDirectMemory(&transfer_va, sz, prot_rw, 0, transfer_phys, sz, "xfr") != 0) {
        NOTIFY("gpu: dmem map failed"); return -1;
    }

    // Get GPU VMID and walk GPU page tables to find victim's PTE
    u32 vmid = get_curproc_vmid(curproc, proc_vmspace_off);
    u64 gvms  = get_gvmspace(vmid);
    u64 start = kread64(gvms + GVMSPACE_START_VA);

    // relative VA within GPU address space
    u64 rel_va = victim_va - start;
    u64 page_size = 0;
    victim_ptbe_va = gpu_walk_pt(vmid, rel_va, &page_size);

    if (!victim_ptbe_va || page_size != GPU_DMA_SIZE) {
        NOTIFY("gpu: PTE walk failed (ptbe=0x%llx sz=0x%llx)", victim_ptbe_va, page_size);
        return -1;
    }

    // Make victim RO, save the cleared PTE for remapping later
    mprotect((void *)(uintptr_t)victim_va, GPU_DMA_SIZE, PROT_READ);
    u64 ro_ptbe = kread64(victim_ptbe_va);
    u64 victim_real_pa = virt_to_phys(victim_va, g_kernel_cr3);
    cleared_ptbe_for_ro = ro_ptbe & ~victim_real_pa;

    NOTIFY("gpu: setup ok vmid=%u ptbe=0x%llx", vmid, victim_ptbe_va);
    return 0;
}

// Main GPU-DMA dump replacing dump_to_file
int dump_to_file_gpu(const char *path, u64 kdata_base, size_t total_size,
                     u64 curproc, u64 proc_vmspace_off) {
    if (gpu_dma_setup(curproc, proc_vmspace_off) != 0)
        return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) { NOTIFY("gpu dump: open failed"); return -1; }

    void *read_buf = malloc(GPU_DMA_SIZE);
    if (!read_buf) { close(fd); return -1; }

    size_t offset = 0, written_total = 0;

    while (offset < total_size) {
        size_t to_read = total_size - offset;
        if (to_read > GPU_DMA_SIZE) to_read = GPU_DMA_SIZE;

        u64 target_va   = kdata_base + offset;
		u64 target_phys = virt_to_phys(target_va, g_kernel_cr3);
        if (!target_phys) {
            NOTIFY("gpu dump: virt_to_phys failed at 0x%llx, skipping", target_va);
            offset += to_read;
            continue;
        }

        // Remap victim GPU PTE -> target physical page
        gpu_remap_victim(target_phys);

        // GPU DMA: victim_va (now mapped to target_phys) -> transfer_va
        gpu_submit_dma(transfer_va, victim_va, (u32)to_read);

        // Read from transfer_va into our local buffer
        memcpy(read_buf, (void *)(uintptr_t)transfer_va, to_read);

        ssize_t written = write(fd, read_buf, to_read);
        if (written <= 0) {
            NOTIFY("gpu dump: write failed at offset 0x%zx", offset);
            break;
        }

        written_total += written;
        offset        += to_read;

        if ((offset % (16 * 1024 * 1024)) == 0)
            NOTIFY("gpu dump: %zu MB written...", written_total / (1024 * 1024));
    }

    free(read_buf);
    close(fd);
    NOTIFY("gpu dump: complete — %zu MB", written_total / (1024 * 1024));
    return written_total > 0 ? 0 : -1;
}
