/*
 * SPDX-FileCopyrightText: 2026 KittyChan Project
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <esp_wifi.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init: sets up display, servos (UART1), touch, IMU
    // WifiBoard::StartNetwork() fires during this call — shut WiFi down immediately after.
    GetHAL().init();

    // WiFi scanning/TX peaks at 200-400mA, pushing total USB draw over Pi 5's 500mA limit.
    // AppPiControl communicates over USB serial only — radio not needed.
    esp_wifi_stop();
    esp_wifi_deinit();

    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Install only AppPiControl — no launcher, no AI agent
    GetMooncake().installApp(std::make_unique<AppPiControl>());

    while (1) {
        GetHAL().feedTheDog();
        GetMooncake().update();
    }
}
