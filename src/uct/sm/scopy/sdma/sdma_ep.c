/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#include <sys/uio.h>
#include <ucs/debug/log.h>
#include <ucs/type/class.h>
#include <uct/base/uct_iov.inl>
#include <errno.h>
#include <ucs/arch/cpu.h>
#include "sdma_ep.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define EP_FLUSH_TIMEOUT 1000

ucs_status_t uct_sdma_ep_flush(uct_ep_h tl_ep, unsigned flags,
                               uct_completion_t *comp)
{
    uct_sdma_ep_t *sdma_ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    uct_sdma_req_queue_t *sq = sdma_ep->req_q;
    uct_sdma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_sdma_iface_t);
    int flight = 0;
    int result = 0;
    int i, ret;
    int count = 0;

    ucs_error("===TEST, uct_sdma_ep_flush sq->head=%d, sq->tail=%d", sq->head, sq->tail);
    ucs_spin_lock(&sdma_ep->lock);
    if (!iface->config.shared_mode) {
        sdma_progress(iface->src_sdma_handle);
    }

    i = sq->head;
    while (i != sq->tail) {
        if (iface->config.shared_mode) {
            /* must wait channal completed */
            ret = sdma_iwait_chn(iface->src_sdma_handle, &sq->reqs[i].request);
            ucs_error("===TEST, uct_sdma_ep_flush ret=%d, SDMA_SUCCESS:%d, SDMA_RNDCNT_ERR:%d", ret, SDMA_SUCCESS, SDMA_RNDCNT_ERR);
            if (ret != SDMA_SUCCESS) {
                if (ret == SDMA_RNDCNT_ERR) {
                    sq->timeout++;
                    flight = 1;
                    break;
                } else {
                    ucs_fatal("sdma_iface:chn_id[%d] src_dev_idx[%d] cur_cpu[%d] status = %d, error_code = %d", iface->chn_id,
                              iface->src_dev_idx, iface->cur_cpu, result, ret);
                }
            }
            sq->reqs[i].result = ret;
        } else if (sq->reqs[i].is_over != 1) {
            sq->timeout++;
            flight = 1;
            break;
        }

        if (sq->reqs[i].result != 0) {
            result = sq->reqs[i].result;
            ucs_fatal("sdma_ep[%d->%d] requests failed. status = %d", sdma_ep->local_ifaceid,
                sdma_ep->remote_ifaceid, result);
        }

        if (sq->reqs[i].comp != NULL) {
            uct_invoke_completion(sq->reqs[i].comp, sq->reqs[i].result);
        }

        i++;
        if (i >= sq->size) {
            i = 0;
        }

        sq->head = i;
        count++;
    }

    if (flight == 0) {
        sq->timeout = 0;
        ucs_spin_unlock(&sdma_ep->lock);
        return UCS_OK;
    }

    if (sq->timeout > EP_FLUSH_TIMEOUT) {
        sq->timeout = 0;
        ucs_warn("sdma: ep[%d->%d] requests already query %d times", sdma_ep->local_ifaceid,
            sdma_ep->remote_ifaceid, EP_FLUSH_TIMEOUT);
    }

    ucs_arbiter_dispatch(&iface->arbiter, 1, uct_sdma_ep_process_pending,
                         &count);

    ucs_spin_unlock(&sdma_ep->lock);

    return UCS_INPROGRESS;
}

void uct_sdma_req_cb(int task_status, void *task_data)
{
    uct_sdma_req_t *s_req = (uct_sdma_req_t *)task_data;

    /* 根据cqe status，标记sdma任务执行结果 */
    s_req->result = task_status;
    s_req->is_over = 1;

    return;
}

static uct_sdma_req_t *uct_sdma_ep_alloc_req(uct_sdma_ep_t *ep)
{
    uct_sdma_req_t *s_req;
    int pos;
    int next_tail;

    /* 从资源池找1个空闲资源 */
    next_tail = ep->req_q->tail + 1;
    if (next_tail == ep->req_q->size) {
        next_tail = 0;
    }
    ucs_error("===TEST, ep->req_q->head:%d, ep->req_q->tail:%d, next_tail:%d", ep->req_q->head, ep->req_q->tail, next_tail);
    /* 没有空闲资源则 */
    if (ep->req_q->head == next_tail) {
        ucs_debug("sdma: ep[%d->%d] request fifo is full", ep->local_ifaceid, ep->remote_ifaceid);
        return NULL;
    }
    pos = ep->req_q->tail;
    ep->req_q->tail = next_tail;

    s_req = &(ep->req_q->reqs[pos]);
    s_req->comp = NULL;
    s_req->result = 0;
    s_req->is_over = 0;

    return s_req;
}

