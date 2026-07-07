/*
 * SPDX-FileCopyrightText: 2026 KittyChan Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <atomic>
#include <lvgl.h>

/**
 * @brief Listens on PORT.C UART for newline-delimited JSON commands from the Pi.
 *
 * Protocol: {"pan": float, "tilt": float, "emotion": str, "speed": int}\n
 *   pan/tilt: -1.0 to 1.0 (normalized, optional)
 *   emotion:  "happy"|"sad"|"angry"|"surprised"|"doubt"|"neutral" (optional)
 *   speed:    0-1000 (optional, default 500)
 */
class AppPiControl : public mooncake::AppAbility {
public:
    AppPiControl();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    float _last_pan  = 0.0f;
    float _last_tilt = 0.0f;

    QueueHandle_t _cmd_queue = nullptr;
    TaskHandle_t  _uart_task = nullptr;

    // Shared between UART reader task and main loop for status display
    std::atomic<uint32_t> _rx_count{0};
    lv_obj_t* _status_label = nullptr;
};
