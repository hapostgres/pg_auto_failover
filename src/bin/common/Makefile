# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.

SRC_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.DEFAULT_GOAL := $(SRC_DIR)libpgaf_common.a

include $(SRC_DIR)Makefile.common

override CFLAGS += -I$(SRC_DIR)../pg_autoctl

clean:
	rm -f $(COMMON_OBJ) $(COMMON_LIB)
	rm -rf $(DEPDIR)

.PHONY: clean
