/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <ucs/config/parser.h>
#include <ucs/type/status.h>
#include <ucs/sys/topo/base/topo.h>
#include <uct/api/uct_def.h>
#include <uct/base/uct_iface.h>
#include <uct/sm/base/sm_iface.h>
#include "config.h"
#include "kupl_iface.h"
#include "kupl_ep.h"

#define KUPL_EP_PROGRESS_TIMEOUT 1000

/* env config table */
static ucs_config_field_t uct_kupl_iface_config_table[] = {
    {"KUPL_", "", NULL,
     ucs_offsetof(uct_kupl_iface_config_t, super),
     UCS_CONFIG_TYPE_TABLE(uct_iface_config_table)},

    {NULL}
};

/* progress */
static unsigned uct_kupl_iface_progress(uct_iface_h tl_iface)
{
    ucs_queue_iter_t iter;
    uct_kupl_req_t *kupl_seq;
    kupl_event_status_t status;
    uct_kupl_iface_t *iface = ucs_derived_of(tl_iface, uct_kupl_iface_t);
    int flight = 0;

    ucs_spin_lock(&iface->lock);
    ucs_queue_for_each_safe(kupl_seq, iter, &iface->queue, kupl_elem) {
        if (kupl_seq->event != NULL) {
            status = KUPL_FUNC(kupl_event_query)(kupl_seq->event);
            if (status == KUPL_ERROR) {
                ucs_fatal("kupl iface[%d] : kupl_event_query failed, status = %d", iface->iface_id, status);
            } else if (status != KUPL_EVENT_STATUS_COMPLETE) {
                iface->timeout++;
                flight = 1;
                break;
            }
        }
        if (kupl_seq->comp != NULL) {
            uct_invoke_completion(kupl_seq->comp, 0);
        }

        uct_kupl_xpmem_mem_detach(kupl_seq->rcache, kupl_seq->mem_region);
        KUPL_FUNC(kupl_event_destroy)(kupl_seq->event);
        ucs_queue_del_iter(&iface->queue, iter);
        ucs_mpool_put(kupl_seq);
    }

    if (flight == 0) {
        iface->timeout = 0;
        ucs_spin_unlock(&iface->lock);
        return UCS_OK;
    }
    if (iface->timeout > KUPL_EP_PROGRESS_TIMEOUT) {
        iface->timeout = 0;
        ucs_debug("kupl_iface: requests already query %d times", KUPL_EP_PROGRESS_TIMEOUT);
    }
    ucs_spin_unlock(&iface->lock);
    return UCS_INPROGRESS;
}

/* iface query attr */
static ucs_status_t uct_kupl_iface_query(uct_iface_h tl_iface, uct_iface_attr_t *attr)
{
    uct_kupl_iface_t *iface = ucs_derived_of(tl_iface, uct_kupl_iface_t);
    uct_base_iface_query(&iface->super, attr);

    attr->iface_addr_len          = sizeof(uct_kupl_iface_addr_t);
    attr->device_addr_len         = uct_sm_iface_get_device_addr_len();
    attr->ep_addr_len             = 0;
    attr->max_conn_priv           = 0;

    /* default values for all shared memory transports */
    attr->cap.put.min_zcopy       = 0;
    attr->cap.put.max_zcopy       = SIZE_MAX;
    attr->cap.put.opt_zcopy_align = 1;
    attr->cap.put.align_mtu       = attr->cap.put.opt_zcopy_align;
    attr->cap.put.max_iov         = 1;

    attr->cap.get.min_zcopy       = 0;
    attr->cap.get.max_zcopy       = SIZE_MAX;
    attr->cap.get.opt_zcopy_align = 1;
    attr->cap.get.align_mtu       = attr->cap.get.opt_zcopy_align;
    attr->cap.get.max_iov         = 1;
    attr->cap.flags               = UCT_IFACE_FLAG_GET_ZCOPY |
                                    UCT_IFACE_FLAG_PUT_ZCOPY |
                                    UCT_IFACE_FLAG_PENDING   |
                                    UCT_IFACE_FLAG_CONNECT_TO_IFACE;

    attr->latency                 = ucs_linear_func_make(0, 0);
    attr->bandwidth.dedicated     = 10000;
    attr->overhead                = 10e-9;
    attr->priority                = 1;
    return UCS_OK;
}

/* iface get address */
static ucs_status_t uct_kupl_iface_get_address(uct_iface_h tl_iface, uct_iface_addr_t *addr)
{
    uct_kupl_iface_t *iface = ucs_derived_of(tl_iface, uct_kupl_iface_t);
    uct_kupl_iface_addr_t *iface_addr = (uct_kupl_iface_addr_t *)addr;

    iface_addr->iface_id = iface->iface_id;
    iface_addr->xsegid   = iface->kupl_md->xsegid;
    return UCS_OK;
}

static UCS_CLASS_DECLARE_DELETE_FUNC(uct_kupl_iface_t, uct_iface_t);

