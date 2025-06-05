/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include <uct/api/uct_def.h>
#include <ucs/type/class.h>
#include <uct/base/uct_iov.inl>
#include "config.h"
#include "kupl_ep.h"

#define KUPL_EP_FLUSH_TIMEOUT 1000

static inline uct_kupl_req_t *uct_kupl_ep_alloc_req(uct_kupl_iface_t *iface, void *buffer, 
                                                    void *remote_addr, uct_completion_t *comp) {
    uct_kupl_req_t *s_req = ucs_mpool_get(&iface->req_mp);
    if (s_req == NULL) {
        return NULL;
    }
    s_req->src = remote_addr;
    s_req->dst = buffer;
    s_req->event = KUPL_FUNC(kupl_event_create)();
    if (s_req->event == NULL) {
        ucs_error("Failed to create kupl event");
        ucs_mpool_put(s_req);
        return NULL;
    }
    s_req->comp = comp;
    return s_req;
}

static inline void uct_kupl_ep_process(uct_kupl_ep_t *ep, uct_kupl_iface_t *iface,
                                       uct_kupl_req_t *s_req, size_t length) {       
    kupl_event_status_t status = KUPL_FUNC(kupl_memcpy_async)(s_req->dst, s_req->src, 
                                 length, NULL, s_req->event);

    if (status != KUPL_OK) {
        ucs_mpool_put(s_req);
        ucs_fatal("kupl:copy data from memory area src to memory area dst asynchronously failed, status = %d", status);
    } else {
        ucs_queue_push(&iface->queue, &(s_req->kupl_elem));
    }
    return;
}

ucs_status_t uct_kupl_ep_put_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov, 
                                   size_t iov_cnt, uint64_t remote_addr,
                                   uct_rkey_t rkey, uct_completion_t *comp)
{
    uint64_t buffer;
    uct_kupl_req_t *s_req;
    ucs_status_t status;
    uct_kupl_xpmem_mem_region_t *mem_region;
    size_t length = uct_iov_get_length(&iov[0]);
    uct_kupl_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_kupl_iface_t);
    uct_kupl_ep_t *ep = ucs_derived_of(tl_ep, uct_kupl_ep_t);

    if (length == 0) {
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, ZCOPY, iov_cnt);
        return UCS_OK;
    }
    ucs_debug("kupl: ep[%d->%d] uct_kupl_ep_put_zcopy, iov cnt = %lu, length = %lu", ep->local_ifaceid,
              ep->remote_ifaceid, iov_cnt, length);

    buffer = (uint64_t)uct_iov_get_buffer(&iov[0]);

    //xpmem_mem_attach
    status = uct_kupl_xpmem_mem_attach((void *)remote_addr, length, ep->rcache, &mem_region);
    if(status != UCS_OK) {
        ucs_error("xpmem attach failed, err = %d", status);
        kupl_xpmem_release_func(ep->apid);
        ucs_rcache_destroy(ep->rcache);
        return status;
    }

    s_req = uct_kupl_ep_alloc_req(iface, (void*)mem_region->mapped_address, (void *)buffer, comp);
    if (s_req == NULL) {
        ucs_error("kupl: ep[%d->%d] put short failed, not enough slot",
                  ep->local_ifaceid, ep->remote_ifaceid);
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, ZCOPY, iov_cnt);
        return UCS_ERR_NO_RESOURCE;
    }
    s_req->rcache = ep->rcache;
    s_req->mem_region = mem_region;
    uct_kupl_ep_process(ep, iface, s_req, length);
    UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, ZCOPY, iov_cnt);
    return UCS_INPROGRESS;
}

ucs_status_t uct_kupl_ep_get_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov,
                                   size_t iov_cnt, uint64_t remote_addr,
                                   uct_rkey_t rkey, uct_completion_t *comp)
{
    uct_kupl_req_t *s_req;
    uint64_t buffer;
    ucs_status_t status;
    uct_kupl_xpmem_mem_region_t *mem_region;
    uct_kupl_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_kupl_iface_t);
    uct_kupl_ep_t *ep = ucs_derived_of(tl_ep, uct_kupl_ep_t);
    size_t length = uct_iov_get_length(&iov[0]);

    if (length == 0) {
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), GET, ZCOPY, iov_cnt);
        return UCS_OK;
    }
    ucs_debug("kupl: ep[%d->%d] uct_kupl_ep_get_zcopy, iov cnt = %lu, length = %lu", ep->local_ifaceid,
              ep->remote_ifaceid, iov_cnt, length);

    buffer = (uint64_t)uct_iov_get_buffer(&iov[0]);

    //xpmem_mem_attach
    status = uct_kupl_xpmem_mem_attach((void *)remote_addr, length, ep->rcache, &mem_region);
    if(status != UCS_OK) {
        ucs_error("xpmem attach failed, err = %d", status);
        kupl_xpmem_release_func(ep->apid);
        ucs_rcache_destroy(ep->rcache);
        return status;
    }

    s_req = uct_kupl_ep_alloc_req(iface, (void *)buffer, (void*)mem_region->mapped_address, comp);
    if (s_req == NULL) {
        ucs_error("kupl: ep[%d->%d] put short failed, not enough slot",
                  ep->local_ifaceid, ep->remote_ifaceid);
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), GET, ZCOPY, iov_cnt);
        return UCS_ERR_NO_RESOURCE;
    }

    s_req->rcache = ep->rcache;
    s_req->mem_region = mem_region;

    uct_kupl_ep_process(ep, iface, s_req, length);
    UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), GET, ZCOPY, iov_cnt);
    return UCS_INPROGRESS;
}

