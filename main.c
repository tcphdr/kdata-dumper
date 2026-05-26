/*
 * kdata-dumper: main.c
 */

#include "main.h"

static void notify(const char *fmt, ...) {
    char buf[3075];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.message, buf, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static u32 get_fw_version(void) {
    u32 fw = 0;
    size_t sz = sizeof(fw);
    sysctlbyname("kern.sdk_version", &fw, &sz, NULL, 0);
    return fw;
}

static size_t get_kdata_size(u64 kdata_base) {
    u64 kern_base = (u64)KERNEL_ADDRESS_TEXT_BASE;
    Elf64_Ehdr ehdr;
    if (kernel_copyout(kern_base, &ehdr, sizeof(ehdr)) != 0) {
        NOTIFY("kdata-dumper: failed to read kernel ELF header");
        return 0;
    }
    if (ehdr.e_ident[0] != 0x7f || ehdr.e_phnum == 0) {
        NOTIFY("kdata-dumper: invalid ELF header");
        return 0;
    }
    size_t phdrs_size = sizeof(Elf64_Phdr) * ehdr.e_phnum;
    Elf64_Phdr *phdrs = malloc(phdrs_size);
    if (!phdrs) return 0;
    if (kernel_copyout(kern_base + ehdr.e_phoff, phdrs, phdrs_size) != 0) {
        free(phdrs);
        return 0;
    }
    size_t result = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_vaddr == kdata_base) {
            result = phdrs[i].p_filesz;
            NOTIFY("kdata-dumper: ELF phdr match: %zu MB", result / (1024*1024));
            break;
        }
    }
    free(phdrs);
    return result;
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    u32 fw = get_fw_version();
    u64 kdata_base = (u64)KERNEL_ADDRESS_DATA_BASE;

    NOTIFY("kdata-dumper: fw=0x%08x base=0x%lx", fw, kdata_base);

    if (setuid(0) != 0) {
        NOTIFY("kdata-dumper: setuid failed");
        return -1;
    }
    NOTIFY("kdata-dumper: setuid ok");

    // Use SDK-provided text base to size the dump via ELF phdrs
    size_t size = get_kdata_size(kdata_base);
    if (size == 0) {
        NOTIFY("kdata-dumper: ELF size detection failed, using heuristic");
        size = (fw >= 0x07000000) ? FW_POST_700_DATA_SIZE : FW_PRE_700_DATA_SIZE;
    }
    NOTIFY("kdata-dumper: dumping %zu MB via GPU-DMA...", size / (1024 * 1024));

    intptr_t curproc = kernel_get_proc(getpid());
    if (!curproc) {
        NOTIFY("kdata-dumper: kernel_get_proc failed");
        return -1;
    }

    if (dump_to_file_gpu(OUTPUT_PATH, kdata_base, size,
                         (u64)curproc,
                         (u64)KERNEL_OFFSET_PROC_P_VMSPACE) != 0)
        return -1;

    NOTIFY("kdata-dumper: done");
    return 0;
}
