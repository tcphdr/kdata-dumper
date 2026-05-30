/*
 * kdata-dumper: main.c
 */

#include "main.h"

void notify(const char *fmt, ...) {
    char buf[3075];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    klog_printf("kdata-dumper: %s\n", buf);
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.message, buf, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static size_t get_kdata_size(u64 kdata_base) {
    u64 kern_base = (u64)KERNEL_ADDRESS_TEXT_BASE;
    Elf64_Ehdr ehdr;
    if (kernel_copyout(kern_base, &ehdr, sizeof(ehdr)) != 0)
        return 0;
    if (ehdr.e_ident[0] != 0x7f || ehdr.e_phnum == 0)
        return 0;

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
            break;
        }
    }

    free(phdrs);
    return result;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    if (setuid(0) != 0) {
        NOTIFY("setuid failed");
        return -1;
    }

    u32 fw = kernel_get_fw_version();
    u64 kdata_base = (u64)KERNEL_ADDRESS_DATA_BASE;
    NOTIFY("fw=0x%08x base=0x%lx", fw, kdata_base);

    size_t total = get_kdata_size(kdata_base);
    if (total == 0) {
        NOTIFY("ELF phdr size detection failed, using heuristic");
        total = (fw >= 0x07000000) ? FW_POST_700_DATA_SIZE : FW_PRE_700_DATA_SIZE;
    }
    NOTIFY("dump size: %zu MB", total / (1024 * 1024));

    int fd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        NOTIFY("open failed");
        return -1;
    }

    void *chunk = malloc(CHUNK_SIZE);
    if (!chunk) {
        NOTIFY("malloc failed");
        close(fd);
        return -1;
    }

    size_t offset = 0;
    int ret = 0;
    while (offset < total) {
        size_t to_read = total - offset;
        if (to_read > CHUNK_SIZE) to_read = CHUNK_SIZE;

        if (kernel_copyout(kdata_base + offset, chunk, to_read) != 0) {
            NOTIFY("copyout failed at offset 0x%zx", offset);
            ret = -1;
            break;
        }

        ssize_t written = write(fd, chunk, to_read);
        if (written != (ssize_t)to_read) {
            NOTIFY("write failed at offset 0x%zx", offset);
            ret = -1;
            break;
        }

        offset += to_read;
        if ((offset % (16 * 1024 * 1024)) == 0)
            NOTIFY("progress: %zu / %zu MB",
                   offset / (1024 * 1024),
                   total  / (1024 * 1024));
    }

    free(chunk);
    close(fd);

    if (ret == 0)
        NOTIFY("done — %zu MB written to %s", total / (1024 * 1024), OUTPUT_PATH);
    return ret;
}