static void uct_sdma_ep_free_req(uct_sdma_ep_t *ep, int count)
{
    /* 释放资源给资源池 */
    while (count > 0) {
        if (ep->req_q->tail == 0) {
            ep->req_q->tail = ep->req_q->size - 1;
        } else {
            ep->req_q->tail--;
        }
        count--;
    }
    return;
}

/* return 0 means no resource */
static int uct_sdma_ep_check_req_res(uct_sdma_ep_t *ep)
{
    int next_tail;
    next_tail = ep->req_q->tail + 1;
    if (next_tail == ep->req_q->size) {
        next_tail = 0;
    }

    return ep->req_q->head != next_tail;
}

static void uct_sdma_ep_create_task(sdma_sqe_task_t *s_task, uint64_t src_addr, uint64_t dst_addr,
                                    uint32_t src_pasid, uint32_t dst_pasid, uint32_t length,
                                    sdma_task_callback task_cb, void *task_data)
{
    s_task->src_addr = src_addr;
    s_task->dst_addr = dst_addr;
    s_task->src_process_id = src_pasid;
    s_task->dst_process_id = dst_pasid;
    s_task->length = length;
    s_task->stride_num = 0;
    s_task->src_stride_len = 0;
    s_task->dst_stride_len = 0;
    s_task->opcode = 0;
    s_task->task_cb = task_cb;
    s_task->task_data = task_data;
    s_task->next_sqe = NULL;
    return;
}

ucs_status_t uct_sdma_ep_am_short(uct_ep_h tl_ep, uint8_t id, uint64_t header, const void *payload, unsigned length)
{
    sdma_shmem_field_t *sdma_shmem_field;
    int send_err_st = 1;
    uct_sdma_ep_t *ep;

    UCT_CHECK_AM_ID(id);

    if (length > SHMEM_BUFF_SIZE) {
        ucs_error("uct_sdma_ep_am_short length: %u exceeds the maximum length: %u.",
            length, SHMEM_BUFF_SIZE);
        return UCS_ERR_INVALID_PARAM;
    }

    ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    sdma_shmem_field = (sdma_shmem_field_t *)ep->remote_shmem_msg->shmem_base;

    for (int i = 0; i < SHMEM_FIELD_SIZE; i++) {
        ucs_spin_lock(&ep->lock);
        if (sdma_shmem_field->am_desc[i].owner_bit == 0) {
            sdma_shmem_field->am_desc[i].am_short_bit = 1;
            sdma_shmem_field->am_desc[i].id = id;
            sdma_shmem_field->am_desc[i].am_field.header = header;
            sdma_shmem_field->am_desc[i].length = length;

            if (payload) {
                memcpy(sdma_shmem_field->am_desc[i].am_field.buff, payload, length);
            }

            sdma_shmem_field->am_desc[i].owner_bit = 1;
            ucs_memory_cpu_store_fence();
            ucs_spin_unlock(&ep->lock);
            send_err_st = 0;
            break;
        }
        ucs_spin_unlock(&ep->lock);
    }

    if (send_err_st) {
        return UCS_ERR_NO_RESOURCE;
    }

    return UCS_OK;
}