/* iface op interface */
static uct_iface_ops_t uct_kupl_iface_ops = {
    .ep_put_zcopy = uct_kupl_ep_put_zcopy,
    .ep_get_zcopy = uct_kupl_ep_get_zcopy,
    .ep_pending_add = (uct_ep_pending_add_func_t)ucs_empty_function_return_busy,
    .ep_pending_purge = (uct_ep_pending_purge_func_t)ucs_empty_function,
    .ep_flush = uct_kupl_ep_flush,
    .ep_fence = uct_base_ep_fence,
    .ep_check = (uct_ep_check_func_t)ucs_empty_function_return_success,
    .ep_create = UCS_CLASS_NEW_FUNC_NAME(uct_kupl_ep_t),
    .ep_destroy = UCS_CLASS_DELETE_FUNC_NAME(uct_kupl_ep_t),
    .iface_flush = uct_base_iface_flush,
    .iface_fence = uct_base_iface_fence,
    .iface_progress_enable = uct_base_iface_progress_enable,
    .iface_progress_disable = uct_base_iface_progress_disable,
    .iface_progress = uct_kupl_iface_progress,
    .iface_close = UCS_CLASS_DELETE_FUNC_NAME(uct_kupl_iface_t),
    .iface_query = uct_kupl_iface_query,
    .iface_get_device_address = uct_sm_iface_get_device_address,
    .iface_get_address = uct_kupl_iface_get_address,
    .iface_is_reachable = uct_sm_iface_is_reachable
};

static ucs_mpool_ops_t uct_kupl_event_desc_mpool_ops = {
    .chunk_alloc   = ucs_mpool_chunk_malloc,
    .chunk_release = ucs_mpool_chunk_free,
    .obj_init      = NULL,
    .obj_cleanup   = NULL,
    .obj_str       = NULL
};

/* init kupl iface resources */
static UCS_CLASS_INIT_FUNC(uct_kupl_iface_t, uct_md_h md, uct_worker_h worker, const uct_iface_params_t *params,
                           const uct_iface_config_t *tl_config)
{
    ucs_status_t status = UCS_OK;
    ucs_mpool_params_t mp_params;

    UCS_CLASS_CALL_SUPER_INIT(uct_base_iface_t, &uct_kupl_iface_ops, &uct_base_iface_internal_ops, md, worker, params,
        tl_config UCS_STATS_ARG((params->field_mask & UCT_IFACE_PARAM_FIELD_STATS_ROOT) ? params->stats_root : NULL)
        UCS_STATS_ARG(params->mode.device.dev_name));
    self->timeout = 0;
    self->kupl_md = ucs_derived_of(md, uct_kupl_md_t);
    if (NULL == self->kupl_md) {
        ucs_error("Failed to allocate memory for uct_kupl_md_t");
        status = UCS_ERR_NO_MEMORY;
        goto out;
    }

    self->iface_id = getpid();

    ucs_mpool_params_reset(&mp_params);
    mp_params.elem_size       = sizeof(uct_kupl_req_t);
    mp_params.elems_per_chunk = 1024;
    mp_params.ops             = &uct_kupl_event_desc_mpool_ops;
    mp_params.name            = "uct_kupl_iface_req";
    status = ucs_mpool_init(&mp_params, &self->req_mp);
    if (UCS_OK != status) {
        ucs_error("mpool creation failed");
        status = UCS_ERR_NO_MEMORY;
        goto out;
    }

    ucs_queue_head_init(&self->queue);

    if (self->kupl_md->xsegid < 0) {
        self->kupl_md->xsegid = kupl_xpmem_make_func(0, XPMEM_MAXADDR_SIZE, XPMEM_PERMIT_MODE, (void*)0600);
        if (self->kupl_md->xsegid < 0) {
            ucs_error("kupl xpmem failed to register process address space: %m");
            status = UCS_ERR_IO_ERROR;
            goto mpool_cleanup;
        }
    }

    status = ucs_spinlock_init(&self->lock, 0);
    if (status != UCS_OK) {
        ucs_error("lock init failed, err = %d", status);
        goto remove_xsegid;
    }
    ucs_info("kupl: iface[%d] init.", self->iface_id);
    goto out;

remove_xsegid:
    kupl_xpmem_remove_func(self->kupl_md->xsegid);
mpool_cleanup:
    ucs_mpool_cleanup(&self->req_mp, 1);
out:
    return status;
}


/* clear kupl iface resources */
static UCS_CLASS_CLEANUP_FUNC(uct_kupl_iface_t)
{
    uct_base_iface_progress_disable(&self->super.super, UCT_PROGRESS_SEND | UCT_PROGRESS_RECV);
    ucs_spinlock_destroy(&self->lock);
    kupl_xpmem_remove_func(self->kupl_md->xsegid);
    ucs_mpool_cleanup(&self->req_mp, 1);
    ucs_info("kupl: iface[%d] cleanup.", self->iface_id);
    return;
}

UCS_CLASS_DEFINE(uct_kupl_iface_t, uct_base_iface_t);

static UCS_CLASS_DEFINE_NEW_FUNC(uct_kupl_iface_t, uct_iface_t, uct_md_h, uct_worker_h, const uct_iface_params_t *,
                                 const uct_iface_config_t *);

static UCS_CLASS_DEFINE_DELETE_FUNC(uct_kupl_iface_t, uct_iface_t);

static ucs_status_t uct_kupl_query_tl_devices(uct_md_h md,
                                              uct_tl_device_resource_t **tl_devices_p,
                                              unsigned *num_tl_devices_p)
{
    return uct_single_device_resource(md, "memory", UCT_DEVICE_TYPE_SHM, UCS_SYS_DEVICE_ID_UNKNOWN, tl_devices_p,
                                      num_tl_devices_p);
}

UCT_TL_DEFINE(&uct_kupl_component, kupl, uct_kupl_query_tl_devices, uct_kupl_iface_t, "KUPL_",
              uct_kupl_iface_config_table, uct_kupl_iface_config_t);