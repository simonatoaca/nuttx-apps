# ##############################################################################
# apps/examples/bme680/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

if(CONFIG_HACKTORWATCH_APP_BASE)
  nuttx_add_application(
    NAME
    ${CONFIG_HACKTORWATCH_APP_BASE_PROGNAME}
    PRIORITY
    ${CONFIG_HACKTORWATCH_APP_BASE_PRIORITY}
    STACKSIZE
    ${CONFIG_HACKTORWATCH_APP_BASE_STACKSIZE}
    MODULE
    ${CONFIG_HACKTORWATCH_APP_BASE}
    SRCS
    app_base.c
    button_handler.c
    home.c
    menu.c
    haptic.c
    notification.c
    sys_timer.c
    app_timer.c
    ${CMAKE_CURRENT_LIST_DIR}/nimble/nimble_main.c
    ${CMAKE_CURRENT_LIST_DIR}/nimble/ble_print_utils.c
    ${CMAKE_CURRENT_LIST_DIR}/nimble/ble_peer.c
    ${CMAKE_CURRENT_LIST_DIR}/nimble/gatt_svr.c
    INCLUDE_DIRECTORIES
    $<GENEX_EVAL:$<TARGET_PROPERTY:nimble,INCLUDE_DIRECTORIES>>
    DEPENDS
    )
  
endif()

nuttx_generate_kconfig(MENUDESC "HacktorWatch")
