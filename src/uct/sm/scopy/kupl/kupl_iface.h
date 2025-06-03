/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef UCT_KUPL_IFACE_H
#define UCT_KUPL_IFACE_H

#include <uct/base/uct_iface.h>
#include "kupl_md.h"
#include "kupl_adaptor.h"
#include "uct/sm/scopy/base/scopy_iface.h"
#include "kupl_xpmem.h"

#define MAX_KUPL_PUT_SHORT_SIZE 0x2000

typedef struct uct_bcopy_completion {
    uct_completion_t *super;
    uct_completion_t *new_comp;
    void *buffer;
	uct_unpack_callback_t unpack_cb;
	void *arg;
	size_t length;
} uct_bcopy_completion_t;

typedef struct uct_kupl_req {
    ucs_queue_elem_t kupl_elem;
    kupl_event_h event;
    uct_completion_t *comp;
    void *dst;
    void *src;
    ucs_rcache_t *rcache;
    uct_kupl_xpmem_mem_region_t *mem_region;
    int flag;
} uct_kupl_req_t;

typedef struct uct_kupl_iface_config {
    uct_iface_config_t super;
} uct_kupl_iface_config_t;

typedef struct uct_kupl_iface_addr {
    uint32_t iface_id;  //传递iface编号 
    xpmem_segid_t xsegid;
} UCS_S_PACKED uct_kupl_iface_addr_t;

typedef struct uct_kupl_iface {
    uct_base_iface_t super;
    kupl_queue_h kupl_queue;
    ucs_spinlock_t lock;
    uct_kupl_md_t *kupl_md;
    uint32_t iface_id;
    ucs_queue_head_t queue;
    uint32_t timeout;
    ucs_mpool_t req_mp;
    ucs_mpool_t buffer_mp;
} uct_kupl_iface_t;

#endif  /* UCT_KUPL_IFACE_H */