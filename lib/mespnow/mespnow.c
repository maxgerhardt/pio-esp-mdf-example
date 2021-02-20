// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp32/rom/crc.h"

#include "mdf_common.h"
#include "mespnow.h"

/**< in espnow_debug module, all ESP_LOG* will be redirected to espnow_debug,
     so the log of itself should be restored to other log channel, use ets_printf here */
#undef LOG_LOCAL_LEVEL
#undef LOG_FORMAT
#undef MDF_LOGE
#undef MDF_LOGW
#undef MDF_LOGI
#undef MDF_LOGD
#undef MDF_LOGV

#define LOG_LOCAL_LEVEL CONFIG_MESPNOW_LOG_LEVEL
#define LOG_FORMAT(letter, format)  LOG_COLOR_ ## letter #letter " (%d) [mespnow, %d]: " format LOG_RESET_COLOR "\n"

#define MDF_LOGE( format, ... ) if(LOG_LOCAL_LEVEL >= ESP_LOG_ERROR) { ets_printf(LOG_FORMAT(E, format), xTaskGetTickCount(), __LINE__, ##__VA_ARGS__); }
#define MDF_LOGW( format, ... ) if(LOG_LOCAL_LEVEL >= ESP_LOG_WARN) { ets_printf(LOG_FORMAT(W, format), xTaskGetTickCount(), __LINE__, ##__VA_ARGS__); }
#define MDF_LOGI( format, ... ) if(LOG_LOCAL_LEVEL >= ESP_LOG_INFO) { ets_printf(LOG_FORMAT(I, format), xTaskGetTickCount(), __LINE__, ##__VA_ARGS__); }
#define MDF_LOGD( format, ... ) if(LOG_LOCAL_LEVEL >= ESP_LOG_DEBUG) { ets_printf(LOG_FORMAT(D, format), xTaskGetTickCount(), __LINE__, ##__VA_ARGS__); }
#define MDF_LOGV( format, ... ) if(LOG_LOCAL_LEVEL >= ESP_LOG_VERBOSE) { ets_printf(LOG_FORMAT(V, format), xTaskGetTickCount(), __LINE__, ##__VA_ARGS__); }

#define SEND_CB_OK               BIT0
#define SEND_CB_FAIL             BIT1
#define MESPNOW_OUI_LEN          (2)
#define MESPNOW_SEND_RETRY_NUM   (3)

/**
 * @brief Data format for communication between two devices
 */
typedef struct {
    uint8_t oui[MESPNOW_OUI_LEN];    /**< Use oui to filter unexpect espnow package */
    uint8_t pipe;                    /**< Mespnow data stream pipe */
    uint8_t crc;                     /**< Value that is in little endian */
    uint8_t seq;                     /**< Sequence of espnow package when sending or receiving multiple package */
    uint8_t size;                    /**< The length of this packet of data */
    uint16_t total_size;             /**< Total length of data */
    uint32_t magic;                  /**< Filter duplicate packets */
    uint8_t payload[0];              /**< Data */
} __attribute__((packed)) mespnow_head_data_t;

/**
 * @brief Receive data packet temporarily store in queue
 */
typedef struct {
    uint8_t addr[ESP_NOW_ETH_ALEN]; /**< source MAC address  */
    mespnow_head_data_t data[0];    /**< Received data */
} mespnow_queue_data_t;

static const char *TAG                                     = "mespnow";
static bool g_espnow_init_flag                             = false;
static const uint8_t g_oui[MESPNOW_OUI_LEN]                = {0x4E, 0x4F}; /**< 'N', 'O' */

static EventGroupHandle_t g_event_group                    = NULL;
static xQueueHandle g_espnow_queue[MESPNOW_TRANS_PIPE_MAX] = {NULL};
static uint8_t g_espnow_queue_size[MESPNOW_TRANS_PIPE_MAX] = {CONFIG_MESPNOW_TRANS_PIPE_DEBUG_QUEUE_SIZE,
                                                              CONFIG_MESPNOW_TRANS_PIPE_CONTROL_QUEUE_SIZE,
                                                              CONFIG_MESPNOW_TRANS_PIPE_MCONFIG_QUEUE_SIZE,
                                                              CONFIG_MESPNOW_TRANS_PIPE_RESERVED_QUEUE_SIZE
                                                             };
static uint32_t g_last_magic[MESPNOW_TRANS_PIPE_MAX]       = {0};

/**< callback function of sending ESPNOW data */
static void mespnow_send_cb(const uint8_t *addr, esp_now_send_status_t status)
{
    if (!addr) {
        MDF_LOGW("Send cb args error, addr is NULL");
        return;
    }

    if (status == ESP_NOW_SEND_SUCCESS) {
        xEventGroupSetBits(g_event_group, SEND_CB_OK);
    } else {
        xEventGroupSetBits(g_event_group, SEND_CB_FAIL);
    }
}

