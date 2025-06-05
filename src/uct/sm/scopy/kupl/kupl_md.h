/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef UCT_KUPL_MD_H
#define UCT_KUPL_MD_H

#include <uct/base/uct_md.h>
#include <uct/api/uct_def.h>
#include "kupl_adaptor.h"

extern uct_component_t uct_kupl_component;

typedef struct uct_kupl_md {
    struct uct_md super;
    void *ptr_pin_mem; //锁定的内存块起始位置
    size_t size_pin_mem; //锁定内存块大小
    xpmem_segid_t xsegid;
} uct_kupl_md_t;

typedef struct uct_kupl_md_config {
    uct_md_config_t super;
} uct_kupl_md_config_t;

typedef struct uct_kupl_key {
    void *address; /**< base addr for the registration */
    uint64_t length;
} uct_kupl_key_t;

#endif  /* UCT_KUPL_MD_H */