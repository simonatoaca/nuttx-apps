/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <string.h>

#include "sysinit/sysinit.h"
#include "syscfg/syscfg.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "ble.h"

/* Characteristic value handles */
#if MYNEWT_VAL(BLE_SVC_STEPS_CNT_NOTIFY_ENABLE) > 0
static uint16_t ble_svc_steps_handle;
#endif

/* Step count */
uint16_t ble_svc_steps_cnt;

/* Access function */
static int
ble_svc_steps_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_steps_defs[] = {
    {
        /*** Steps Service. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(BLE_SVC_STEPS_UUID128),
        .characteristics = (struct ble_gatt_chr_def[]) { {
	    /*** Steps level characteristic */
            .uuid = BLE_UUID128_DECLARE(BLE_SVC_STEPS_CNT_CHR_UUID128),
            .access_cb = ble_svc_steps_access,
#if MYNEWT_VAL(BLE_SVC_STEPS_CNT_NOTIFY_ENABLE) > 0
	    .val_handle = &ble_svc_steps_handle,
#endif
            .flags = BLE_GATT_CHR_F_READ
#if MYNEWT_VAL(BLE_SVC_STEPS_CNT_NOTIFY_ENABLE) > 0
	          |  BLE_GATT_CHR_F_NOTIFY
#endif
	    }, {
            0, /* No more characteristics in this service. */
        } },
    },

    {
        0, /* No more services. */
    },
};

/**
 * STEPS access function
 */
static int
ble_svc_steps_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    int rc;

    assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
    rc = os_mbuf_append(ctxt->om, &ble_svc_steps_cnt,
                        sizeof ble_svc_steps_cnt);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/**
 * Set the steps count.
 */
int
ble_svc_steps_cnt_set(uint16_t cnt) {

    if (ble_svc_steps_cnt != cnt) {
	    ble_svc_steps_cnt = cnt;
#if MYNEWT_VAL(BLE_SVC_STEPS_CNT_NOTIFY_ENABLE) > 0
        ble_gatts_chr_updated(ble_svc_steps_steps_handle);
#endif
    }
    return 0;
}

/**
 * Initialize the Steps Service.
 */
void
ble_svc_steps_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = ble_gatts_count_cfg(ble_svc_steps_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_add_svcs(ble_svc_steps_defs);
    SYSINIT_PANIC_ASSERT(rc == 0);
}