/**< callback function of receiving ESPNOW data */
static void mespnow_recv_cb(const uint8_t *addr, const uint8_t *data, int size)
{
    if (!addr || !data || size < sizeof(mespnow_head_data_t)) {
        MDF_LOGD("Receive cb args error, addr: %p, data: %p, size: %d", addr, data, size);
        return;
    }

    mespnow_head_data_t *espnow_data = (mespnow_head_data_t *)data;
    xQueueHandle espnow_queue       = NULL;

    if (espnow_data->pipe >= MESPNOW_TRANS_PIPE_MAX) {
        MDF_LOGD("Device pipe error");
        return;
    }

    /**< filter unexpect espnow package */
    if (memcmp(espnow_data->oui, g_oui, MESPNOW_OUI_LEN)) {
        MDF_LOGD("Receive cb data fail");
        return; /**< mdf espnow oui field err */
    }

    if (g_last_magic[espnow_data->pipe] == espnow_data->magic) {
        MDF_LOGD("Receive duplicate packets, magic: 0x%x", espnow_data->magic);
        return;
    }

    g_last_magic[espnow_data->pipe] = espnow_data->magic;

    if (espnow_data->crc != crc8_le(UINT8_MAX, espnow_data->payload, espnow_data->size)) {
        MDF_LOGD("Receive cb CRC fail");
        return;
    }

    /**
     * @brief Received data packet store in special queue
     */
    espnow_queue = g_espnow_queue[espnow_data->pipe];

    if (espnow_data->seq == 0 && espnow_data->pipe != MESPNOW_TRANS_PIPE_DEBUG) {
        int pipe_tmp = espnow_data->pipe;

        /**< Send MDF_EVENT_MESPNOW_RECV event to the event handler */
        mdf_event_loop_send(MDF_EVENT_MESPNOW_RECV, (void *)pipe_tmp);
    }

    /**< espnow_queue is full */
    if (!uxQueueSpacesAvailable(espnow_queue)) {
        MDF_LOGD("espnow_queue is full");
        return ;
    }

    mespnow_queue_data_t *q_data = MDF_MALLOC(sizeof(mespnow_queue_data_t) + size);

    if (!q_data) {
        return;
    }

    memcpy(q_data->data, data, size);
    memcpy(q_data->addr, addr, ESP_NOW_ETH_ALEN);

    if (xQueueSend(espnow_queue, &q_data, 0) != pdPASS) {
        MDF_LOGD("Send receive queue failed");
        MDF_FREE(q_data);
    }
}

mdf_err_t mespnow_add_peer(wifi_interface_t ifx, const uint8_t *addr, const uint8_t *lmk)
{
    MDF_PARAM_CHECK(addr);

    /**< If peer exists, delete a peer from peer list */
    if (esp_now_is_peer_exist(addr)) {
        mdf_err_t ret = esp_now_del_peer(addr);
        MDF_ERROR_CHECK(ret != ESP_OK, ret, "esp_now_del_peer fail, ret: %d", ret);
        return MDF_OK;
    }

    mdf_err_t ret                  = MDF_OK;
    esp_now_peer_info_t peer       = {0x0};
    wifi_second_chan_t sec_channel = 0;

    ret = esp_wifi_get_channel(&peer.channel, &sec_channel);
    MDF_ERROR_CHECK(ret != ESP_OK, ret, "esp_wifi_get_channel, ret: 0x%x", ret);

    if (lmk) {
        peer.encrypt = true;
        memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    }

    peer.ifidx = ifx;
    memcpy(peer.peer_addr, addr, ESP_NOW_ETH_ALEN);

    /**< Add a peer to peer list */
    ret = esp_now_add_peer(&peer);
    MDF_ERROR_CHECK(ret != ESP_OK, ret, "Add a peer to peer list fail");

    return MDF_OK;
}

mdf_err_t mespnow_del_peer(const uint8_t *addr)
{
    MDF_PARAM_CHECK(addr);

    mdf_err_t ret = MDF_OK;

    /**< If peer exists, delete a peer from peer list */
    if (esp_now_is_peer_exist(addr)) {
        ret = esp_now_del_peer(addr);
        MDF_ERROR_CHECK(ret != ESP_OK, ret, "esp_now_del_peer fail, ret: %d", ret);
    }

    return MDF_OK;
}

