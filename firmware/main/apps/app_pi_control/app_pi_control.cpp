/*
 * SPDX-FileCopyrightText: 2026 KittyChan Project
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_pi_control.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <stackchan/stackchan.h>
#include <apps/common/common.h>
#include <smooth_lvgl.hpp>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <cJSON.h>
#include <cstring>
#include <cstdio>

using namespace mooncake;
using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace stackchan;

static constexpr auto TAG = "AppPiControl";

// USB-C via CH9102F bridge — appears as /dev/ttyUSB0 on Pi
static constexpr uart_port_t kPortCUart    = UART_NUM_0;
static constexpr int         kPortCTxGpio  = UART_PIN_NO_CHANGE;  // already set by IDF
static constexpr int         kPortCRxGpio  = UART_PIN_NO_CHANGE;  // already set by IDF
static constexpr int         kPortCBaud    = 115200;
static constexpr size_t      kUartBufSize  = 512;
static constexpr size_t      kLineBufSize  = 256;
static constexpr int         kCmdQueueSize = 8;

struct PiCommand {
    float pan      = 0.0f;
    float tilt     = 0.0f;
    int   speed    = 500;
    bool  has_pan  = false;
    bool  has_tilt = false;
    char  emotion[32] = {};
};

struct UartTaskArg {
    QueueHandle_t         queue;
    std::atomic<uint32_t>* rx_count;
};

static avatar::Emotion emotion_from_string(const char* name)
{
    if (!name)                            return avatar::Emotion::Neutral;
    if (strcasecmp(name, "happy")     == 0) return avatar::Emotion::Happy;
    if (strcasecmp(name, "sad")       == 0) return avatar::Emotion::Sad;
    if (strcasecmp(name, "angry")     == 0) return avatar::Emotion::Angry;
    if (strcasecmp(name, "surprised") == 0) return avatar::Emotion::Sleepy;
    if (strcasecmp(name, "doubt")     == 0) return avatar::Emotion::Doubt;
    if (strcasecmp(name, "sleepy")    == 0) return avatar::Emotion::Sleepy;
    return avatar::Emotion::Neutral;
}

static void uart_reader_task(void* arg)
{
    auto* targ = static_cast<UartTaskArg*>(arg);
    QueueHandle_t          queue    = targ->queue;
    std::atomic<uint32_t>* rx_count = targ->rx_count;

    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate      = kPortCBaud;
    uart_cfg.data_bits      = UART_DATA_8_BITS;
    uart_cfg.parity         = UART_PARITY_DISABLE;
    uart_cfg.stop_bits      = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl      = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk     = UART_SCLK_DEFAULT;

    uart_driver_install(kPortCUart, kUartBufSize * 2, 0, 0, nullptr, 0);
    uart_param_config(kPortCUart, &uart_cfg);

    char    line[kLineBufSize];
    int     line_len = 0;
    uint8_t byte     = 0;

    while (true) {
        int n = uart_read_bytes(kPortCUart, &byte, 1, pdMS_TO_TICKS(20));
        if (n <= 0) continue;

        if (byte == '\n' || byte == '\r') {
            if (line_len == 0) continue;
            line[line_len] = '\0';
            line_len = 0;

            cJSON* root = cJSON_Parse(line);
            if (!root) {
                mclog::tagWarn(TAG, "JSON parse failed");
                continue;
            }

            PiCommand cmd = {};

            cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "pan");
            if (cJSON_IsNumber(item)) {
                cmd.pan     = (float)item->valuedouble;
                cmd.has_pan = true;
            }

            item = cJSON_GetObjectItemCaseSensitive(root, "tilt");
            if (cJSON_IsNumber(item)) {
                cmd.tilt     = (float)item->valuedouble;
                cmd.has_tilt = true;
            }

            item = cJSON_GetObjectItemCaseSensitive(root, "speed");
            if (cJSON_IsNumber(item)) {
                cmd.speed = (int)item->valuedouble;
            }

            item = cJSON_GetObjectItemCaseSensitive(root, "emotion");
            if (cJSON_IsString(item) && item->valuestring) {
                strncpy(cmd.emotion, item->valuestring, sizeof(cmd.emotion) - 1);
            }

            cJSON_Delete(root);
            rx_count->fetch_add(1, std::memory_order_relaxed);
            xQueueSend(queue, &cmd, 0);
        } else {
            if (line_len < (int)kLineBufSize - 1) {
                line[line_len++] = (char)byte;
            }
        }
    }
}

AppPiControl::AppPiControl()
{
    setAppInfo().name = "PiControl";
}

void AppPiControl::onCreate()
{
    mclog::tagInfo(TAG, "on create");
    _cmd_queue = xQueueCreate(kCmdQueueSize, sizeof(PiCommand));
}

void AppPiControl::onOpen()
{
    mclog::tagInfo(TAG, "on open");

    {
        LvglLockGuard lock;
        auto avatar = std::make_unique<avatar::DefaultAvatar>();
        avatar->init(lv_screen_active());
        GetStackChan().attachAvatar(std::move(avatar));

        // Small diagnostic label in bottom-left corner
        _status_label = lv_label_create(lv_screen_active());
        lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(0x888888), 0);
        lv_obj_set_style_bg_opa(_status_label, LV_OPA_TRANSP, 0);
        lv_obj_align(_status_label, LV_ALIGN_BOTTOM_LEFT, 4, -4);
        lv_label_set_text(_status_label, "USB waiting...");
    }

    // Pass both queue and rx_count to the UART task
    static UartTaskArg task_arg;
    task_arg.queue    = _cmd_queue;
    task_arg.rx_count = &_rx_count;
    xTaskCreate(uart_reader_task, "pi_uart", 4096, &task_arg, 5, &_uart_task);
    mclog::tagInfo(TAG, "UART0 reader started (USB-C via CH9102F) @ {} baud", kPortCBaud);
}

void AppPiControl::onRunning()
{
    PiCommand cmd;
    while (xQueueReceive(_cmd_queue, &cmd, 0) == pdTRUE) {
        if (cmd.has_pan)  _last_pan  = cmd.pan;
        if (cmd.has_tilt) _last_tilt = cmd.tilt;

        if (cmd.has_pan || cmd.has_tilt) {
            GetStackChan().motion().lookAtNormalized(_last_pan, _last_tilt, cmd.speed);
        }

        if (cmd.emotion[0] != '\0') {
            LvglLockGuard lock;
            GetStackChan().addModifier(
                std::make_unique<TimedEmotionModifier>(emotion_from_string(cmd.emotion), 3000));
        }
    }

    // Update diagnostic label every ~30 frames
    static uint32_t frame = 0;
    if (_status_label && (++frame % 30 == 0)) {
        uint32_t n = _rx_count.load(std::memory_order_relaxed);
        char buf[32];
        if (n == 0) {
            snprintf(buf, sizeof(buf), "USB waiting...");
        } else {
            snprintf(buf, sizeof(buf), "USB rx:%lu", (unsigned long)n);
        }
        LvglLockGuard lock;
        lv_label_set_text(_status_label, buf);
    }

    {
        LvglLockGuard lock;
        GetStackChan().update();
    }
}

void AppPiControl::onClose()
{
    mclog::tagInfo(TAG, "on close");

    if (_uart_task) {
        vTaskDelete(_uart_task);
        _uart_task = nullptr;
    }
    uart_driver_delete(kPortCUart);

    {
        LvglLockGuard lock;
        _status_label = nullptr;
        GetStackChan().resetAvatar();
    }
}