ssize_t uct_sdma_ep_am_bcopy(uct_ep_h tl_ep, uint8_t id, uct_pack_callback_t pack_cb, void *arg, unsigned flags)
{
    sdma_shmem_field_t *sdma_shmem_field;
    uct_sdma_ep_t *ep;
    size_t length = 0;
    int send_err_st = 1;

    UCT_CHECK_AM_ID(id);

    ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    sdma_shmem_field = (sdma_shmem_field_t *)ep->remote_shmem_msg->shmem_base;
    for (int i = 0; i < SHMEM_FIELD_SIZE; i++) {
        ucs_spin_lock(&ep->lock);
        if (sdma_shmem_field->am_desc[i].owner_bit == 0) {
            length = pack_cb(sdma_shmem_field->am_desc[i].am_field.buff, arg);

            ucs_trace("uct_sdma_ep_am_bcopy pid: %d, am id: %u, i: %d, length: %lu", ep->local_ifaceid,
                id, i, length);

            sdma_shmem_field->am_desc[i].am_short_bit = 0;
            sdma_shmem_field->am_desc[i].id = id;
            sdma_shmem_field->am_desc[i].length = length;
            sdma_shmem_field->am_desc[i].owner_bit = 1;
            sdma_shmem_field->am_desc[i].flags = flags;

            UCT_TL_EP_STAT_OP(&ep->super, AM, BCOPY, length);
            ucs_memory_cpu_store_fence();
            ucs_spin_unlock(&ep->lock);
            send_err_st = 0;
            break;
        }
        ucs_spin_unlock(&ep->lock);
    }

    if (send_err_st) {
        return UCS_ERR_NO_RESOURCE;
    }

    return length;
}

ucs_status_t uct_sdma_ep_put_short(uct_ep_h tl_ep, const void *buffer, unsigned length, uint64_t remote_addr,
    uct_rkey_t rkey)
{
    uct_sdma_ep_t *ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    uct_sdma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_sdma_iface_t);
    uct_sdma_req_t *s_req;
    sdma_sqe_task_t *s_task;
    ucs_status_t status;
    //int ret;
    static int cnt = 0;

    ucs_info("sdma: ep[%d->%d] uct_sdma_ep_put_short2, length = %u",
        ep->local_ifaceid, ep->remote_ifaceid, length);

    if (length == 0) {
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, SHORT, length);
        return UCS_OK;
    }

    /* 从资源池找1个空闲资源 */
    s_req = uct_sdma_ep_alloc_req(ep);
    if (s_req == NULL) {
        ucs_debug("sdma: ep[%d->%d] put short failed, not enough slot",
            ep->local_ifaceid, ep->remote_ifaceid);
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, SHORT, length);
        return UCS_ERR_NO_RESOURCE;
    }

    /* 填充sdma任务 */
    s_task = &(s_req->task);
    uct_sdma_ep_create_task(s_task, (uint64_t)buffer, remote_addr, ep->ep_pasid, ep->remote_pasid,
        length, uct_sdma_req_cb, s_req);
    cnt++;
    ucs_error("===TEST, enter sdma_icopy_data, buffer:%p, remote_addr:%p, ep->ep_pasid:%d, ep->remote_pasid:%d, length:%d, cnt=%d.",
               buffer, (void *)remote_addr, ep->ep_pasid, ep->remote_pasid, length, cnt);
    /* 提交sdma任务 */
    if (iface->config.shared_mode) {

        status = sdma_icopy_data(ep->chn_ctx, s_task, 1, &(s_req->request));
        // ret = sdma_iwait_chn(iface->src_sdma_handle,  &(s_req->request));
        // if (ret != 0) {
        //     printf("===TEST, sdma_iwait_chn failed, ret = %d\n", ret);
        //     uct_sdma_ep_free_req(ep, 1);
        //     return UCS_ERR_IO_ERROR;
        // } else {
        //     printf("===TEST, sdma iwait success!\n");
        //     uct_sdma_ep_free_req(ep, 1);
        //     return UCS_OK;
        // }
        
    } else {
        status = sdma_copy_data(ep->chn_ctx, s_task, 1);
    }
    if (status != UCS_OK) {
        uct_sdma_ep_free_req(ep, 1);
        return UCS_ERR_NO_RESOURCE;
    }

    UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, SHORT, length);
    return UCS_OK;
}

