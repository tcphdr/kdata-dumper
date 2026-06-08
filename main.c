/*
 * Kernel data section dumper
 * Authored by: darkness
 * Tested On: PS5 FW 11.20
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/elf64.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>

#define PMAP_STORE_OFF  0x02E04F18ULL
#define PAGE_SIZE_4K    0x1000ULL
#define DMAP_ADDR_MASK  0x000ffffffffff000ULL

// NOTE: Dump segment size may be incorrect, just a blind guess according to EchoStretch's repository (https://github.com/echostretch/ps5-kdata-dumper)
#define DUMP_SIZE       0x8800000

// Full path to write dumped data to.
#define DUMP_FILE		"/data/kdata.bin"

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t  u8;

static u64 g_dmap_base  = 0;
static u64 g_kernel_cr3 = 0;

typedef struct {
    u64 base;
    u64 end;
    u32 prot;
    u32 type_flags;
} vm_region_t;

static u64 kread64(u64 addr)
{
    u64 v = 0;
    kernel_copyout(addr, &v, 8);
    return v;
}

static u64 resolve_dmap_base(void)
{
    u64 pmap_store = (u64)KERNEL_ADDRESS_DATA_BASE + PMAP_STORE_OFF;
    u64 pml4 = kread64(pmap_store + 0x20);
    u64 cr3  = kread64(pmap_store + 0x28);

    if (!cr3 || !pml4 || cr3 > pml4)
        return 0;

    g_kernel_cr3 = cr3;

    return pml4 - cr3;
}

static u64 vaddr_to_paddr(u64 vaddr)
{
    u64 pml4e_idx = (vaddr >> 39) & 0x1ff;
    u64 pdpe_idx  = (vaddr >> 30) & 0x1ff;
    u64 pde_idx   = (vaddr >> 21) & 0x1ff;
    u64 pte_idx   = (vaddr >> 12) & 0x1ff;
    u64 entry     = 0;

    kernel_copyout(g_dmap_base + g_kernel_cr3 + pml4e_idx * 8, &entry, 8);

    if (!(entry & 1))
		return 0;

    kernel_copyout(g_dmap_base + (entry & DMAP_ADDR_MASK) + pdpe_idx * 8, &entry, 8);

    if (!(entry & 1))
		return 0;

    kernel_copyout(g_dmap_base + (entry & DMAP_ADDR_MASK) + pde_idx * 8, &entry, 8);

    if (!(entry & 1))
		return 0;

    if (entry & (1ULL << 7))
        return (entry & 0x000fffffffe00000ULL) + (vaddr & 0x1fffff);

    kernel_copyout(g_dmap_base + (entry & DMAP_ADDR_MASK) + pte_idx * 8, &entry, 8);

    if (!(entry & 1))
		return 0;

    return (entry & DMAP_ADDR_MASK) + (vaddr & 0xfff);
}

static int dump_segment_dmap(u64 vaddr, size_t size, int out_fd)
{
    u8 page[PAGE_SIZE_4K];
    size_t offset = 0;

    while (offset < size)
    {
        size_t to_write  = size - offset;

        if (to_write > PAGE_SIZE_4K)
			to_write = PAGE_SIZE_4K;

        u64 page_vaddr = (vaddr + offset) & ~(PAGE_SIZE_4K - 1);
        u64 page_off   = (vaddr + offset) - page_vaddr;
        u64 pa         = vaddr_to_paddr(page_vaddr);

        if (pa)
            kernel_copyout(g_dmap_base + pa, page, PAGE_SIZE_4K);
        else
            memset(page, 0, PAGE_SIZE_4K);

        ssize_t written = write(out_fd, page + page_off, to_write);

        if (written != (ssize_t)to_write)
        {
            klog_printf("write failed at offset 0x%zx errno=%d\n", offset, errno);
            return -1;
        }

        offset += to_write;

        if ((offset % (16 * 1024 * 1024)) == 0)
            klog_printf("progress: %zu / %zu MB\n", offset >> 20, size >> 20);

    }
    return 0;
}

int main(void)
{
    g_dmap_base = resolve_dmap_base();

    if (!g_dmap_base)
    {
        klog_printf("dmap resolve failed\n");
        return 1;
    }

    u64 data_base = (u64)KERNEL_ADDRESS_DATA_BASE;
    u64 pa = vaddr_to_paddr(data_base);

	klog_printf("data_base=0x%lx dmap_base=0x%lx kernel_cr3=0x%lx\n", data_base, g_dmap_base, g_kernel_cr3);

    if (!pa)
    {
        klog_printf("vaddr_to_paddr failed for data_base\n");
        return 1;
    }

    size_t dump_size = DUMP_SIZE;
    klog_printf("dumping 0x%zx bytes to /data/kdata.bin\n", dump_size);

    int fd = open(DUMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0777);

    if (fd < 0)
    {
        klog_printf("open failed errno=%d\n", errno);
        return 1;
    }

    if (dump_segment_dmap(data_base, dump_size, fd) == 0)
    {
        klog_printf("kdata dump complete\n");
    }
    else
    {
        klog_printf("kdata dump failed\n");
    }

    close(fd);

    return 0;
}
