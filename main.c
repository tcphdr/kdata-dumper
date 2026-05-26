#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/elf64.h>
#include <ps5/kernel.h>

#include <main.h>

//#define OUTPUT_PATH           "/data/kdata.bin"
//#define CHUNK_SIZE (4 * 1024 * 1024)
//#define KERNEL_DATA_BASE_11_20 0xffffffff945e0000

//#define FW_POST_700_DATA_SIZE (134 * 1024 * 1024)
//#define FW_PRE_700_DATA_SIZE  (83  * 1024 * 1024)

//typedef uint64_t u64;
//typedef uint32_t u32;
//typedef uint8_t  u8;
//typedef unsigned long ulong;

//typedef struct {
//    char useless[45];
//    char message[3075];
//} notify_request_t;

//int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

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

/*static size_t get_kdata_size(u64 kdata_base) {
#ifndef KERNEL_ADDRESS_TEXT_BASE
    NOTIFY("kdata-dumper: KERNEL_ADDRESS_TEXT_BASE unavailable, skipping ELF size detection");
    return 0;
#else
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
    if (!phdrs) {
        NOTIFY("kdata-dumper: malloc failed for phdrs");
        return 0;
    }
    if (kernel_copyout(kern_base + ehdr.e_phoff, phdrs, phdrs_size) != 0) {
        NOTIFY("kdata-dumper: failed to read program headers");
        free(phdrs);
        return 0;
    }
    size_t result = 0;
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_vaddr == kdata_base) {
            result = phdrs[i].p_filesz;
            NOTIFY("kdata-dumper: ELF phdr match: size=%zu MB", result / (1024 * 1024));
            break;
        }
    }
    free(phdrs);
    return result;
#endif
}

static int dump_to_file(const char *path, unsigned long base, size_t size) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        NOTIFY("kdata-dumper: open failed");
        printf("[-] open failed\n");
        return -1;
    }

    void *chunk = malloc(CHUNK_SIZE);
    if (!chunk) {
        NOTIFY("kdata-dumper: malloc failed");
        close(fd);
        return -1;
    }

    size_t offset = 0;
    size_t written_total = 0;

    while (offset < size) {
        size_t to_read = size - offset;
        if (to_read > CHUNK_SIZE) to_read = CHUNK_SIZE;

        if (kernel_copyout(base + offset, chunk, to_read) != 0) {
            NOTIFY("kdata-dumper: copyout stopped at offset 0x%zx (%zu MB)", offset, offset / (1024 * 1024));
            printf("[!] copyout stopped at 0x%zx\n", offset);
            break;
        }

        ssize_t written = write(fd, chunk, to_read);
        if (written != (ssize_t)to_read) {
            NOTIFY("kdata-dumper: write failed at offset 0x%zx", offset);
            printf("[-] write failed at 0x%zx\n", offset);
            break;
        }

        written_total += written;
        offset += to_read;
    }

    free(chunk);
    close(fd);

    if (written_total == 0) {
        NOTIFY("kdata-dumper: nothing written");
        return -1;
    }

    NOTIFY("kdata-dumper: wrote %zu MB", written_total / (1024 * 1024));
    printf("[+] wrote %zu MB\n", written_total / (1024 * 1024));
    return 0;
}*/

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    u32 fw = get_fw_version();
    u64 kdata_base = (u64)KERNEL_DATA_BASE_11_20;

    NOTIFY("kdata-dumper: fw=0x%08x base=0x%lx", fw, kdata_base);
    printf("[*] firmware: 0x%08x\n", fw);
    printf("[*] kdata_base=0x%lx\n", kdata_base);

    if (setuid(0) != 0) {
        NOTIFY("kdata-dumper: setuid failed");
        return -1;
    }
    NOTIFY("kdata-dumper: setuid ok");

    size_t size = get_kdata_size(kdata_base);
    if (size == 0) {
        NOTIFY("kdata-dumper: ELF detection failed, using firmware heuristic");
        size = (fw >= 0x07000000) ? FW_POST_700_DATA_SIZE : FW_PRE_700_DATA_SIZE;
    }
    printf("[*] size: %zu MB\n", size / (1024 * 1024));

    NOTIFY("kdata-dumper: dumping %zu MB...", size / (1024 * 1024));
    if (dump_to_file(OUTPUT_PATH, kdata_base, size) != 0)
        return -1;

    NOTIFY("kdata-dumper: done");
    printf("[+] done\n");
    return 0;
}