mdf_err_t mespnow_write(mespnow_trans_pipe_e pipe, const uint8_t *dest_addr,
                        const void *data, size_t size, TickType_t wait_ticks)
{
    MDF_PARAM_CHECK(dest_addr);
    MDF_PARAM_CHECK(data);
    MDF_PARAM_CHECK(size > 0);
    MDF_PARAM_CHECK(pipe < MESPNOW_TRANS_PIPE_MAX);
    MDF_ERROR_CHECK(!g_espnow_init_flag, ESP_ERR_ESPNOW_NOT_INIT, "ESPNOW is not initialized");

    mdf_err_t ret                        = ESP_FAIL;
    mespnow_head_data_t *espnow_data     = NULL;
    ssize_t write_size                   = size;
    TickType_t write_ticks               = 0;
    uint32_t start_ticks                 = xTaskGetTickCount();
    static SemaphoreHandle_t s_send_lock = NULL;

    if (!s_send_lock) {
        s_send_lock = xSemaphoreCreateMutex();
    }

    /**< Wait for other tasks to be sent before send ESP-NOW data */
    if (xSemaphoreTake(s_send_lock, wait_ticks) != pdPASS) {
        return MDF_ERR_TIMEOUT;
    }

    espnow_data = MDF_MALLOC(ESP_NOW_MAX_DATA_LEN);
    MDF_ERROR_CHECK(!espnow_data, MDF_ERR_NO_MEM, "");

    espnow_data->pipe       = pipe;
    espnow_data->seq        = 0;
    espnow_data->total_size = write_size;
    memcpy(espnow_data->oui, g_oui, MESPNOW_OUI_LEN);

    do {
        write_ticks = (wait_ticks == portMAX_DELAY) ? portMAX_DELAY :
                      xTaskGetTickCount() - start_ticks < wait_ticks ?
                      wait_ticks - (xTaskGetTickCount() - start_ticks) : 0;
        espnow_data->size  = MIN(write_size, MESPNOW_PAYLOAD_LEN);
        espnow_data->crc   = crc8_le(UINT8_MAX, (uint8_t *)data, espnow_data->size);
        espnow_data->magic = esp_random();
        memcpy(espnow_data->payload, data, espnow_data->size);

        int retry_count    = CONFIG_MESPNOW_RETRANSMIT_NUM;
        EventBits_t uxBits = SEND_CB_FAIL;
        xEventGroupClearBits(g_event_group, SEND_CB_OK | SEND_CB_FAIL);

        do {
            /**< Send ESPNOW data */
            ret = esp_now_send(dest_addr, (uint8_t *)espnow_data,
                               espnow_data->size + sizeof(mespnow_head_data_t));
            MDF_ERROR_GOTO(ret != ESP_OK, EXIT, "<%s> esp_now_send", mdf_err_to_name(ret));

            /**< Waiting send complete ack from mac layer */
            uxBits = xEventGroupWaitBits(g_event_group, SEND_CB_OK | SEND_CB_FAIL,
                                         pdTRUE, pdFALSE, write_ticks);

            write_ticks = (wait_ticks == portMAX_DELAY) ? portMAX_DELAY :
                          xTaskGetTickCount() - start_ticks < wait_ticks ?
                          wait_ticks - (xTaskGetTickCount() - start_ticks) : 0;
        } while ((uxBits & SEND_CB_OK) != SEND_CB_OK && --retry_count);

        if ((uxBits & SEND_CB_OK) != SEND_CB_OK) {
            ret = ESP_FAIL;
            MDF_LOGW("Wait SEND_CB_OK fail");
            goto EXIT;
        }

        write_size -= MESPNOW_PAYLOAD_LEN;
        data       += MESPNOW_PAYLOAD_LEN;
        espnow_data->seq++;
    } while (write_size > 0);

    if (espnow_data->pipe != MESPNOW_TRANS_PIPE_DEBUG) {
        int pipe_tmp = espnow_data->pipe;

        /**< Send MDF_EVENT_MESPNOW_SEND event to the event handler */
        mdf_event_loop_send(MDF_EVENT_MESPNOW_SEND, (void *)pipe_tmp);
    }

EXIT:
    MDF_FREE(espnow_data);

    /**< ESP-NOW send completed, release send lock */
    xSemaphoreGive(s_send_lock);

    return ret;
}

