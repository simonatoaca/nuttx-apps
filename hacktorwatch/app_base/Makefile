############################################################################
# apps/examples/bme680/Makefile
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

include $(APPDIR)/Make.defs

PROGNAME = $(CONFIG_HACKTORWATCH_APP_BASE_PROGNAME)
PRIORITY = $(CONFIG_HACKTORWATCH_APP_BASE_PRIORITY)
STACKSIZE = $(CONFIG_HACKTORWATCH_APP_BASE_STACKSIZE)
MODULE = $(CONFIG_HACKTORWATCH_APP_BASE)

STEP_COUNTER_DIR = C-Step-Counter
STEP_COUNTER_SRC = C-Step-Counter/src/
STEP_COUNTER_URL = git@github.com:Ana-Mirza/C-Step-Counter.git

MAINSRC = app_base.c button_handler.c home.c menu.c haptic.c notification.c sys_timer.c app_timer.c
MAINSRC += nimble/ble_peer.c nimble/ble_print_utils.c nimble/gatt_svr.c nimble/ble_steps_svc.c nimble/nimble_main.c

MAINSRC += $(STEP_COUNTER_SRC)/detectionStage.c
MAINSRC += $(STEP_COUNTER_SRC)/filterStage.c
MAINSRC += $(STEP_COUNTER_SRC)/motionDetectStage.c
MAINSRC += $(STEP_COUNTER_SRC)/postProcessingStage.c
MAINSRC += $(STEP_COUNTER_SRC)/preProcessingStage.c
MAINSRC += $(STEP_COUNTER_SRC)/ringbuffer.c
MAINSRC += $(STEP_COUNTER_SRC)/scoringStage.c
MAINSRC += $(STEP_COUNTER_SRC)/StepCountingAlgo.c

CFLAGS += ${INCDIR_PREFIX}"$(STEP_COUNTER_DIR)$(DELIM)include"

MAINSRC += step_counter.c

ifeq ($(wildcard $(STEP_COUNTER_DIR )/.git),)
context::
	git clone $(STEP_COUNTER_URL)

distclean::
	$(call DELDIR,$(STEP_COUNTER_DIR))
endif

include $(APPDIR)/wireless/bluetooth/nimble/Makefile.nimble
include $(APPDIR)/Application.mk
