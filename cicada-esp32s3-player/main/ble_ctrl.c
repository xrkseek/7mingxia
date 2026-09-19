/*
 * 7鸣匣蓝牙。
 * 写 0..6：这一只。匣子空闲就立刻亮灯播放；正在播则排到后面，播完自动下一位。
 * 写 L：退出等待（正在播的不取消）。
 * 通知：mine,ahead   mine=1 正在为你播放；ahead>0 前面还有几位。
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/nimble_npl.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_ctrl.h"
#include "web_portal.h"

static const char *TAG = "ble";

#define BLE_PEERS   4

/* 7e57000N-7e57-4000-8000-00805f9b34fb ，N=1 服务 / 2 写 / 3 通知 */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x40, 0x57, 0x7e, 0x01, 0x00, 0x57, 0x7e);
static const ble_uuid128_t s_cmd_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x40, 0x57, 0x7e, 0x02, 0x00, 0x57, 0x7e);
static const ble_uuid128_t s_state_uuid =
    BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                     0x00, 0x40, 0x57, 0x7e, 0x03, 0x00, 0x57, 0x7e);

static uint16_t s_state_handle;
static uint8_t s_own_addr_type;
static volatile int s_cmd;
static volatile bool *s_stop;
static volatile bool s_busy;
static volatile bool s_done;
static struct ble_npl_callout s_tick;
static int s_nconn;
static int s_playing = -1;

typedef struct {
    bool used;
    uint16_t conn;
    bool notify;
    int ord;
    int8_t track;
} peer_t;

static peer_t s_peers[BLE_PEERS];

void ble_store_config_init(void);

void ble_ctrl_bind_stop_flag(volatile bool *stop_flag)
{
    s_stop = stop_flag;
}

int ble_ctrl_take_cmd(void)
{
    int c = s_cmd;
    s_cmd = WEB_CMD_NONE;
    return c;
}

void ble_ctrl_mark_busy(void)
{
    s_busy = true;
}

void ble_ctrl_play_finished(void)
{
    s_done = true;
}

static int peer_by_conn(uint16_t conn)
{
    for (int i = 0; i < BLE_PEERS; i++) {
        if (s_peers[i].used && s_peers[i].conn == conn) {
            return i;
        }
    }
    return -1;
}

static int holder_i(void)
{
    int best = -1;
    int best_ord = 0x7fffffff;
    for (int i = 0; i < BLE_PEERS; i++) {
        if (s_peers[i].used && s_peers[i].ord >= 0 && s_peers[i].ord < best_ord) {
            best = i;
            best_ord = s_peers[i].ord;
        }
    }
    return best;
}

static bool device_busy(void)
{
    return s_playing >= 0 || s_busy;
}

static void enqueue(int i)
{
    if (i < 0 || s_peers[i].ord >= 0) {
        return;
    }
    int next = 0;
    for (int j = 0; j < BLE_PEERS; j++) {
        if (s_peers[j].used && s_peers[j].ord >= next) {
            next = s_peers[j].ord + 1;
        }
    }
    s_peers[i].ord = next;
}

static void start_peer(int i)
{
    s_playing = i;
    s_cmd = WEB_CMD_PLAY_TRACK_BASE + s_peers[i].track;
    ESP_LOGI(TAG, "play track %d", (int)s_peers[i].track);
}

static void dispatch_next(void)
{
    s_busy = false;
    if (s_playing >= 0 && s_playing < BLE_PEERS && s_peers[s_playing].used) {
        s_peers[s_playing].ord = -1;
    }
    s_playing = -1;
    int n = holder_i();
    if (n >= 0) {
        start_peer(n);
    }
}

static void request_track(int i, int track)
{
    if (i < 0 || track < 0 || track > 6) {
        return;
    }
    if (s_playing == i) {
        return;
    }
    s_peers[i].track = (int8_t)track;
    enqueue(i);
    if (!device_busy() && holder_i() == i) {
        start_peer(i);
    } else {
        ESP_LOGI(TAG, "queued track %d", track);
    }
}