ucs_status_t uct_kupl_ep_flush(uct_ep_h tl_ep, unsigned flags, uct_completion_t *comp)
{
    kupl_event_status_t status;
    uct_kupl_req_t *seq;
    ucs_queue_iter_t iter;
    uct_kupl_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_kupl_iface_t);
    uct_kupl_ep_t *kupl_ep = ucs_derived_of(tl_ep, uct_kupl_ep_t);
    int flight = 0;

    ucs_spin_lock(&kupl_ep->lock);
    ucs_queue_for_each_safe(seq, iter, &iface->queue, kupl_elem) {
        if (seq->event != NULL) {
            status = KUPL_FUNC(kupl_event_query)(seq->event);
            if (status == KUPL_ERROR) {
                ucs_fatal("kupl iface[%d] : kupl_event_query failed, status = %d", iface->iface_id, status);
            } else if (status != KUPL_EVENT_STATUS_COMPLETE) {
                kupl_ep->timeout++;
                flight = 1;
                break;
            }
        }

        if (seq->comp != NULL) {
            uct_invoke_completion(seq->comp, 0);
        }

        uct_kupl_xpmem_mem_detach(seq->rcache, seq->mem_region);
        KUPL_FUNC(kupl_event_destroy)(seq->event);
        ucs_queue_del_iter(&iface->queue, iter);
        ucs_mpool_put(seq);
    }

    if (flight == 0) {
        kupl_ep->timeout = 0;
        ucs_spin_unlock(&kupl_ep->lock);
        return UCS_OK;
    }

    if (kupl_ep->timeout > KUPL_EP_FLUSH_TIMEOUT) {
        kupl_ep->timeout = 0;
        ucs_warn("kupl_ep: requests already query %d times", KUPL_EP_FLUSH_TIMEOUT);
    }
    ucs_spin_unlock(&kupl_ep->lock);
    return status;
}

/* init kupl ep resources */
static UCS_CLASS_INIT_FUNC(uct_kupl_ep_t, const uct_ep_params_t *params)
{
    uct_kupl_iface_addr_t *iface_addr = (uct_kupl_iface_addr_t *)params->iface_addr;
    uct_kupl_iface_t *iface = ucs_derived_of(params->iface, uct_kupl_iface_t);
    ucs_status_t status;

    ucs_trace_func("");
    UCS_CLASS_CALL_SUPER_INIT(uct_base_ep_t, &iface->super);
    UCT_EP_PARAMS_CHECK_DEV_IFACE_ADDRS(params);

    self->local_ifaceid = iface->iface_id;
    self->remote_ifaceid = iface_addr->iface_id;
    self->timeout = 0;

    self->apid = kupl_xpmem_get_func(iface_addr->xsegid, XPMEM_RDWR, XPMEM_PERMIT_MODE, NULL);
    if (self->apid < 0) {
        ucs_error("xpmem_get(segid=0x%lx) failed: %m", (unsigned long)iface_addr->xsegid);
        kupl_xpmem_remove_func(iface_addr->xsegid);
        return UCS_ERR_SHMEM_SEGMENT;
    }

    status = uct_kupl_xpmem_create_rcache(&self->rcache, self->apid);
    if (status != UCS_OK) {
        ucs_error("rcache create failed, err = %d", status);
        kupl_xpmem_remove_func(iface_addr->xsegid);
        kupl_xpmem_release_func(self->apid);
        return status;
    }

    status = ucs_spinlock_init(&self->lock, 0);
    if (status != UCS_OK) {
        ucs_error("lock init failed, err = %d", status);
        return status;
    }
    ucs_debug("uct_kupl_ep_t %p: created on iface 0x%p, remote_ifaceid: 0x%x .", self, iface, self->remote_ifaceid);
    return UCS_OK;
}

/* delete kupl ep resources */
static UCS_CLASS_CLEANUP_FUNC(uct_kupl_ep_t)
{
    uct_kupl_xpmem_destroy_rcache(self->rcache);
    kupl_xpmem_release_func(self->apid);
    ucs_spinlock_destroy(&self->lock);
    ucs_info("kupl: ep[%d->%d] cleanup", self->local_ifaceid, self->remote_ifaceid);
    return;
}

UCS_CLASS_DEFINE(uct_kupl_ep_t, uct_base_ep_t)
UCS_CLASS_DEFINE_NEW_FUNC(uct_kupl_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DEFINE_DELETE_FUNC(uct_kupl_ep_t, uct_ep_t);