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

MAINSRC = app_base.c button_handler.c home.c menu.c haptic.c notification.c timer.c
MAINSRC += nimble/ble_peer.c nimble/ble_print_utils.c nimble/gatt_svr.c nimble/nimble_main.c

include $(APPDIR)/wireless/bluetooth/nimble/Makefile.nimble
include $(APPDIR)/Application.mk
