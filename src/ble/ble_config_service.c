#include "ble_config_service.h"
#include "config/alert_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "ble_cfg";

// -------------------- Custom UUIDs --------------------
// Service:     aabbccdd-1234-5678-abcd-000000000001
// Orient chr:  aabbccdd-1234-5678-abcd-000000000002  (R/W encrypted)
// Thresh chr:  aabbccdd-1234-5678-abcd-000000000003  (R/W encrypted)
// Status chr:  aabbccdd-1234-5678-abcd-000000000004  (R/Notify encrypted)

static const ble_uuid128_t svc_uuid =
    BLE_UUID128_INIT(0x01,0x00,0x00,0x00,0x00,0x00,0xcd,0xab,
                     0x78,0x56,0x34,0x12,0xdd,0xcc,0xbb,0xaa);

static const ble_uuid128_t chr_orient_uuid =
    BLE_UUID128_INIT(0x02,0x00,0x00,0x00,0x00,0x00,0xcd,0xab,
                     0x78,0x56,0x34,0x12,0xdd,0xcc,0xbb,0xaa);

static const ble_uuid128_t chr_thresh_uuid =
    BLE_UUID128_INIT(0x03,0x00,0x00,0x00,0x00,0x00,0xcd,0xab,
                     0x78,0x56,0x34,0x12,0xdd,0xcc,0xbb,0xaa);

static const ble_uuid128_t chr_status_uuid =
    BLE_UUID128_INIT(0x04,0x00,0x00,0x00,0x00,0x00,0xcd,0xab,
                     0x78,0x56,0x34,0x12,0xdd,0xcc,0xbb,0xaa);

// -------------------- State --------------------

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_status_chr_val_handle;
static bool     s_notify_enabled = false;
static bool     s_paired = false;

static ble_orient_write_cb_t    s_orient_cb    = NULL;
static ble_threshold_write_cb_t s_threshold_cb = NULL;

static int16_t s_orient_vals[3] = {0, 0, 90};
static float   s_threshold_val  = 55.0f;
static ble_device_status_t s_last_status = {0};

// -------------------- Forward declarations --------------------

static int gap_event_handler(struct ble_gap_event *event, void *arg);
static void start_advertising(void);

// -------------------- Callbacks registration --------------------

void ble_config_service_set_orient_callback(ble_orient_write_cb_t cb)
{
    s_orient_cb = cb;
}

void ble_config_service_set_threshold_callback(ble_threshold_write_cb_t cb)
{
    s_threshold_cb = cb;
}

// -------------------- GATT Access Callback --------------------

static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *uuid = ctxt->chr->uuid;
    int rc;

    // ---- Orientation Characteristic ----
    if (ble_uuid_cmp(uuid, &chr_orient_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, s_orient_vals, sizeof(s_orient_vals));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len != 6) {
                ESP_LOGW(TAG, "Orient write: expected 6 bytes, got %d", len);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            int16_t vals[3];
            rc = ble_hs_mbuf_to_flat(ctxt->om, vals, sizeof(vals), NULL);
            if (rc != 0) return BLE_ATT_ERR_UNLIKELY;

            memcpy(s_orient_vals, vals, sizeof(vals));
            ESP_LOGI(TAG, "BLE Orient write: Roll=%d  Pitch=%d  Yaw=%d",
                     vals[0], vals[1], vals[2]);

            if (s_orient_cb) {
                s_orient_cb((float)vals[0], (float)vals[1], (float)vals[2]);
            }
            return 0;
        }
    }

    // ---- Crash Threshold Characteristic ----
    if (ble_uuid_cmp(uuid, &chr_thresh_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &s_threshold_val, sizeof(s_threshold_val));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len != sizeof(float)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            float val;
            rc = ble_hs_mbuf_to_flat(ctxt->om, &val, sizeof(val), NULL);
            if (rc != 0) return BLE_ATT_ERR_UNLIKELY;

            if (val < 5.0f || val > 90.0f) {
                ESP_LOGW(TAG, "Threshold %.1f° out of range [5-90]", val);
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }

            s_threshold_val = val;
            ESP_LOGI(TAG, "BLE Threshold write: %.1f°", val);

            if (s_threshold_cb) {
                s_threshold_cb(val);
            }
            return 0;
        }
    }

    // ---- Device Status Characteristic (read-only + notify) ----
    if (ble_uuid_cmp(uuid, &chr_status_uuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &s_last_status, sizeof(s_last_status));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// -------------------- GATT Service Definition (Encrypted) --------------------

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &chr_orient_uuid.u,
                .access_cb  = gatt_access_cb,
                .flags      = BLE_GATT_CHR_F_READ  | BLE_GATT_CHR_F_READ_ENC
                            | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid       = &chr_thresh_uuid.u,
                .access_cb  = gatt_access_cb,
                .flags      = BLE_GATT_CHR_F_READ  | BLE_GATT_CHR_F_READ_ENC
                            | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid       = &chr_status_uuid.u,
                .access_cb  = gatt_access_cb,
                .val_handle = &s_status_chr_val_handle,
                .flags      = BLE_GATT_CHR_F_READ  | BLE_GATT_CHR_F_READ_ENC
                            | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },
    { 0 }
};

