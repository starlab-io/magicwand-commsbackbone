/*************************************************************************
* STAR LAB PROPRIETARY & CONFIDENTIAL
* Copyright (C) 2018, Star Lab — All Rights Reserved
* Unauthorized copying of this file, via any medium is strictly prohibited.
***************************************************************************/

#ifndef rumpdefs_h
#define rumpdefs_h

#ifdef NORUMP
#  define atomic_cas_32 __sync_val_compare_and_swap
#else
#  include <rump/rump.h>
#  include <rump/rumpdefs.h>
#  include <atomic.h> // atomic_cas_32 - interlocked compare and swap
#endif

#endif // rumpdefs_h
