/*
 * kdata-dumper: main.h
 */

// General
#define OUTPUT_PATH  "/data/kdata.bin"
#define CHUNK_SIZE (4 * 1024 * 1024)
#define FW_POST_700_DATA_SIZE (134 * 1024 * 1024)
#define FW_PRE_700_DATA_SIZE  (83  * 1024 * 1024)

// Hardcoded 11.20 values
#define DMAP_BASE           0xffffb65500000000ULL
#define KERNEL_CR3          0x000000000ea84000ULL
#define VMSPACE_VM_PMAP_OFF 0x1d8
#define VMSPACE_VM_VMID_OFF 0x1ec
#define DATA_BASE           0xffffffffcb980000ULL
#define DATA_BASE_GVMSPACE  (DATA_BASE + 0x02E66570ULL)
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
