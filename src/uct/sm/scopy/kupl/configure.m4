#
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#

#
# KUPL Support
#
AC_ARG_WITH([kupl],
            [AS_HELP_STRING([--with-kupl], [Compile with KUPL support])],
            [],
            [with_kupl=yes])

AS_IF([test "x$with_kupl" != xno],
      [
        AC_DEFINE([HAVE_KUPL], 1, [KUPL transport support])
        uct_modules="${uct_modules}:kupl"
      ])

AS_IF([test "x$with_xpmem" != "xno"],
      [
       CPPFLAGS="$CPPFLAGS -I$with_xpmem/include"
       AC_CHECK_HEADER([xpmem.h],
                       [AC_SUBST(CPPFLAGS)],
                       [AC_MSG_WARN([cray-xpmem header was not found in $with_xpmem])]
                      )
      ]
     )

AM_CONDITIONAL([HAVE_KUPL], [test "x$with_kupl" != xno])

AC_CONFIG_FILES([src/uct/sm/scopy/kupl/Makefile])