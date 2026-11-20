# nosh (nornSHELL) as a module of a nornOS application. The application's app.mk says
#
#   MODULES += $(NORN_RTOS)/modules/nornSHELL/nosh.mk
#
# and the nornOS Makefile includes this file, which appends to MOD_SRC, MOD_INC and
# MOD_CFLAGS. Every source under src/ is compiled; the commands the application does not
# register with nosh_add() are dropped by the linker (--gc-sections), and so is nosh
# itself when nosh_enable() is never called. A nosh_config.h next to the application
# overrides the NOSH_CFG_* defaults of nosh.h.
NOSH_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

MOD_SRC += $(wildcard $(NOSH_DIR)/src/*.c)
MOD_INC += -I$(NOSH_DIR)/include
ifneq ($(wildcard $(APPDIR)/nosh_config.h),)
  MOD_CFLAGS += -DNOSH_CONFIG_H
endif
