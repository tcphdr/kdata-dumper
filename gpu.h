/*
 * kdata-dumper: gpu.h
 */
#define GPU_PDE_ADDR_MASK   0x0000ffffffffffc0ULL
#define GPU_VALID           (1ULL << 0)
#define GPU_IS_PTE          (1ULL << 54)
#define GPU_TF              (1ULL << 56)
#define GPU_FRAG_MASK       (0x1fULL << 59)
#define GPU_FRAG_SHIFT      59
#define GPU_DMA_SIZE        0x200000ULL  // 2MB chunks

// PM4 packet building
#define PM4_TYPE3_HEADER(opcode, count) \
    ((2u << 30) | (((count) - 1u) << 16) | (((opcode) & 0xff) << 8) | (1u << 1))
