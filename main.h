/*
 * kdata-dumper: main.h
 */
#pragma once

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

// General
#define OUTPUT_PATH  "/data/kdata.bin"
#define CHUNK_SIZE (4 * 1024 * 1024)
#define FW_POST_700_DATA_SIZE (134 * 1024 * 1024)
#define FW_PRE_700_DATA_SIZE  (83  * 1024 * 1024)

// Kernel Values (COMMENTED OUT DEFINES NEED TO BE RESOLVED DYNAMICALLY AT RUNTIME)
//#define DMAP_BASE           0xffffb65500000000ULL
//#define KERNEL_CR3          0x000000000ea84000ULL
//#define DATA_BASE           0xffffffffcb980000ULL
#define VMSPACE_VM_PMAP_OFF 0x1d8
#define VMSPACE_VM_VMID_OFF 0x1ec
#define DATA_BASE_GVMSPACE  ((u64)KERNEL_ADDRESS_DATA_BASE + 0x02E66570ULL)
#define SIZEOF_GVMSPACE     0x100
#define GVMSPACE_START_VA   0x08
#define GVMSPACE_SIZE_OFF   0x10
#define GVMSPACE_PAGE_DIR   0x38

// CPU
#define CPU_PG_PHYS_FRAME   0x000ffffffffff000ULL
#define CPU_PG_PS_FRAME     0x000fffffffe00000ULL

// Typedef shortcuts.
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t  u8;
typedef unsigned long ulong;

// Console notifications.
#define NOTIFY(fmt, ...) notify(fmt, ##__VA_ARGS__)

typedef struct {
    char useless[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

// Functions.
void notify(const char *fmt, ...);
int gpu_dma_setup(u64 curproc, u64 proc_vmspace_off);
int dump_to_file_gpu(const char *path, u64 kdata_base, size_t total_size, u64 curproc, u64 proc_vmspace_off);
