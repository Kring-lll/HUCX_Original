/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef UCT_KUPL_XPMEM_H
#define UCT_KUPL_XPMEM_H

#include <ucs/memory/rcache.h>
#include <ucs/debug/log.h>
#include "kupl_xpmem_adaptor.h"

typedef struct uct_kupl_xpmem_mem_region {
    ucs_rcache_region_t     super;
    void *attach_address;
    void *mapped_address;       // mapped remote address
} uct_kupl_xpmem_mem_region_t;

ucs_status_t uct_kupl_xpmem_create_rcache(ucs_rcache_t **rcache, xpmem_apid_t apid);
void uct_kupl_xpmem_destroy_rcache(ucs_rcache_t *rcache);
ucs_status_t uct_kupl_xpmem_mem_attach(void *remote_address, size_t length,
                                       ucs_rcache_t *rcache, uct_kupl_xpmem_mem_region_t **mem_region);
void uct_kupl_xpmem_mem_detach(ucs_rcache_t *rcache,
                               uct_kupl_xpmem_mem_region_t *xpmem_region);

#endif  /* UCT_KUPL_XPMEM_H */