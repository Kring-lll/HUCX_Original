/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef UCT_KUPL_EP_H
#define UCT_KUPL_EP_H

#include <uct/api/uct_def.h>
#include <uct/api/uct.h>
#include <ucs/datastruct/khash.h>
#include "kupl_iface.h"
#include "kupl_xpmem.h"
#include "kupl_adaptor.h"

#define SHMEM_KEY_GET(a, b) ((a)*100 + (b))
typedef struct uct_kupl_ep {
    uct_base_ep_t super;
    int local_ifaceid;
    int remote_ifaceid;
    ucs_spinlock_t lock;
    kupl_queue_h kupl_queue;
    int timeout;
    ucs_rcache_t *rcache;
    xpmem_apid_t apid;
} uct_kupl_ep_t;

UCS_CLASS_DECLARE_NEW_FUNC(uct_kupl_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DECLARE_DELETE_FUNC(uct_kupl_ep_t, uct_ep_t);

ucs_status_t uct_kupl_ep_put_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov, size_t iov_cnt, uint64_t remote_addr,
    uct_rkey_t rkey, uct_completion_t *comp);

ucs_status_t uct_kupl_ep_get_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov, size_t iov_cnt, uint64_t remote_addr,
    uct_rkey_t rkey, uct_completion_t *comp);

ucs_status_t uct_kupl_ep_flush(uct_ep_h tl_ep, unsigned flags, uct_completion_t *comp);

#endif  /* UCT_KUPL_EP_H */