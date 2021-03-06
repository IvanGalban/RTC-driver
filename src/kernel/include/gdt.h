#ifndef __GDT_H__
#define __GDT_H__

#include <typedef.h>

/* For a reference to the layout of segment descriptors check
 * "IA-32 Intel Architecture Software Developer's Manual,
 * Volume 3: System Programming Guide", section 3. */

/*****************************************************************************
 * Segment descriptors                                                       *
 *****************************************************************************/
typedef u64                               gdt_descriptor_t;
typedef u16                               gdt_selector_t;

#define GDT_NULL_ENTRY                    0x0000000000000000

/* General flags */
#define GDT_GRANULARITY_4K                0x0080000000000000
#define GDT_GRANULARITY_1B                0x0000000000000000

#define GDT_OP_SIZE_16                    0x0000000000000000
#define GDT_OP_SIZE_32                    0x0040000000000000

#define GDT_PRESENT                       0x0000800000000000
#define GDT_NOT_PRESENT                   0x0000000000000000

#define GDT_DPL_0                         0x0000000000000000
#define GDT_DPL_1                         0x0000200000000000
#define GDT_DPL_2                         0x0000400000000000
#define GDT_DPL_3                         0x0000600000000000
#define GDT_DPL_KERNEL                    GDT_DPL_0
#define GDT_DPL_USER                      GDT_DPL_3

/* Code and data segments */
#define GDT_DESC_TYPE_CODE_DATA           0x0000100000000000

#define GDT_DATA_SEGMENT                  0x0000000000000000
#define GDT_DATA_ACCESSED                 0x0000010000000000
#define GDT_DATA_READ_ONLY                0x0000000000000000
#define GDT_DATA_READ_WRITE               0x0000020000000000
#define GDT_DATA_EXPAND_UP                0x0000000000000000
#define GDT_DATA_EXPAND_DOWN              0x0000040000000000

#define GDT_CODE_SEGMENT                  0x0000080000000000
#define GDT_CODE_ACCESSED                 0x0000010000000000
#define GDT_CODE_EXEC_ONLY                0x0000000000000000
#define GDT_CODE_EXEC_READ                0x0000020000000000
#define GDT_CODE_NON_CONFORMING           0x0000000000000000
#define GDT_CODE_CONFORMING               0x0000040000000000

/* System segments */
#define GDT_DESC_TYPE_SYSTEM              0x0000000000000000

#define GDT_SYSTEM_RESERVED               0x0000000000000000
#define GDT_SYSTEM_TSS_16                 0x0000010000000000
#define GDT_SYSTEM_LDT                    0x0000020000000000
#define GDT_SYSTEM_TSS_16_BSY             0x0000030000000000
#define GDT_SYSTEM_CALL_GATE_16           0x0000040000000000
#define GDT_SYSTEM_TASK_GATE_16           0x0000050000000000
#define GDT_SYSTEM_INT_GATE_16            0x0000060000000000
#define GDT_SYSTEM_TRAP_GATE_16           0x0000070000000000
#define GDT_SYSTEM_RESERVED_2             0x0000080000000000
#define GDT_SYSTEM_TSS_32                 0x0000090000000000
#define GDT_SYSTEM_RESERVED_3             0x00000a0000000000
#define GDT_SYSTEM_TSS_32_BSY             0x00000b0000000000
#define GDT_SYSTEM_CALL_GATE_32           0x00000c0000000000
#define GDT_SYSTEM_RESERVED_4             0x00000d0000000000
#define GDT_SYSTEM_INT_GATE_32            0x00000e0000000000
#define GDT_SYSTEM_TRAP_GATE_32           0x00000f0000000000

/*****************************************************************************
 * Segment selectors                                                         *
 *****************************************************************************/
#define GDT_NULL_SEGMENT                  0x00
#define GDT_KERNEL_CODE_SEGMENT           0x08
#define GDT_KERNEL_DATA_SEGMENT           0x10
#define GDT_TSS                           0x18
/* Other descriptors will be allocated dynamically. */

#define GDT_RPL_KERNEL                    0x00
#define GDT_RPL_USER                      0x03

#define GDT_SEGMENT_SELECTOR(S, RPL)      ((u16)((u16)(S) | (u16)(RPL)))

/*****************************************************************************
 * For mem.c only                                                            *
 *****************************************************************************/
void gdt_setup(u32);

/*****************************************************************************
 * API                                                                       *
 *****************************************************************************/
gdt_selector_t gdt_alloc(void * base, u32 limit, u64 flags);
void gdt_dealloc(gdt_selector_t);
gdt_descriptor_t gdt_get(gdt_selector_t);
void * gdt_base(gdt_descriptor_t);
u32 gdt_limit(gdt_descriptor_t);

#endif