ssize_t uct_sdma_ep_put_bcopy(uct_ep_h tl_ep, uct_pack_callback_t pack_cb, void *arg, uint64_t remote_addr,
    uct_rkey_t rkey)
{
    uct_sdma_ep_t *ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    uct_sdma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_sdma_iface_t);
    pack_context_t *pack_ctx = (pack_context_t *)arg;
    uint64_t buffer = (uint64_t)pack_ctx->src;
    size_t length = pack_ctx->length;
    uct_sdma_req_t *s_req;
    sdma_sqe_task_t *s_task;
    ucs_status_t status;

    ucs_info("sdma: ep[%d->%d] uct_sdma_ep_put_bcopy2, length = %lu",
        ep->local_ifaceid, ep->remote_ifaceid, length);

    if (length == 0) {
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, BCOPY, length);
        return UCS_OK;
    }

    /* 从资源池找1个空闲资源 */
    s_req = uct_sdma_ep_alloc_req(ep);
    if (s_req == NULL) {
        ucs_debug("sdma: ep[%d->%d] put bcopy failed, not enough slot",
            ep->local_ifaceid, ep->remote_ifaceid);
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, BCOPY, length);
        return UCS_ERR_NO_RESOURCE;
    }

    /* 填充sdma任务 */
    s_task = &(s_req->task);
    uct_sdma_ep_create_task(s_task, (uint64_t)buffer, remote_addr, ep->ep_pasid, ep->remote_pasid,
        length, uct_sdma_req_cb, s_req);

    /* 提交sdma任务 */
    if (iface->config.shared_mode) {
        status = sdma_icopy_data(ep->chn_ctx, s_task, 1, &(s_req->request));
    } else {
        status = sdma_copy_data(ep->chn_ctx, s_task, 1);
    }
    if (status != UCS_OK) {
        uct_sdma_ep_free_req(ep, 1);
        return UCS_ERR_NO_RESOURCE;
    }

    UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, BCOPY, length);
    return length;
}

ucs_status_t uct_sdma_ep_put_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov, size_t iov_cnt, uint64_t remote_addr,
    uct_rkey_t rkey, uct_completion_t *comp)
{
    uct_sdma_ep_t *ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    uct_sdma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_sdma_iface_t);
    uct_sdma_req_t *s_req;
    sdma_sqe_task_t *s_task;
    ucs_status_t status;
    uint64_t buffer;
    size_t length;

    /* sdma iface的cap中已定义iov = 1 */
    length = uct_iov_get_length(&iov[0]);
    if (length == 0) {
        /* 参考rdma\tcp: to avoid zero length elements */
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, ZCOPY, iov_cnt);
        return UCS_OK;
    }

    ucs_info("sdma: ep[%d->%d] uct_sdma_ep_put_zcopy, iov cnt = %lu, length = %lu", ep->local_ifaceid,
             ep->remote_ifaceid, iov_cnt, length);

    buffer = (uint64_t)uct_iov_get_buffer(&iov[0]);

    /* 从资源池找1个空闲资源 */
    s_req = uct_sdma_ep_alloc_req(ep);
    if (s_req == NULL) {
        ucs_debug("sdma: ep[%d->%d] put zcopy failed, not enough slot",
            ep->local_ifaceid, ep->remote_ifaceid);
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, ZCOPY, iov_cnt);
        return UCS_ERR_NO_RESOURCE;
    }

    /* 填充sdma任务 */
    s_task = &(s_req->task);
    uct_sdma_ep_create_task(s_task, (uint64_t)buffer, remote_addr, ep->ep_pasid, ep->remote_pasid,
        length, uct_sdma_req_cb, s_req);

    /* 1次任务只激活1次completion */
    s_req->comp = comp;

    /* 提交sdma任务 */
    if (iface->config.shared_mode) {
        status = sdma_icopy_data(ep->chn_ctx, s_task, 1, &(s_req->request));
    } else {
        status = sdma_copy_data(ep->chn_ctx, s_task, 1);
    }
    if (status != UCS_OK) {
        uct_sdma_ep_free_req(ep, 1);
        return UCS_ERR_NO_RESOURCE;
    }

    UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, ZCOPY, iov_cnt);
    return UCS_INPROGRESS;
}

