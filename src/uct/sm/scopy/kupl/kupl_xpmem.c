/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <ucs/type/status.h>
#include <ucs/memory/rcache.h>
#include <ucs/sys/checker.h>

#include <limits.h>
#include <ucs/sys/sys.h>
#include <ucs/sys/math.h>

#include "kupl_xpmem.h"

static UCS_F_ALWAYS_INLINE size_t
uct_kupl_xpmem_rcache_region_length(uct_kupl_xpmem_mem_region_t *xpmem_region)
{
    return xpmem_region->super.super.end - xpmem_region->super.super.start;
}

static inline void touch_page(void *addr)
{
    volatile char *temp_ptr = (volatile char *)addr;
    char value = temp_ptr[0];
    temp_ptr[0] = value;
    return;
}

static ucs_status_t uct_kupl_xpmem_rcache_mem_reg(void *context, ucs_rcache_t *rcache, void *arg,
                                                  ucs_rcache_region_t *region, uint16_t flags)
{
    xpmem_apid_t apid = (xpmem_apid_t)context;
    uct_kupl_xpmem_mem_region_t *mem_region = ucs_derived_of(region, uct_kupl_xpmem_mem_region_t);
    struct xpmem_addr addr;
    size_t length;
    size_t page_size = ucs_get_page_size();

    addr.apid   = apid;
    addr.offset = mem_region->super.super.start;
    length      = uct_kupl_xpmem_rcache_region_length(mem_region);

    mem_region->attach_address = kupl_xpmem_attach_func(addr, length, NULL);
    VALGRIND_MAKE_MEM_DEFINED(&mem_region->attach_address,
                              sizeof(mem_region->attach_address));
    if (mem_region->attach_address == MAP_FAILED) {
        ucs_error("failed to attach xpmem apid 0x%lx offset 0x%lx length %zu: %m",
                  (unsigned long)addr.apid, addr.offset, length);
        return UCS_ERR_IO_ERROR;
    }

    for(unsigned int i = 0; i < (length + page_size - 1)/page_size; i++) {
        char *current_begin = mem_region->attach_address + i * page_size;
        touch_page(current_begin);
    }

    ucs_trace("xpmem attached apid 0x%lx offset 0x%lx length %zu at %p",
              (unsigned long)addr.apid, addr.offset, length,
              mem_region->attach_address);

    VALGRIND_MAKE_MEM_DEFINED(mem_region->attach_address, length);
    return UCS_OK;
}

static void uct_kupl_xpmem_rcache_mem_dereg(void *context, ucs_rcache_t *rcache,
                                            ucs_rcache_region_t *region)
{
    uct_kupl_xpmem_mem_region_t *xpmem_region = ucs_derived_of(region, uct_kupl_xpmem_mem_region_t);
    int ret;

    ret = kupl_xpmem_detach_func(xpmem_region->attach_address);
    if (ret < 0) {
        ucs_warn("Failed to xpmem_detach: %m");
    }

    xpmem_region->attach_address = NULL;
    xpmem_region->mapped_address = NULL;
}

static void uct_xpmem_rcache_dump_region(void *context, ucs_rcache_t *rcache,
                                         ucs_rcache_region_t *region, char *buf,
                                         size_t max)
{
    xpmem_apid_t apid = (xpmem_apid_t)context;
    uct_kupl_xpmem_mem_region_t *xpmem_region = ucs_derived_of(region, uct_kupl_xpmem_mem_region_t);

    snprintf(buf, max, "apid 0x%llx attach_addr %p",
             apid, xpmem_region->attach_address);
}

static ucs_rcache_ops_t uct_kupl_xpmem_rcache_ops = {
    .mem_reg     = uct_kupl_xpmem_rcache_mem_reg,
    .mem_dereg   = uct_kupl_xpmem_rcache_mem_dereg,
    .dump_region = uct_xpmem_rcache_dump_region
};

ucs_status_t uct_kupl_xpmem_create_rcache(ucs_rcache_t **rcache, xpmem_apid_t apid)
{
    ucs_rcache_params_t rcache_params;
    ucs_status_t status;

    rcache_params.region_struct_size = sizeof(uct_kupl_xpmem_mem_region_t);
    rcache_params.alignment          = ucs_get_page_size();
    rcache_params.max_alignment      = ucs_get_page_size();
    rcache_params.ucm_events         = 0;
    rcache_params.ucm_event_priority = 0;
    rcache_params.ops                = &uct_kupl_xpmem_rcache_ops;
    rcache_params.context            = (void *)(uintptr_t)apid;
    rcache_params.flags              = UCS_RCACHE_FLAG_NO_PFN_CHECK;
    rcache_params.max_regions        = ULONG_MAX;
    rcache_params.max_size           = SIZE_MAX;

    status = ucs_rcache_create(&rcache_params, "kupl_xpmem_remote_mem",
                               ucs_stats_get_root(), rcache);
    if (status != UCS_OK) {
        ucs_error("failed to create xpmem remote cache: %s",
                  ucs_status_string(status));
        return status;
    }
    return UCS_OK;
}

inline void uct_kupl_xpmem_destroy_rcache(ucs_rcache_t *rcache)
{
    ucs_rcache_destroy(rcache);
    return;
}

ucs_status_t uct_kupl_xpmem_mem_attach(void *remote_address, size_t length,
                                       ucs_rcache_t *rcache, uct_kupl_xpmem_mem_region_t **mem_region)
{
    uintptr_t start, end;
    ucs_status_t status;
    void *offset;
    ucs_rcache_region_t *rcache_region = NULL;
    uct_kupl_xpmem_mem_region_t *mem_reg = NULL;
    start = ucs_align_down_pow2((size_t)remote_address, (size_t)ucs_get_page_size());
    end   = ucs_align_up_pow2((size_t)remote_address + length, (size_t)ucs_get_page_size());

    status = ucs_rcache_get(rcache, (void*)start, end - start,
                            PROT_READ|PROT_WRITE, NULL, &rcache_region);
    if (status != UCS_OK) {
        return status;
    }

    mem_reg = ucs_derived_of(rcache_region, uct_kupl_xpmem_mem_region_t);

    if ((uintptr_t)remote_address >= mem_reg->super.super.start && 
        (uintptr_t)remote_address < mem_reg->super.super.end){
        offset = remote_address - mem_reg->super.super.start;
    } else {
        ucs_error("remote_address is not in correct region");
        return UCS_ERR_OUT_OF_RANGE;
    }
    mem_reg->mapped_address = UCS_PTR_BYTE_OFFSET(mem_reg->attach_address, offset);

    *mem_region = mem_reg;

    return UCS_OK;
}

inline void
uct_kupl_xpmem_mem_detach(ucs_rcache_t *rcache,
                          uct_kupl_xpmem_mem_region_t *xpmem_region)
{
    ucs_rcache_region_put(rcache, &xpmem_region->super);
    return;
}