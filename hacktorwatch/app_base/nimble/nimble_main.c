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
#include <hacktorwatch/context.h>
#include <hacktorwatch/common.h>

#ifdef CONFIG_GRAPHICS_LVGL
#include <lvgl/lvgl.h>
#endif

#include <nuttx/timers/timer.h>
#include <nuttx/input/buttons.h>
#include <nuttx/semaphore.h>

#include "netutils/netinit.h"

#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"

#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"

/* BLE */
#include "nimble/ble.h"

/* Application-specified header. */
#include "ble.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define TASK_DEFAULT_PRIORITY       100
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
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_advertise(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char *g_gap_name = "HacktorWatch";
static uint8_t g_own_addr_type;
static mqd_t notif_mq;

/****************************************************************************
 * Private Functions
 ****************************************************************************/
/**
 * Application callback.  Called when the read of the ANS Supported New Alert
 * Category characteristic has completed.
 */
static int
ble_on_read(uint16_t conn_handle,
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

static int
ble_on_read_time(uint16_t conn_handle,
                const struct ble_gatt_error *error,
                struct ble_gatt_attr *attr,
                void *arg)
{
  mq_send(notif_mq, attr->om->om_data, attr->om->om_len, NOTIF_TIME);

  return 0;
}

/**
 * Application callback.  Called when the attempt to subscribe to notifications
 * for the ANS Unread Alert Status characteristic has completed.
 */
static int
ble_on_subscribe(uint16_t conn_handle,
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
ble_read_ans(const struct peer *peer)
{
  const struct peer_chr *chr;
  int rc;

  /* Read the supported-new-alert-category characteristic. */
  chr = peer_chr_find_uuid(peer,
                          BLE_UUID16_DECLARE(BLECENT_SVC_ALERT_UUID),
                          BLE_UUID16_DECLARE(BLECENT_CHR_SUP_NEW_ALERT_CAT_UUID));
  if (chr == NULL) {
      printf( "Error: Peer doesn't support the Supported New "
                      "Alert Category characteristic\n");
      goto err_rd;
  }

  rc = ble_gattc_read(peer->conn_handle, chr->chr.val_handle,
                      ble_on_read, NULL);
  if (rc != 0) {
      printf( "Error: Failed to read characteristic; rc=%d\n",
                  rc);
      goto err_rd;
  }

  return;

err_rd:
  /* Terminate the connection. */
  // ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void
ble_subscribe_ans(const struct peer *peer)
{
    const struct peer_chr *chr;
    const struct peer_dsc *dsc;
    uint8_t value[2];
    int rc;

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
        goto err_sub;
    }

    value[0] = 1;
    value[1] = 0;
    rc = ble_gattc_write_flat(peer->conn_handle, dsc->dsc.handle,
                              value, sizeof value, ble_on_subscribe, NULL);
    if (rc != 0) {
        printf( "Error: Failed to subscribe to characteristic; "
                           "rc=%d\n", rc);
        goto err_sub;
    }

err_sub:
    /* Terminate the connection. */
    // ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void
ble_subscribe_curr_time(const struct peer *peer)
{
    const struct peer_chr *chr;
    const struct peer_dsc *dsc;
    uint8_t value[2];
    int rc;

    /* Subscribe to notifications for the Current Time characteristic.
     * A central enables notifications by writing two bytes (1, 0) to the
     * characteristic's client-characteristic-configuration-descriptor (CCCD).
     */
    dsc = peer_dsc_find_uuid(peer,
                             BLE_UUID16_DECLARE(BLECENT_SVC_CURRENT_TIME),
                             BLE_UUID16_DECLARE(BLECENT_CHR_CURRENT_TIME),
                             BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
    if (dsc == NULL) {
        printf( "Error: Peer lacks a CCCD for the Current Time "
                           "characteristic\n");
        goto err_sub;
    }

    value[0] = 1;
    value[1] = 0;
    rc = ble_gattc_write_flat(peer->conn_handle, dsc->dsc.handle,
                              value, sizeof value, ble_on_subscribe, NULL);
    if (rc != 0) {
        printf( "Error: Failed to subscribe to characteristic; "
                           "rc=%d\n", rc);
        goto err_sub;
    }

err_sub:
    /* Terminate the connection. */
    ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void
ble_read_curr_time(const struct peer *peer)
{
  const struct peer_chr *chr;
  int rc;

  /* Read the current time characteristic. */
  chr = peer_chr_find_uuid(peer,
                          BLE_UUID16_DECLARE(BLECENT_SVC_CURRENT_TIME),
                          BLE_UUID16_DECLARE(BLECENT_CHR_CURRENT_TIME));
  if (chr == NULL) {
      printf( "Error: Peer doesn't support the Current Time characteristic\n");
      goto err_rd;
  }

  rc = ble_gattc_read(peer->conn_handle, chr->chr.val_handle,
                      ble_on_read_time, NULL);
  if (rc != 0) {
      printf( "Error: Failed to read characteristic; rc=%d\n",
                  rc);
      goto err_rd;
  }

  return;

err_rd:
  /* Terminate the connection. */
  ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

/**
 * Called when service discovery of the specified peer has completed.
 */
static void
ble_on_disc_complete(const struct peer *peer, int status, void *arg)
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

  /* Read Services */

  // TODO: don t disconnect if these fail
  // ble_read_ans(peer);
  ble_read_curr_time(peer);

  // ble_subscribe_curr_time(peer);
}

/**
 * Logs information about a connection to the console.
 */
static void
ble_print_conn_desc(struct ble_gap_conn_desc *desc)
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

int
ble_sec_restart(uint16_t conn_handle,
                uint8_t key_size,
                uint8_t *ltk,
                uint16_t ediv,
                uint64_t rand_val,
                int auth)
{
  struct ble_store_value_sec value_sec;
  struct ble_store_key_sec key_sec;
  struct ble_gap_conn_desc desc;
  uint8_t conn_flags;
  int rc;

  if (ltk == NULL) {
      /* The user is requesting a store lookup. */
      rc = ble_gap_conn_find(conn_handle, &desc);
      if (rc != 0) {
          return rc;
      }

      memset(&key_sec, 0, sizeof key_sec);
      key_sec.peer_addr = desc.peer_id_addr;

      rc = ble_hs_atomic_conn_flags(conn_handle, &conn_flags);
      if (rc != 0) {
          return rc;
      }
      if (conn_flags & 0x01) {
          rc = ble_store_read_peer_sec(&key_sec, &value_sec);
      } else {
          rc = ble_store_read_our_sec(&key_sec, &value_sec);
      }
      if (rc != 0) {
          return rc;
      }

      ltk = value_sec.ltk;
      key_size = value_sec.key_size;
      ediv = value_sec.ediv;
      rand_val = value_sec.rand_num;
      auth = value_sec.authenticated;
  }

  rc = ble_gap_encryption_initiate(conn_handle, key_size, ltk,
                                    ediv, rand_val, auth);
  return rc;
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that is
 * established.  ble uses the same callback for all connections.
 *
 * @param event                 The event being signalled.
 * @param arg                   Application-specified argument; unused by
 *                                  ble.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
ble_gap_event(struct ble_gap_event *event, void *arg)
{
  int rc;
  struct ble_gap_conn_desc desc;
  char msg[MAX_NOTIFICATION_LEN];
  memset(msg, 0, MAX_NOTIFICATION_LEN);

  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
      /* A new connection was established or a connection attempt failed. */
      if (event->connect.status == 0) {
        /* Connection successfully established. */
        printf( "Connection established ");

        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
        assert(rc == 0);
        ble_print_conn_desc(&desc);
        printf( "\n");

        /* Remember peer. */
        rc = peer_add(event->connect.conn_handle);
        if (rc != 0) {
            printf( "Failed to add peer; rc=%d\n", rc);
            return 0;
        }

        // rc = ble_gap_security_initiate(event->connect.conn_handle);

        /* Perform service discovery. */
        rc = peer_disc_all(event->connect.conn_handle,
                          ble_on_disc_complete, NULL);
        if (rc != 0) {
            printf( "Failed to discover services; rc=%d\n", rc);
            return 0;
        }

        sprintf(msg, "connected\nstatus 0x%02x", rc);
        mq_send(notif_mq, msg, MAX_NOTIFICATION_LEN, NOTIF_NORMAL);
      } else {
        /* Connection attempt failed; resume advertising. */
        printf( "Error: Connection failed; status=%d\n",
                    event->connect.status);

        ble_advertise();
      }

      return 0;

  case BLE_GAP_EVENT_PAIRING_COMPLETE:
      if (event->pairing_complete.status == 0) {
        mq_send(notif_mq, "pair complete", MAX_NOTIFICATION_LEN, NOTIF_NORMAL);

        /* Perform service discovery. */
        rc = peer_disc_all(event->pairing_complete.conn_handle,
              ble_on_disc_complete, NULL);
        if (rc != 0) {
            printf( "Failed to discover services; rc=%d\n", rc);
            return 0;
        }
      } else {
        mq_send(notif_mq, "pair problem", MAX_NOTIFICATION_LEN, NOTIF_NORMAL);
      }
      return 0;

  case BLE_GAP_EVENT_DISCONNECT:
      /* Connection terminated. */
      printf( "disconnect; reason=%d ", event->disconnect.reason);
      print_conn_desc(&event->disconnect.conn);
      printf( "\n");

      /* Forget about peer. */
      peer_delete(event->disconnect.conn.conn_handle);

      sprintf(msg, "disconnected\nreason 0x%x", event->disconnect.reason);
      mq_send(notif_mq, msg, MAX_NOTIFICATION_LEN, NOTIF_NORMAL);

      /* Resume advertising. */
      ble_advertise();
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
      struct os_mbuf *om = event->notify_rx.om;

      event->notify_rx.om = NULL;
      /* Peer sent us a notification or indication. */
      printf( "received %s; conn_handle=%d attr_handle=%d "
                        "attr_len=%d\n",
                  event->notify_rx.indication ?
                      "indication" :
                      "notification",
                  event->notify_rx.conn_handle,
                  event->notify_rx.attr_handle,
                  OS_MBUF_PKTLEN(om));

      mq_send(notif_mq, (char *)om->om_data, om->om_len, NOTIF_ALERT);
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

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void
ble_advertise(void)
{
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  const char *name;
  int rc;
  printf("Advertising\n");

  /* Figure out address to use while advertising (no privacy for now) */
  rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
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

  // fields.uuids16 = (ble_uuid16_t[]){
  //     BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)
  // };
  // fields.num_uuids16 = 1;
  // fields.uuids16_is_complete = 1;

  // fields.uuids128 = (ble_uuid128_t[]){
  //   BLE_UUID128_INIT(GATT_SVR_STEPS_UUID)
  // };
  // fields.num_uuids128 = 1;
  // fields.uuids128_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
      printf( "error setting advertisement data; rc=%d\n", rc);
      return;
  }

  /* Begin advertising. */
  memset(&adv_params, 0, sizeof adv_params);
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                        &adv_params, ble_gap_event, NULL);
  if (rc != 0) {
      printf( "error enabling advertisement; rc=%d\n", rc);
      return;
  }
}

static void
ble_on_reset(int reason)
{
     printf( "Resetting state; reason=%d\n", reason);
}

static void
ble_on_sync(void)
{
  int rc;

  /* Make sure we have proper identity address set (public preferred) */
  rc = ble_hs_util_ensure_addr(0);
  assert(rc == 0);

  /* Begin advertising. */
  ble_advertise();
}

 /****************************************************************************
  * Name: ble_hci_sock_task
  ****************************************************************************/

static FAR void *ble_hci_sock_task(FAR void *param)
{
  set_cpu_affinity(1);
  ble_hci_sock_ack_handler(param);
  return NULL;
}

/****************************************************************************
 * Name: ble_host_task
 ****************************************************************************/

static FAR void *ble_host_task(FAR void *param)
{
  ble_hs_cfg.reset_cb = ble_on_reset;
  ble_hs_cfg.sync_cb = ble_on_sync;
  ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
  ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  ble_svc_gap_device_name_set(g_gap_name);
  // set_cpu_affinity(1);
  nimble_port_run();
  return NULL;
}

static int ble_create_task(struct ble_npl_task *t, const char *name, ble_npl_task_func_t func,
                        void *arg, uint8_t prio, ble_npl_time_t sanity_itvl,
                        ble_npl_stack_t *stack_bottom, uint16_t stack_size)
{
  int err;

  if ((t == NULL) || (func == NULL)) {
      return OS_INVALID_PARM;
  }

  err = pthread_attr_init(&t->attr);
  if (err) return err;
  err = pthread_attr_getschedparam (&t->attr, &t->param);
  if (err) return err;
#if CONFIG_RR_INTERVAL > 0
  err = pthread_attr_setschedpolicy(&t->attr, SCHED_RR);
  if (err) return err;
#endif
  t->param.sched_priority = prio;
  err = pthread_attr_setschedparam (&t->attr, &t->param);
  if (err) return err;

  t->name = name;
  err = pthread_create(&t->handle, &t->attr, func, arg);

  if (err == ENOMEM)
    {
      err = OS_ENOMEM;
    }
  else
    {
      pthread_setname_np(t->handle, t->name);
    }

  return err;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nimble_main
 ****************************************************************************/

int nimble(int argc, FAR char *argv[])
{
  struct ble_npl_task s_task_host;
  struct ble_npl_task s_task_hci;
  int                   ret = 0;
  struct mq_attr attr;

  attr.mq_maxmsg  = 5;
  attr.mq_msgsize = MAX_NOTIFICATION_LEN;
  attr.mq_flags   = 0;

  notif_mq = mq_open(NOTIF_MQ_NAME, O_CREAT | O_WRONLY, 0666, &attr);

  if (notif_mq < 0) {
    return EXIT_FAILURE;
  }

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
  ble_svc_steps_init();

  // ret = gatt_svr_init();
  ret = peer_init(1, 64, 64, 64);

  /* Create task which handles HCI socket */

  ret = ble_create_task(&s_task_hci, "hci_sock", ble_hci_sock_task,
                          NULL, TASK_DEFAULT_PRIORITY, BLE_NPL_TIME_FOREVER,
                          TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);
  if (ret != 0)
    {
      printf("ERROR: starting hci task: %i\n", ret);
    }

  /* Create task which handles default event queue for host stack. */

  ret = ble_create_task(&s_task_host, "ble_host", ble_host_task,
                          NULL, TASK_DEFAULT_PRIORITY, BLE_NPL_TIME_FOREVER,
                          TASK_DEFAULT_STACK, TASK_DEFAULT_STACK_SIZE);
  if (ret != 0)
    {
      printf("ERROR: starting ble task: %i\n", ret);
    }

  return 0;
}