ucs_status_t uct_sdma_ep_get_zcopy(uct_ep_h tl_ep, const uct_iov_t *iov, size_t iov_cnt, uint64_t remote_addr,
    uct_rkey_t rkey, uct_completion_t *comp)
{
    uct_sdma_ep_t *ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    uct_sdma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_sdma_iface_t);
    uct_sdma_req_t *s_req;
    sdma_sqe_task_t *s_task;
    ucs_status_t status;
    uint64_t buffer;
    size_t length;

    /* sdma iface的cap中已定义iov = 1 */
    length = uct_iov_get_length(&iov[0]);
    if (length == 0) {
        /* 参考rdma\tcp: to avoid zero length elements */
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), GET, ZCOPY, iov_cnt);
        return UCS_OK;
    }

    buffer = (uint64_t)uct_iov_get_buffer(&iov[0]);

    /* 从资源池找1个空闲资源 */
    s_req = uct_sdma_ep_alloc_req(ep);
    if (s_req == NULL) {
        ucs_debug("sdma: ep[%d->%d] get zcopy failed, not enough slot",
            ep->local_ifaceid, ep->remote_ifaceid);
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), GET, ZCOPY, iov_cnt);
        return UCS_ERR_NO_RESOURCE;
    }

    /* 填充sdma任务 */
    s_task = &(s_req->task);
    uct_sdma_ep_create_task(s_task, remote_addr, (uint64_t)buffer, ep->remote_pasid, ep->ep_pasid,
        length, uct_sdma_req_cb, s_req);

    /* 1次任务只激活1次completion */
    s_req->comp = comp;

    /* 提交sdma任务 */
    if (iface->config.shared_mode) {
        status = sdma_icopy_data(ep->chn_ctx, s_task, 1, &(s_req->request));
    } else {
        status = sdma_copy_data(ep->chn_ctx, s_task, 1);
    }
    if (status != UCS_OK) {
        uct_sdma_ep_free_req(ep, 1);
        return UCS_ERR_NO_RESOURCE;
    }

    UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), GET, ZCOPY, iov_cnt);
    return UCS_INPROGRESS;
}

ucs_status_t uct_sdma_ep_get_bcopy(uct_ep_h tl_ep, uct_unpack_callback_t unpack_cb, void *arg, size_t length,
    uint64_t remote_addr, uct_rkey_t rkey, uct_completion_t *comp)
{
    uct_sdma_ep_t *ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    uct_sdma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_sdma_iface_t);
    uct_sdma_req_t *s_req;
    sdma_sqe_task_t *s_task;
    ucs_status_t status;
    uint64_t buffer = (uint64_t)arg;

    ucs_info("sdma: ep[%d->%d] uct_sdma_ep_get_bcopy2", ep->local_ifaceid, ep->remote_ifaceid);

    if (length <= 0) {
        ucs_error("sdma: ep[%d->%d] get bcopy invalid length, length = %lu", ep->local_ifaceid,
            ep->remote_ifaceid, length);
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), GET, BCOPY, length);
        return UCS_ERR_INVALID_PARAM;
    }

    /* 从资源池找1个空闲资源 */
    s_req = uct_sdma_ep_alloc_req(ep);
    if (s_req == NULL) {
        ucs_debug("sdma: ep[%d->%d] get bcopy failed, not enough slot",
            ep->local_ifaceid, ep->remote_ifaceid);
        UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), PUT, BCOPY, length);
        return UCS_ERR_NO_RESOURCE;
    }

    /* 填充sdma任务 */
    s_task = &(s_req->task);
    uct_sdma_ep_create_task(s_task, (uint64_t)buffer, remote_addr, ep->ep_pasid, ep->remote_pasid,
        length, uct_sdma_req_cb, s_req);

    /* 保存completion */
    s_req->comp = comp;

    /* 提交sdma任务 */
    if (iface->config.shared_mode) {
        status = sdma_icopy_data(ep->chn_ctx, s_task, 1, &(s_req->request));
    } else {
        status = sdma_copy_data(ep->chn_ctx, s_task, 1);
    }
    if (status != UCS_OK) {
        uct_sdma_ep_free_req(ep, 1);
        return UCS_ERR_NO_RESOURCE;
    }

    UCT_TL_EP_STAT_OP(ucs_derived_of(tl_ep, uct_base_ep_t), GET, BCOPY, length);
    return UCS_INPROGRESS;
}

