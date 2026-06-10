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
#include <stdarg.h>
#include <sys/elf64.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>

#define PAGE_SIZE_4K    0x1000ULL
#define DMAP_ADDR_MASK  0x000ffffffffff000ULL
#define DUMP_FILE       "/data/kdata.bin"
#define GAP_LIMIT       16
#define MAX_SCAN        0x10000000UL

#define notify(...) notify_(__VA_ARGS__)

#if !defined(_countof)
    #define _countof(a) (sizeof(a) / sizeof(*a))
#endif
#if !defined(_countof_1)
    #define _countof_1(a) (_countof(a) - 1)
#endif

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

static u64 g_dmap_base  = 0;
static u64 g_kernel_cr3 = 0;

static u64 kread64(u64 addr)
{
    u64 v = 0;
    kernel_copyout(addr, &v, 8);
    return v;
}

static void notify_(const char* fmt, ...)
{
    struct notify_request
    {
        char useless1[45];
        char message[1024];
        char useless2[2051];
    } buf = {};

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf.message, _countof_1(buf.message), fmt, args);
    va_end(args);

    size_t len = strlen(buf.message);
    while (len > 0 && buf.message[len - 1] == '\n')
        buf.message[--len] = '\0';

    extern int sceKernelSendNotificationRequest(const size_t, const struct notify_request*, const size_t, const int);
    sceKernelSendNotificationRequest(0, &buf, sizeof(buf), 0);
}

static u64 get_pmap_store_offset(u32 fw)
{
    switch (fw & 0xffff0000)
    {
    case 0x1000000: case 0x1010000: case 0x1020000:
    case 0x1050000: case 0x1100000: case 0x1110000:
    case 0x1120000: case 0x1130000: case 0x1140000:
        return 0x01D28B78ULL;
    case 0x2000000: case 0x2200000: case 0x2250000:
    case 0x2260000: case 0x2300000: case 0x2500000:
    case 0x2700000:
        return 0x01CF0EF8ULL;
    case 0x9000000: case 0x9050000: case 0x9200000:
    case 0x9400000: case 0x9600000:
        return 0x02D28B78ULL;
    case 0x10000000: case 0x10010000: case 0x10200000:
    case 0x10400000: case 0x10600000:
        return 0x02CF0EF8ULL;
    case 0x11000000: case 0x11200000: case 0x11400000:
    case 0x11600000:
        return 0x02E04F18ULL;
    case 0x12000000: case 0x12020000: case 0x12200000:
    case 0x12400000: case 0x12600000: case 0x12700000:
        return 0x02E1CFB8ULL;
    default:
        return 0;
    }
}

static u64 resolve_dmap_base(u64 pmap_store)
{
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

static u64 find_data_segment_size(u64 data_vaddr)
{
    u64 offset = 0;
    int gap    = 0;

    while (offset < MAX_SCAN)
    {
        u64 pa = vaddr_to_paddr(data_vaddr + offset);

        if (!pa)
        {
            gap++;
            if (gap >= GAP_LIMIT)
            {
                offset -= (u64)(gap - 1) * PAGE_SIZE_4K;
                return offset;
            }
        }
        else
        {
            gap = 0;
        }

        offset += PAGE_SIZE_4K;

        if ((offset % (32 * 1024 * 1024)) == 0)
            printf("[size] scanned %zu MB...\n", offset >> 20);
    }

    return MAX_SCAN;
}

static int dump_segment_dmap(u64 vaddr, size_t size, int out_fd)
{
    u8 page[PAGE_SIZE_4K];
    size_t offset    = 0;
    size_t gap_pages = 0;

    while (offset < size)
    {
        size_t to_write = size - offset;
        if (to_write > PAGE_SIZE_4K)
            to_write = PAGE_SIZE_4K;

        u64 page_vaddr = (vaddr + offset) & ~(PAGE_SIZE_4K - 1);
        u64 page_off   = (vaddr + offset) - page_vaddr;
        u64 pa         = vaddr_to_paddr(page_vaddr);

        if (pa)
        {
            if (gap_pages)
            {
                printf("[dump] gap: %zu unmapped pages before 0x%lx\n", gap_pages, page_vaddr);
                gap_pages = 0;
            }
            kernel_copyout(g_dmap_base + pa, page, PAGE_SIZE_4K);
        }
        else
        {
            memset(page, 0, PAGE_SIZE_4K);
            gap_pages++;
        }

        ssize_t written = write(out_fd, page + page_off, to_write);
        if (written != (ssize_t)to_write)
        {
            printf("[dump] write failed at offset 0x%zx errno=%d\n", offset, errno);
            return -1;
        }

        offset += to_write;

        if ((offset % (16 * 1024 * 1024)) == 0)
            printf("[dump] progress: %zu / %zu MB\n", offset >> 20, size >> 20);
    }

    if (gap_pages)
        printf("[dump] gap: %zu unmapped pages at end of segment\n", gap_pages);

    return 0;
}

int main(void)
{
    u32 fw          = kernel_get_fw_version();
    u8  major       = (fw >> 24) & 0xFF;
    u8  minor       = (fw >> 16) & 0xFF;
    u64 data_vaddr  = (u64)KERNEL_ADDRESS_DATA_BASE;

    printf("[init] FW %x.%02x\n", major, minor);

    u64 pmap_off   = get_pmap_store_offset(fw);
    if (!pmap_off)
    {
        printf("[init] unsupported firmware\n");
        notify("unsupported firmware %x.%02x", major, minor);
        return 1;
    }

    u64 pmap_store = data_vaddr + pmap_off;
    g_dmap_base    = resolve_dmap_base(pmap_store);

    if (!g_dmap_base)
    {
        printf("[init] dmap resolve failed\n");
        notify("dmap resolve failed");
        return 1;
    }

    u64 data_pa = vaddr_to_paddr(data_vaddr);
    if (!data_pa)
    {
        printf("[init] vaddr_to_paddr failed for data_base\n");
        notify("vaddr_to_paddr failed for data_base");
        return 1;
    }

    printf("[init] data_base=0x%lx pa=0x%lx\n", data_vaddr, data_pa);
    printf("[init] dmap_base=0x%lx cr3=0x%lx\n", g_dmap_base, g_kernel_cr3);
    printf("[init] pmap_store=0x%lx (offset=0x%lx)\n", pmap_store, pmap_off);
    printf("[size] scanning segment size...\n");

    u64 data_size = find_data_segment_size(data_vaddr);

    printf("[size] segment: vaddr=0x%lx size=0x%lx (%zu MB)\n", data_vaddr, data_size, data_size >> 20);
    notify("kdata dumper - by darkness\nFW %x.%02x\ndata_base=0x%lx\ndmap_base=0x%lx\nkernel_cr3=0x%lx\npmap_offset=0x%lx\nkdata_segment_size=%zu MB", major, minor, data_vaddr, g_dmap_base, g_kernel_cr3, pmap_off, data_size >> 20);

    int fd = open(DUMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0)
    {
        printf("[init] open failed errno=%d\n", errno);
        notify("open failed errno=%d", errno);
        return 1;
    }

    printf("[dump] starting dump to %s\n", DUMP_FILE);
    int result = dump_segment_dmap(data_vaddr, (size_t)data_size, fd);

    close(fd);

    printf(result == 0 ? "[done] kdata dump complete\n" : "[done] kdata dump failed\n");
    notify(result == 0 ? "kdata dump complete" : "kdata dump failed");

    return result;
}