mdf_err_t mespnow_read(mespnow_trans_pipe_e pipe, uint8_t *src_addr,
                       void *data, size_t *size, TickType_t wait_ticks)
{
    MDF_PARAM_CHECK(src_addr);
    MDF_PARAM_CHECK(data);
    MDF_PARAM_CHECK(size && *size > 0);
    MDF_PARAM_CHECK(pipe < MESPNOW_TRANS_PIPE_MAX);
    MDF_ERROR_CHECK(!g_espnow_init_flag, ESP_ERR_ESPNOW_NOT_INIT, "ESPNOW is not initialized");

    mespnow_head_data_t *espnow_data = NULL;
    mespnow_queue_data_t *q_data     = NULL;

    /**
     * @brief Receive data packet from special queue
     */
    xQueueHandle espnow_queue       = g_espnow_queue[pipe];
    uint32_t start_ticks            = xTaskGetTickCount();
    ssize_t read_size               = 0;
    size_t total_size               = 0;
    TickType_t recv_ticks           = 0;

    /**< Clear the data in the last buffer space */
    for (;;) {
        recv_ticks = (wait_ticks == portMAX_DELAY) ? portMAX_DELAY :
                     xTaskGetTickCount() - start_ticks < wait_ticks ?
                     wait_ticks - (xTaskGetTickCount() - start_ticks) : 0;

        if (xQueueReceive(espnow_queue, (void *)&q_data, recv_ticks) != pdPASS) {
            MDF_LOGD("Read queue timeout");
            return MDF_ERR_TIMEOUT;
        }

        espnow_data = q_data->data;

        /**< If the first packet data is not, the packet data is discarded. */
        if (espnow_data->seq == 0 && *size > espnow_data->total_size) {
            break;
        }

        MDF_LOGD("Expected sequence: 0, receive sequence: %d, size: %d",
                 espnow_data->seq, espnow_data->total_size);
        MDF_FREE(q_data);
    }

    read_size  = espnow_data->size;
    total_size = espnow_data->total_size;
    memcpy(src_addr, q_data->addr, ESP_NOW_ETH_ALEN);
    memcpy(data, espnow_data->payload, espnow_data->size);
    MDF_FREE(q_data);

    for (int expect_seq = 1, recv_ticks = 0; read_size < total_size; expect_seq++) {
        recv_ticks = (wait_ticks == portMAX_DELAY) ? portMAX_DELAY :
                     xTaskGetTickCount() - start_ticks < wait_ticks ?
                     wait_ticks - (xTaskGetTickCount() - start_ticks) : 0;

        if (xQueueReceive(espnow_queue, &q_data, recv_ticks) != pdPASS) {
            MDF_LOGW("Read queue timeout");
            return MDF_ERR_TIMEOUT;
        }

        espnow_data = q_data->data;
        MDF_LOGD("total_size: %d, read_total_size: %d, read_size: %d, expect_seq: %d, wait_ticks: %d",
                 espnow_data->total_size, read_size, espnow_data->size, expect_seq, wait_ticks);

        if (espnow_data->seq != expect_seq) {
            MDF_FREE(q_data);
            MDF_LOGW("Receive failed, part of the packet is lost");
            return ESP_FAIL;
        }

        memcpy(data + read_size, espnow_data->payload, espnow_data->size);
        read_size += espnow_data->size;

        MDF_FREE(q_data);
    }

    *size = read_size;
    return MDF_OK;
}

mdf_err_t mespnow_deinit(void)
{
    MDF_ERROR_CHECK(!g_espnow_init_flag, ESP_ERR_ESPNOW_NOT_INIT, "ESPNOW is not initialized");

    for (int i = 0; i < MESPNOW_TRANS_PIPE_MAX; ++i) {
        uint8_t *tmp = NULL;

        while (xQueueReceive(g_espnow_queue[i], &tmp, 0)) {
            MDF_FREE(tmp);
        }

        vQueueDelete(g_espnow_queue[i]);
        g_espnow_queue[i] = NULL;
    }

    vEventGroupDelete(g_event_group);
    g_event_group = NULL;

    /**< De-initialize ESPNOW function */
    ESP_ERROR_CHECK(esp_now_unregister_recv_cb());
    ESP_ERROR_CHECK(esp_now_unregister_send_cb());
    ESP_ERROR_CHECK(esp_now_deinit());

    g_espnow_init_flag = false;

    return MDF_OK;
}

mdf_err_t mespnow_init()
{
    if (g_espnow_init_flag) {
        return MDF_OK;
    }

    /**< Event group for espnow sent cb */
    g_event_group = xEventGroupCreate();
    MDF_ERROR_CHECK(!g_event_group, ESP_FAIL, "Create event group fail");

    /**< Create MESPNOW_TRANS_PIPE_MAX queue to distinguish data and temporarily store */
    for (int i = 0; i < MESPNOW_TRANS_PIPE_MAX; ++i) {
        g_espnow_queue[i] = xQueueCreate(g_espnow_queue_size[i],
                                         sizeof(mespnow_head_data_t *));
        MDF_ERROR_CHECK(!g_espnow_queue[i], ESP_FAIL, "Create espnow debug queue fail");
    }

    /**< Initialize ESPNOW function */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(mespnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(mespnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_MESPNOW_DEFAULT_PMK));

    g_espnow_init_flag = true;

    return MDF_OK;
}