static void fill_state(int i, char *out, size_t n)
{
    int mine = (i >= 0 && i == s_playing) ? 1 : 0;
    int ahead = 0;
    if (!mine && i >= 0 && s_peers[i].used && s_peers[i].ord >= 0) {
        for (int j = 0; j < BLE_PEERS; j++) {
            if (s_peers[j].used && s_peers[j].ord >= 0 && s_peers[j].ord < s_peers[i].ord) {
                ahead++;
            }
        }
    }
    snprintf(out, n, "%d,%d", mine, ahead);
}

static void notify_one(int i)
{
    if (i < 0 || !s_peers[i].used || !s_peers[i].notify || s_state_handle == 0) {
        return;
    }
    char buf[24];
    fill_state(i, buf, sizeof(buf));
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, strlen(buf));
    if (!om) {
        return;
    }
    ble_gatts_notify_custom(s_peers[i].conn, s_state_handle, om);
}

static void notify_all(void)
{
    for (int i = 0; i < BLE_PEERS; i++) {
        notify_one(i);
    }
}

static int gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    int i = peer_by_conn(conn_handle);

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char buf[24];
        fill_state(i, buf, sizeof(buf));
        int rc = os_mbuf_append(ctxt->om, buf, strlen(buf));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    char buf[20];
    uint16_t len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf) - 1, &len);
    if (rc != 0 || len == 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    buf[len] = '\0';

    if (buf[0] == 'L') {
        if (i >= 0 && i != s_playing) {
            s_peers[i].ord = -1;
        }
    } else if (buf[0] >= '0' && buf[0] <= '6') {
        request_track(i, buf[0] - '0');
    }

    notify_all();
    return 0;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_cmd_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &s_state_uuid.u,
                .access_cb = gatt_access,
                .val_handle = &s_state_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

static void advertise(void);

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        if (event->connect.status != 0) {
            advertise();
            break;
        }
        int slot = -1;
        for (int i = 0; i < BLE_PEERS; i++) {
            if (!s_peers[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            ble_gap_terminate(event->connect.conn_handle, BLE_ERR_CONN_LIMIT);
            break;
        }
        memset(&s_peers[slot], 0, sizeof(s_peers[slot]));
        s_peers[slot].used = true;
        s_peers[slot].conn = event->connect.conn_handle;
        s_peers[slot].ord = -1;
        s_nconn++;
        ESP_LOGI(TAG, "connect handle=%d n=%d", event->connect.conn_handle, s_nconn);
        advertise();
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        int i = peer_by_conn(event->disconnect.conn.conn_handle);
        if (i >= 0) {
            bool playing = i == s_playing;
            s_peers[i].ord = -1;
            if (playing) {
                s_playing = -1;
                if (s_stop) {
                    *s_stop = true;
                }
            }
            s_peers[i].used = false;
            notify_all();
        }
        if (s_nconn > 0) {
            s_nconn--;
        }
        ESP_LOGI(TAG, "disconnect n=%d", s_nconn);
        advertise();
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_state_handle) {
            int i = peer_by_conn(event->subscribe.conn_handle);
            if (i >= 0) {
                s_peers[i].notify = event->subscribe.cur_notify != 0;
                notify_one(i);
            }
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu=%d", event->mtu.value);
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    if (s_nconn >= BLE_PEERS || ble_gap_adv_active()) {
        return;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields %d", rc);
        return;
    }

    struct ble_gap_adv_params adv;
    memset(&adv, 0, sizeof(adv));
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start %d", rc);
    }
}

static void tick_cb(struct ble_npl_event *ev)
{
    (void)ev;
    if (s_done) {
        s_done = false;
        dispatch_next();
        notify_all();
    }
    ble_npl_callout_reset(&s_tick, ble_npl_time_ms_to_ticks32(200));
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure addr %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "addr type %d", rc);
        return;
    }
    ble_npl_callout_reset(&s_tick, ble_npl_time_ms_to_ticks32(200));
    advertise();
    ESP_LOGI(TAG, "advertising 7MingXia");
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_ctrl_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "nvs %s", esp_err_to_name(err));
        return err;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(s_svcs) != 0 || ble_gatts_add_svcs(s_svcs) != 0) {
        ESP_LOGE(TAG, "gatt add failed");
        return ESP_FAIL;
    }
    ble_svc_gap_device_name_set("7MingXia");
    ble_store_config_init();
    ble_npl_callout_init(&s_tick, nimble_port_get_dflt_eventq(), tick_cb, NULL);
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
