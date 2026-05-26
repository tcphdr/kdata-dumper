/*
 * kdata-dumper: gpu.h
 */
#pragma once

#include "main.h"

#define GPU_PDE_ADDR_MASK   0x0000ffffffffffc0ULL
#define GPU_VALID           (1ULL << 0)
#define GPU_IS_PTE          (1ULL << 54)
#define GPU_TF              (1ULL << 56)
#define GPU_FRAG_MASK       (0x1fULL << 59)
#define GPU_FRAG_SHIFT      59
#define GPU_DMA_SIZE        0x200000ULL

#define PM4_TYPE3_HEADER(opcode, count) \
    ((2u << 30) | (((count) - 1u) << 16) | (((opcode) & 0xff) << 8) | (1u << 1))

int gpu_dma_setup(u64 curproc, u64 proc_vmspace_off);
static u64 gpu_walk_pt(u32 vmid, u64 rel_va, u64 *page_size_out);
static u64 gpu_walk_pt(u32 vmid, u64 rel_va, u64 *page_size_out);
static u64 resolve_dmap_and_cr3(u64 *cr3_out);
static u64 kread64(u64 addr);
static u64 phys_to_dmap(u64 phys);
static u64 virt_to_phys(u64 vaddr, u64 cr3);
static u32 get_curproc_vmid(u64 curproc, u64 proc_vmspace_off);
static u64 get_gvmspace(u32 vmid);
static u64 get_pdb2_addr(u32 vmid);
static void gpu_submit_dma(u64 dst_va, u64 src_va, u32 size_bytes);
static void gpu_remap_victim(u64 target_phys);