// -------------------- Advertising --------------------

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)ble_svc_gap_device_name();
    fields.name_len = strlen(ble_svc_gap_device_name());
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.tx_pwr_lvl_is_present = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, gap_event_handler, NULL);

    ESP_LOGI(TAG, "BLE advertising started — '%s'", ble_svc_gap_device_name());
}

// -------------------- GAP Event Handler --------------------

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;

    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_paired = false;
            ESP_LOGI(TAG, "BLE connected (handle=%d) — waiting for pairing...", s_conn_handle);

            // Initiate security — force pairing before any data access
            ble_gap_security_initiate(s_conn_handle);
        } else {
            ESP_LOGW(TAG, "BLE connection failed, status=%d", event->connect.status);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_paired = false;
            s_notify_enabled = false;
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected (reason=%d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_paired = false;
        s_notify_enabled = false;
        start_advertising();
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        if (event->enc_change.status == 0 && desc.sec_state.encrypted) {
            s_paired = true;
            ESP_LOGI(TAG, "BLE encrypted & paired — GATT access granted");
            if (desc.sec_state.bonded) {
                ESP_LOGI(TAG, "BLE bonded — will auto-reconnect without PIN next time");
            }
        } else {
            ESP_LOGW(TAG, "BLE encryption failed (status=%d) — disconnecting", event->enc_change.status);
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "Passkey action: display passkey %06lu", (unsigned long)BLE_PASSKEY);
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            struct ble_sm_io pk = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = BLE_PASSKEY,
            };
            ble_sm_inject_io(event->passkey.conn_handle, &pk);
            ESP_LOGI(TAG, "Enter passkey on phone: %06lu", (unsigned long)BLE_PASSKEY);
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // Delete old bond and allow re-pairing
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        ESP_LOGI(TAG, "Re-pairing — old bond deleted");
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_status_chr_val_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Status notifications %s", s_notify_enabled ? "ENABLED" : "DISABLED");
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;

    default:
        break;
    }
    return 0;
}

// -------------------- BLE Host Task --------------------

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, NULL);
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
}

// -------------------- Public API --------------------

esp_err_t ble_config_service_init(const char *device_name)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE port init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // ---- Security Manager config (passkey pairing + bonding) ----
    ble_hs_cfg.sm_io_cap       = BLE_SM_IO_CAP_DISP_ONLY;   // ESP32 displays passkey
    ble_hs_cfg.sm_bonding      = 1;                          // enable bonding (remember paired devices)
    ble_hs_cfg.sm_mitm         = 1;                          // require MITM protection (passkey)
    ble_hs_cfg.sm_sc           = 1;                          // Secure Connections (LE SC)
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;


    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(device_name);

    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count cfg failed: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add svcs failed: %d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE GATT service initialized (secured, passkey=%06lu)", (unsigned long)BLE_PASSKEY);
    return ESP_OK;
}

void ble_config_service_notify_status(const ble_device_status_t *status)
{
    memcpy(&s_last_status, status, sizeof(s_last_status));

    if (!s_notify_enabled || !s_paired || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(status, sizeof(*status));
    if (om == NULL) return;

    ble_gatts_notify_custom(s_conn_handle, s_status_chr_val_handle, om);
}
