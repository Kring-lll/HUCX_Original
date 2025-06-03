/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef UCT_KUPL_XPMEM_ADAPTOR_H
#define UCT_KUPL_XPMEM_ADAPTOR_H

#include <ucs/sys/preprocessor.h>
#include "ucs/type/spinlock.h"

#define XPMEM_MAXADDR_SIZE      (size_t)(-1L)
#define XPMEM_RDONLY    0x1
#define XPMEM_RDWR      0x2
enum {
  XPMEM_PERMIT_MODE = 0x1,
};

typedef long long int xpmem_segid_t;
typedef long long int xpmem_apid_t;
struct xpmem_addr {
  xpmem_apid_t apid;
  off_t offset;
};

typedef xpmem_segid_t (*xpmem_make_func)(void *, size_t, int, void *);
typedef xpmem_apid_t (*xpmem_get_func)(xpmem_segid_t, int, int, void *);
typedef void *(*xpmem_attach_func)(struct xpmem_addr, size_t, void *);  
typedef int (*xpmem_detach_func)(void *);
typedef int (*xpmem_release_func)(xpmem_apid_t);
typedef int (*xpmem_remove_func)(xpmem_segid_t);

ucs_status_t kupl_xpmem_init();
void kupl_xpmem_uninit();

xpmem_segid_t kupl_xpmem_make_func(void *vaddr, size_t size, int permit_type, void *permit_value);
xpmem_apid_t kupl_xpmem_get_func(xpmem_segid_t segid, int flags, int permit_type,
                                 void *permit_value);
void *kupl_xpmem_attach_func(struct xpmem_addr addr, size_t size, void *vaddr);
int kupl_xpmem_detach_func(void *vaddr);
int kupl_xpmem_release_func(xpmem_apid_t apid);
int kupl_xpmem_remove_func(xpmem_segid_t segid);

#endif  /* UCT_KUPL_ADAPTOR_H */