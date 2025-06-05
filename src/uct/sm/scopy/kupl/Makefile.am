#
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#

if HAVE_KUPL

module_LTLIBRARIES     = libuct_kupl.la
libuct_kupl_la_CFLAGS   = $(BASE_CFLAGS)
libuct_kupl_la_CPPFLAGS = $(BASE_CPPFLAGS)
libuct_kupl_la_LIBADD   = $(top_builddir)/src/ucs/libucs.la \
                          $(top_builddir)/src/uct/libuct.la 
libuct_kupl_la_LDFLAGS  = $(SEC_LDFLAGS) -version-info $(SOVERSION)
noinst_HEADERS = \
    kupl_adaptor.h \
    kupl_iface.h \
    kupl_ep.h \
    kupl_md.h \
    kupl_interface.h \
    kupl_xpmem.h \
    kupl_xpmem_adaptor.h

libuct_kupl_la_SOURCES = \
    kupl_adaptor.c \
    kupl_iface.c \
    kupl_ep.c \
    kupl_md.c \
    kupl_xpmem.c \
    kupl_xpmem_adaptor.c

include $(top_srcdir)/config/module.am

endif