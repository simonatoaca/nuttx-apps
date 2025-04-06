/****************************************************************************
 * apps/examples/nimble/nimble_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sysinit/sysinit.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/boardctl.h>
#include <assert.h>

#include "netutils/netinit.h"

#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"
#include "services/ias/ble_svc_ias.h"
#include "services/lls/ble_svc_lls.h"
#include "services/tps/ble_svc_tps.h"
#include "services/bas/ble_svc_bas.h"
#include "services/dis/ble_svc_dis.h"

/* BLE */
#include "nimble/ble.h"

/* Application-specified header. */
#include "bleprph.h"
#include "blecent.h"


/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Not used now */

#define TASK_DEFAULT_PRIORITY       CONFIG_EXAMPLES_NIMBLE_PRIORITY
#define TASK_DEFAULT_STACK          NULL
#define TASK_DEFAULT_STACK_SIZE     4096

/****************************************************************************
 * External Functions Prototypes
 ****************************************************************************/

void ble_hci_sock_ack_handler(FAR void *param);
void ble_hci_sock_set_device(int dev);

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

static FAR void *ble_hci_sock_task(FAR void *param);
static FAR void *ble_host_task(FAR void *param);
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);
static int blecent_gap_event(struct ble_gap_event *event, void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char *g_gap_name = "HacktorWatch";
static uint8_t g_own_addr_type;

/****************************************************************************
 * Private Functions
 ****************************************************************************/
/**
 * Application callback.  Called when the read of the ANS Supported New Alert
 * Category characteristic has completed.
 */
static int
blecent_on_read(uint16_t conn_handle,
                const struct ble_gatt_error *error,
                struct ble_gatt_attr *attr,
                void *arg)
{
    printf( "Read complete; status=%d conn_handle=%d", error->status,
                conn_handle);
    if (error->status == 0) {
        printf( " attr_handle=%d value=", attr->handle);
        print_mbuf(attr->om);
    }
    printf( "\n");

    return 0;
}

/**
 * Application callback.  Called when the write to the ANS Alert Notification
 * Control Point characteristic has completed.
 */
static int
blecent_on_write(uint16_t conn_handle,
                 const struct ble_gatt_error *error,
                 struct ble_gatt_attr *attr,
                 void *arg)
{
    printf(
                "Write complete; status=%d conn_handle=%d attr_handle=%d\n",
                error->status, conn_handle, attr->handle);

    return 0;
}

/**
 * Application callback.  Called when the attempt to subscribe to notifications
 * for the ANS Unread Alert Status characteristic has completed.
 */
static int
blecent_on_subscribe(uint16_t conn_handle,
                     const struct ble_gatt_error *error,
                     struct ble_gatt_attr *attr,
                     void *arg)
{
    printf( "Subscribe complete; status=%d conn_handle=%d "
                      "attr_handle=%d\n",
                error->status, conn_handle, attr->handle);

    return 0;
}

/**
 * Performs three concurrent GATT operations against the specified peer:
 * 1. Reads the ANS Supported New Alert Category characteristic.
 * 2. Writes the ANS Alert Notification Control Point characteristic.
 * 3. Subscribes to notifications for the ANS Unread Alert Status
 *    characteristic.
 *
 * If the peer does not support a required service, characteristic, or
 * descriptor, then the peer lied when it claimed support for the alert
 * notification service!  When this happens, or if a GATT procedure fails,
 * this function immediately terminates the connection.
 */
static void
blecent_read_write_subscribe(const struct peer *peer)
{
    const struct peer_chr *chr;
    const struct peer_dsc *dsc;
    uint8_t value[2];
    int rc;

    /* Read the supported-new-alert-category characteristic. */
    chr = peer_chr_find_uuid(peer,
                             BLE_UUID16_DECLARE(BLECENT_SVC_ALERT_UUID),
                             BLE_UUID16_DECLARE(BLECENT_CHR_SUP_NEW_ALERT_CAT_UUID));
    if (chr == NULL) {
        printf( "Error: Peer doesn't support the Supported New "
                           "Alert Category characteristic\n");
        goto err;
    }

    rc = ble_gattc_read(peer->conn_handle, chr->chr.val_handle,
                        blecent_on_read, NULL);
    if (rc != 0) {
        printf( "Error: Failed to read characteristic; rc=%d\n",
                    rc);
        goto err;
    }

    /* Write two bytes (99, 100) to the alert-notification-control-point
     * characteristic.
     */
    chr = peer_chr_find_uuid(peer,
                             BLE_UUID16_DECLARE(BLECENT_SVC_ALERT_UUID),
                             BLE_UUID16_DECLARE(BLECENT_CHR_ALERT_NOT_CTRL_PT));
    if (chr == NULL) {
        printf( "Error: Peer doesn't support the Alert "
                           "Notification Control Point characteristic\n");
        goto err;
    }

    value[0] = 99;
    value[1] = 100;
    rc = ble_gattc_write_flat(peer->conn_handle, chr->chr.val_handle,
                              value, sizeof value, blecent_on_write, NULL);
    if (rc != 0) {
        printf( "Error: Failed to write characteristic; rc=%d\n",
                    rc);
    }

    /* Subscribe to notifications for the Unread Alert Status characteristic.
     * A central enables notifications by writing two bytes (1, 0) to the
     * characteristic's client-characteristic-configuration-descriptor (CCCD).
     */
    dsc = peer_dsc_find_uuid(peer,
                             BLE_UUID16_DECLARE(BLECENT_SVC_ALERT_UUID),
                             BLE_UUID16_DECLARE(BLECENT_CHR_UNR_ALERT_STAT_UUID),
                             BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
    if (dsc == NULL) {
        printf( "Error: Peer lacks a CCCD for the Unread Alert "
                           "Status characteristic\n");
        goto err;
    }

    value[0] = 1;
    value[1] = 0;
    rc = ble_gattc_write_flat(peer->conn_handle, dsc->dsc.handle,
                              value, sizeof value, blecent_on_subscribe, NULL);
    if (rc != 0) {
        printf( "Error: Failed to subscribe to characteristic; "
                           "rc=%d\n", rc);
        goto err;
    }

    return;

err:
    /* Terminate the connection. */
    ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

/**
 * Called when service discovery of the specified peer has completed.
 */
static void
blecent_on_disc_complete(const struct peer *peer, int status, void *arg)
{

    if (status != 0) {
        /* Service discovery failed.  Terminate the connection. */
        printf( "Error: Service discovery failed; status=%d "
                           "conn_handle=%d\n", status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    /* Service discovery has completed successfully.  Now we have a complete
     * list of services, characteristics, and descriptors that the peer
     * supports.
     */
    printf( "Service discovery complete; status=%d "
                       "conn_handle=%d\n", status, peer->conn_handle);

    /* Now perform three concurrent GATT procedures against the peer: read,
     * write, and subscribe to notifications.
     */
    blecent_read_write_subscribe(peer);
}

/**
 * Initiates the GAP general discovery procedure.
 */
static void
blecent_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;
    printf("Start scanning\n");

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        printf( "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      blecent_gap_event, NULL);
    if (rc != 0) {
        printf( "Error initiating GAP discovery procedure; rc=%d\n",
                    rc);
    }
}

/**
 * Indicates whether we should tre to connect to the sender of the specified
 * advertisement.  The function returns a positive result if the device
 * advertises connectability and support for the Alert Notification service.
 */
static int
blecent_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    uint16_t uuid = 0;
    int rc;
    int i;

    /* The device has to be advertising connectability. */
    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {

        return 0;
    }

    printf("Event type: %d\n", disc->event_type);
    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0) {
        return 0;
    }

    /* The device has to advertise support for the Alert Notification
     * service (0x1811).
     */
    printf("uuids: %d\n", fields.svc_data_uuid16_len);

    for (i = 0; i < fields.svc_data_uuid16_len / 2; i++) {
        uuid = *(((uint16_t *)fields.svc_data_uuid16) + i);
        printf("Checking uuid16: 0x%02x\n", uuid);
        if (uuid == BLECENT_SVC_ALERT_UUID) {
            return 1;
        }
    }

    return 0;
}

/**
 * Connects to the sender of the specified advertisement of it looks
 * interesting.  A device is "interesting" if it advertises connectability and
 * support for the Alert Notification service.
 */
static void
blecent_connect_if_interesting(const struct ble_gap_disc_desc *disc)
{
    uint8_t own_addr_type;
    int rc;

    /* Don't do anything if we don't care about this advertiser. */
    if (!blecent_should_connect(disc)) {
        return;
    }
    printf("Should connect\n");

    /* Scanning must be stopped before a connection can be initiated. */
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        printf( "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    /* Figure out address to use for connect (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        printf( "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */
    printf("Try to connect to ");
    print_addr(&disc->addr);
    printf("\n");
  
    rc = ble_gap_connect(own_addr_type, &disc->addr, 30000, NULL,
                         blecent_gap_event, NULL);
    if (rc != 0) {
        printf( "Error: Failed to connect to device; addr_type=%d "
                           "addr=%s\n; rc=%d",
                    disc->addr.type, addr_str(disc->addr.val), rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  blecent uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  blecent.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
blecent_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;
    printf("Gap event\n");

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        /* An advertisment report was received during GAP discovery. */
        print_adv_fields(&fields);

        /* Try to connect to the advertiser if it looks interesting. */
        blecent_connect_if_interesting(&event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0) {
            /* Connection successfully established. */
            printf( "Connection established ");

            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            printf( "\n");

            /* Remember peer. */
            rc = peer_add(event->connect.conn_handle);
            if (rc != 0) {
                printf( "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

            /* Perform service discovery. */
            rc = peer_disc_all(event->connect.conn_handle,
                               blecent_on_disc_complete, NULL);
            if (rc != 0) {
                printf( "Failed to discover services; rc=%d\n", rc);
                return 0;
            }
        } else {
            /* Connection attempt failed; resume scanning. */
            printf( "Error: Connection failed; status=%d\n",
                        event->connect.status);
            blecent_scan();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        /* Connection terminated. */
        printf( "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        printf( "\n");

        /* Forget about peer. */
        peer_delete(event->disconnect.conn.conn_handle);

        /* Resume scanning. */
        blecent_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        printf( "discovery complete; reason=%d\n",
                    event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        printf( "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        /* Peer sent us a notification or indication. */
        printf( "received %s; conn_handle=%d attr_handle=%d "
                          "attr_len=%d\n",
                    event->notify_rx.indication ?
                        "indication" :
                        "notification",
                    event->notify_rx.conn_handle,
                    event->notify_rx.attr_handle,
                    OS_MBUF_PKTLEN(event->notify_rx.om));

        /* Attribute data is contained in event->notify_rx.attr_data. */
        return 0;

    case BLE_GAP_EVENT_MTU:
        printf( "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        return 0;
    }
}

static void
blecent_on_reset(int reason)
{
    printf( "Resetting state; reason=%d\n", reason);
}

static void
blecent_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin scanning for a peripheral to connect to. */
    blecent_scan();
}

/**
 * Logs information about a connection to the console.
 */
static void
bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    printf( "handle=%d our_ota_addr_type=%d our_ota_addr=",
                desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    printf( " our_id_addr_type=%d our_id_addr=",
                desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    printf( " peer_ota_addr_type=%d peer_ota_addr=",
                desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    printf( " peer_id_addr_type=%d peer_id_addr=",
                desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    printf( " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
bleprph_advertise(void)
{
    uint8_t own_addr_type;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;
    printf("Advertising\n");

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        printf("error determining address type; rc=%d\n", rc);
        return;
    }

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assiging the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    fields.uuids16 = (ble_uuid16_t[]){
        BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        printf( "error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        printf( "error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unuesd by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        printf( "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            bleprph_print_conn_desc(&desc);

#if MYNEWT_VAL(BLEPRPH_LE_PHY_SUPPORT)
            phy_conn_changed(event->connect.conn_handle);
#endif
        }
        printf( "\n");

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising. */
            bleprph_advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        printf( "disconnect; reason=%d ", event->disconnect.reason);
        bleprph_print_conn_desc(&event->disconnect.conn);
        printf( "\n");

#if MYNEWT_VAL(BLEPRPH_LE_PHY_SUPPORT)
        phy_conn_changed(CONN_HANDLE_INVALID);
#endif

        /* Connection terminated; resume advertising. */
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        printf( "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        printf( "\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        printf( "advertise complete; reason=%d",
                    event->adv_complete.reason);
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        printf( "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        printf( "\n");
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        printf( "subscribe event; conn_handle=%d attr_handle=%d "
                          "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        printf( "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

#if MYNEWT_VAL(BLEPRPH_LE_PHY_SUPPORT)
    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        /* XXX: assume symmetric phy for now */
        phy_update(event->phy_updated.tx_phy);
        return 0;
#endif
    }

    return 0;
}

static void
bleprph_on_reset(int reason)
{
    printf( "Resetting state; reason=%d\n", reason);
}

static void
bleprph_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin advertising. */
    bleprph_advertise();
}

static void ble_periph_config(void)
{
  ble_hs_cfg.reset_cb = bleprph_on_reset;
  ble_hs_cfg.sync_cb = bleprph_on_sync;
  ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
}

static void ble_central_config(void)
{
  int rc = 0;

  ble_hs_cfg.reset_cb = blecent_on_reset;
  ble_hs_cfg.sync_cb = blecent_on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
}

/****************************************************************************
 * Name: ble_hci_sock_task
 ****************************************************************************/

static FAR void *ble_hci_sock_task(FAR void *param)
{
  ble_hci_sock_ack_handler(param);
  return NULL;
}

/****************************************************************************
 * Name: ble_host_task
 ****************************************************************************/

static FAR void *ble_host_task(FAR void *param)
{
  int is_periph = *((int *)param);

  // if (is_periph) {
  //   ble_periph_config();
  // } else {
    ble_central_config();
  // }

  ble_svc_gap_device_name_set(g_gap_name);
  nimble_port_run();
  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nimble_main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  struct ble_npl_task s_task_host;
  struct ble_npl_task s_task_hci;
  int                   ret = 0;
  int is_periph = 0;

  if (argc > 1) {
    is_periph = atoi(argv[1]);
  }

#ifndef CONFIG_NSH_ARCHINIT
  /* Perform architecture-specific initialization */

  boardctl(BOARDIOC_INIT, 0);
#endif

#ifndef CONFIG_NSH_NETINIT
  /* Bring up the network */

  netinit_bringup();
#endif

  nimble_port_init();

  if (ret < 0)
    {
      printf("nimble port init failed\n");
      return -1;
    }

  /* Initialize services */

  ble_svc_gap_init();
  ble_svc_gatt_init();
  // ble_svc_ans_init(); // Alert Notification Service
  // ble_svc_ias_init(); // Immediate Alert Service
  // ble_svc_lls_init(); // Link Loss Service
  // ble_svc_tps_init(); // TX Power Level Service
  // ble_svc_bas_init(); // Battery Level Service
  // ble_svc_dis_init(); // Device Information Service

  if (is_periph) {
    ret = gatt_svr_init();
  } else {
    ret = peer_init(1, 64, 64, 64);
  }

  /* Create task which handles HCI socket */

  ret = ble_npl_task_init(&s_task_hci, "hci_sock", ble_hci_sock_task,
                          NULL, TASK_DEFAULT_PRIORITY, BLE_NPL_TIME_FOREVER,
                          TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);
  if (ret != 0)
    {
      printf("ERROR: starting hci task: %i\n", ret);
    }

  /* Create task which handles default event queue for host stack. */

  ret = ble_npl_task_init(&s_task_host, "ble_host", ble_host_task,
                          &is_periph, TASK_DEFAULT_PRIORITY, BLE_NPL_TIME_FOREVER,
                          TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);
  if (ret != 0)
    {
      printf("ERROR: starting ble task: %i\n", ret);
    }


  while(1) {
    sleep(1);
  }
  return 0;
}