ucs_status_t uct_get_shmem_base(key_t shmem_key, sdma_shmem_msg_t *shmem_msg)
{
    ucs_status_t ret = UCS_OK;

    shmem_msg->shmem_id = shmget(shmem_key, SHMEM_SIZE, IPC_CREAT|PERMIS_FLAG);
    if(shmem_msg->shmem_id < 0) {
        ucs_error("Failed to get shmem_id, shmem_key: %d, errorno: %d", shmem_key, errno);
        ret = UCS_ERR_SHMEM_SEGMENT;
    }

    shmem_msg->shmem_base = shmat(shmem_msg->shmem_id, NULL, 0);
    if(shmem_msg->shmem_base < 0) {
        ucs_error("Failed to get shmem_base, shmem_base: %p", shmem_msg->shmem_base);
        ret = UCS_ERR_SHMEM_SEGMENT;
    }
    return ret;
}

ucs_status_t uct_creat_shmem(int ftok_id, sdma_shmem_msg_t *shmem_msg)
{
    ucs_status_t ret = UCS_OK;

    shmem_msg->shmem_key = ftok("../", ftok_id);
    if(shmem_msg->shmem_key  == SDMA_SHMEM_ERRCODE) {
        ucs_error("Failed to get shmem key.");
        return UCS_ERR_SHMEM_SEGMENT;
    }
    ret = uct_get_shmem_base(shmem_msg->shmem_key, shmem_msg);
    return ret;
}

static ucs_status_t uct_shmem_detach(sdma_shmem_msg_t *shmem_msg)
{
    if (shmdt(shmem_msg->shmem_base) == SDMA_SHMEM_ERRCODE) {
        ucs_error("Failed to detach shmem.");
        return UCS_ERR_NO_ELEM;
    }
    return UCS_OK;
}

ucs_status_t uct_shmem_del(sdma_shmem_msg_t *shmem_msg)
{
    ucs_status_t ret;

    ret = uct_shmem_detach(shmem_msg);
    if (ret) {
        return ret;
    }

    /* Repeatedly releasing handles may cause failures. */
    shmctl(shmem_msg->shmem_id, IPC_RMID, 0);
    return ret;
}

ucs_status_t uct_sdma_ep_pending_add(uct_ep_h tl_ep, uct_pending_req_t *n,
                                     unsigned flags)
{
    uct_sdma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_sdma_iface_t);
    uct_sdma_ep_t *ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    static int cnt = 0;

    cnt++;
    ucs_error("===TEST, uct_sdma_ep_pending_adFd cnt=%d", cnt);
    UCS_STATIC_ASSERT(sizeof(uct_pending_req_priv_arb_t) <=
                      UCT_PENDING_REQ_PRIV_LEN);
    uct_pending_req_arb_group_push(&ep->arb_group, n);
    /* add the ep's group to the arbiter */
    ucs_arbiter_group_schedule(&iface->arbiter, &ep->arb_group);

    return UCS_OK;
}

static ucs_arbiter_cb_result_t uct_sdma_ep_arbiter_purge_cb(ucs_arbiter_t *arbiter,
                                                            ucs_arbiter_group_t *group,
                                                            ucs_arbiter_elem_t *elem,
                                                            void *arg)
{
    uct_sdma_ep_t *ep = ucs_container_of(group, uct_sdma_ep_t,
                                                         arb_group);
    uct_purge_cb_args_t *cb_args = arg;
    uct_pending_purge_callback_t cb = cb_args->cb;
    uct_pending_req_t *req;

    req = ucs_container_of(elem, uct_pending_req_t, priv);
    if (cb != NULL) {
        cb(req, cb_args->arg);
    } else {
        ucs_warn("ep=%p canceling user pending request %p", ep, req);
    }

    return UCS_ARBITER_CB_RESULT_REMOVE_ELEM;
}

void uct_sdma_ep_pending_purge(uct_ep_h tl_ep, uct_pending_purge_callback_t cb,
                               void *arg)
{
    uct_sdma_iface_t *iface = ucs_derived_of(tl_ep->iface, uct_sdma_iface_t);
    uct_sdma_ep_t *ep = ucs_derived_of(tl_ep, uct_sdma_ep_t);
    uct_purge_cb_args_t args = {cb, arg};

    ucs_arbiter_group_purge(&iface->arbiter, &ep->arb_group,
                            uct_sdma_ep_arbiter_purge_cb, &args);
}

