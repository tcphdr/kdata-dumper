/*
 * kdata-dumper: main.h
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/elf64.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>

#define OUTPUT_PATH           "/data/kdata.bin"
#define CHUNK_SIZE            (4 * 1024 * 1024)
#define FW_POST_700_DATA_SIZE (134 * 1024 * 1024)
#define FW_PRE_700_DATA_SIZE  (83  * 1024 * 1024)

typedef uint64_t u64;
typedef uint32_t u32;

typedef struct {
    char useless[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

void notify(const char *fmt, ...);

#define NOTIFY(fmt, ...) notify(fmt, ##__VA_ARGS__)
