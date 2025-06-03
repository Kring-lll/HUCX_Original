/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef UCT_KUPL_ADAPTOR_H
#define UCT_KUPL_ADAPTOR_H

#include <ucs/sys/preprocessor.h>
#include "ucs/type/spinlock.h"
#include "kupl_xpmem_adaptor.h"

typedef void* kupl_queue_h;
typedef void* kupl_event_h;

#define KUPL_OK            0
#define KUPL_ERROR         (-1)

typedef enum kupl_queue_flag {
    KUPL_QUEUE_FLAG_DEFAULT        /* default queue flag */
} kupl_queue_flag_t;

typedef enum kupl_event_status {
    KUPL_EVENT_STATUS_CREATE,
    KUPL_EVENT_STATUS_INIT,
    KUPL_EVENT_STATUS_COMPLETE,
    KUPL_EVENT_STATUS_RUNNING,
    KUPL_EVENT_STATUS_SUBMITTED,
    KUPL_EVENT_STATUS_QUEUED,
    KUPL_EVENT_STATUS_UNKNOWN,
} kupl_event_status_t;

ucs_status_t init_kupl_lib();
void uninit_kupl_lib();

#define KUPL_FUNC(_f) UCS_PP_TOKENPASTE(_f, _func)
#define KUPL_FUNC_PTR(_f) UCS_PP_TOKENPASTE(_f, _ptr)

#define KUPL_INTERFACE(_ret, _f, _args) \
    _ret KUPL_FUNC(_f) _args

#include "kupl_interface.h"

#endif  /* UCT_KUPL_ADAPTOR_H */