ucs_arbiter_cb_result_t uct_sdma_ep_process_pending(ucs_arbiter_t *arbiter,
                                                    ucs_arbiter_group_t *group,
                                                    ucs_arbiter_elem_t *elem,
                                                    void *arg)
{
    uct_sdma_ep_t *ep = ucs_container_of(group, uct_sdma_ep_t, arb_group);
    uint32_t *count = (uint32_t *)arg;
    uct_pending_req_t *req;
    ucs_status_t status;

    if (!uct_sdma_ep_check_req_res(ep)) {
        return UCS_ARBITER_CB_RESULT_RESCHED_GROUP;
    }

    req = ucs_container_of(elem, uct_pending_req_t, priv);

    ucs_trace_data("progressing pending request %p", req);
    status = req->func(req);
    ucs_trace_data("status returned from progress pending: %s",
                   ucs_status_string(status));

    if (status == UCS_OK || status == UCS_INPROGRESS) {
        (*count)++;
        /* sent successfully. remove from the arbiter */
        return UCS_ARBITER_CB_RESULT_REMOVE_ELEM;
    } else {
        /* couldn't send. keep this request in the arbiter until the next time
         * this function is called */
        return UCS_ARBITER_CB_RESULT_RESCHED_GROUP;
    }
}

static UCS_CLASS_INIT_FUNC(uct_sdma_ep_t, const uct_ep_params_t *params)
{
    uct_sdma_iface_addr_t *iface_addr = (uct_sdma_iface_addr_t *)params->iface_addr;
    uct_sdma_iface_t *iface = ucs_derived_of(params->iface, uct_sdma_iface_t);
    sdma_shmem_msg_t *remote_shmem_msg;
    ucs_status_t status;

    ucs_trace_func("");
    UCS_CLASS_CALL_SUPER_INIT(uct_base_ep_t, &iface->super);
    ucs_arbiter_group_init(&self->arb_group);
    UCT_EP_PARAMS_CHECK_DEV_IFACE_ADDRS(params);

    self->ep_pasid = iface->src_pasid[iface->src_dev_idx];
    self->remote_pasid = iface_addr->pasid[iface->src_dev_idx];
    self->chn_ctx = iface->src_sdma_handle;

    self->remote_devid = iface_addr->devid;
    self->local_ifaceid = iface->cur_cpu;
    self->remote_ifaceid = iface_addr->iface_id;
    if (self->remote_pasid != getpid()) {
        status = (ucs_status_t)sdma_add_authority(iface->sdma_md->sdma_fd[self->remote_devid], &self->remote_pasid, 1);
        if (status != UCS_OK) {
            ucs_error("sdma: sdma_add_authority faild status = %d", status);
            return UCS_ERR_IO_ERROR;
        }
    }

    remote_shmem_msg = (sdma_shmem_msg_t *)calloc(1, sizeof(sdma_shmem_msg_t));
    remote_shmem_msg->shmem_key = iface_addr->shmem_key;
    status = uct_get_shmem_base(remote_shmem_msg->shmem_key, remote_shmem_msg);
    ucs_assert(status == UCS_OK);
    self->remote_shmem_msg = remote_shmem_msg;

    self->req_q = &iface->req_q_iface;
    ucs_list_add_head(&iface->ep_list, &self->list);

    status = ucs_spinlock_init(&self->lock, 0);
    if (status != UCS_OK) {
        ucs_error("lock init failed, err = %d", status);
        return status;
    }
    ucs_info("sdma: ep[%d->%d] init", self->local_ifaceid, self->remote_ifaceid);

    ucs_debug("uct_sdma_ep_t %p: created on iface 0x%p, remote_pasid: 0x%x .", self, iface, self->remote_pasid);
    return UCS_OK;
}

static UCS_CLASS_CLEANUP_FUNC(uct_sdma_ep_t)
{
    ucs_spinlock_destroy(&self->lock);
    uct_shmem_del(self->remote_shmem_msg);
    free(self->remote_shmem_msg);
    self->remote_shmem_msg = NULL;

    ucs_info("sdma: ep[%d->%d] cleanup", self->local_ifaceid, self->remote_ifaceid);
}

UCS_CLASS_DEFINE(uct_sdma_ep_t, uct_base_ep_t)
UCS_CLASS_DEFINE_NEW_FUNC(uct_sdma_ep_t, uct_ep_t, const uct_ep_params_t *);
UCS_CLASS_DEFINE_DELETE_FUNC(uct_sdma_ep_t, uct_ep_t);
