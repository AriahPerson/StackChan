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

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // WifiBoard::StartNetwork() is stubbed — WiFi never starts.
    // Prevents the 300-400 mA radio spike that trips Pi 5 USB OC protection.
    GetHAL().init();

    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Install only AppPiControl — no launcher, no AI agent
    GetMooncake().installApp(std::make_unique<AppPiControl>());

    while (1) {
        GetHAL().feedTheDog();
        GetMooncake().update();
    }
}
