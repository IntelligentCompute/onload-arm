# SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
# X-SPDX-Copyright-Text: (c) Copyright 2004-2020 Xilinx, Inc.
GNU	    := 1
ifeq ($(CROSS_COMPILE),)
ifndef MMAKE_CTUNE
# Not all gcc's support -mtune=native, so we do a dummy invocation with that
# argument and only use the argument if the gcc invocation doesn't fail.
# Note that gcc takes empty STDIN, is told it is C (with -x c) and will create an output executable!
# Then use cond && a || b in order to set MMAKE_CTUNE := "-mtune=native" if the test compile worked
MMAKE_CTUNE := $(shell $(CC) -x c -c -mtune=native - -o /dev/null </dev/null >/dev/null 2>&1 && echo "-mtune=native" || echo "")

# GCC 10+ requires -mno-outline-atomics for ARM64 to avoid runtime issues
# Test if compiler supports this flag
MMAKE_OUTLINE_ATOMICS := $(shell $(CC) -x c -c -mno-outline-atomics - -o /dev/null </dev/null >/dev/null 2>&1 && echo "-mno-outline-atomics" || echo "")
endif

AARCH64_PAGE_SIZE := $(shell getconf PAGESIZE)

MMAKE_CARCH := -march=native $(MMAKE_CTUNE) $(MMAKE_OUTLINE_ATOMICS) -DAARCH64_PAGE_SIZE=$(AARCH64_PAGE_SIZE)

else
# There is no "native" arch when cross-compiling

AARCH64_PAGE_SIZE ?= 4096

MMAKE_CARCH := $(MMAKE_CTUNE) $(MMAKE_OUTLINE_ATOMICS) -DAARCH64_PAGE_SIZE=$(AARCH64_PAGE_SIZE)

endif

MMAKE_RELOCATABLE_LIB := -z combreloc

include $(TOPPATH)/mk/linux_gcc.mk

