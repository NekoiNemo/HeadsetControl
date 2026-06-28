#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis GameBuds (wireless earbuds + USB-C dongle)
 *
 * Features:
 * - Battery status
 *
 * Battery protocol:
 *   byte[3] left bud status (0x03 = out of case/active, 0x02 = docked/off)
 *   byte[4] right bud status
 *   byte[5] left bud battery % (direct 0-100 percentage)
 *   byte[6] right bud battery %
 * The earbuds report no charging state (docking a bud powers it off) and the charging
 * case has no wireless connection, so only active buds are reported. The lower of the two
 * active buds is surfaced, since that bud tends to be the control/passthrough one and drains
 * faster.
 */
class SteelSeriesArctisGamebuds : public protocols::SteelSeriesNovaDevice<SteelSeriesArctisGamebuds> {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x230a // Arctis GameBuds
    };

    static constexpr size_t LEFT_BUD_STATUS   = 3;
    static constexpr size_t RIGHT_BUD_STATUS  = 4;
    static constexpr size_t LEFT_BUD_BATTERY  = 5;
    static constexpr size_t RIGHT_BUD_BATTERY = 6;

    static constexpr uint8_t BUD_STATUS_ACTIVE = 0x03; // out of case; 0x02 = docked/off

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis GameBuds"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
            return { .usagepage = 0xffc0, .usageid = 0x1, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto status_result = readDeviceStatus(device_handle); // sends 0x00 0xb0
        if (!status_result) {
            return status_result.error();
        }
        auto& data = *status_result;

        if (data.size() <= RIGHT_BUD_BATTERY) {
            return DeviceError::protocolError("Battery status response too short");
        }

        // Consider only buds that are out of the case as docked bud reports 0%
        std::optional<int> level;
        if (data[LEFT_BUD_STATUS] == BUD_STATUS_ACTIVE) {
            level = data[LEFT_BUD_BATTERY];
        }
        if (data[RIGHT_BUD_STATUS] == BUD_STATUS_ACTIVE) {
            int right = data[RIGHT_BUD_BATTERY];
            level     = level ? std::min(*level, right) : right;
        }

        if (!level) {
            return DeviceError::deviceOffline("Earbuds are docked in the charging case");
        }

        return BatteryResult {
            .level_percent = std::min(*level, 100),
            .status        = BATTERY_AVAILABLE,
            .raw_data      = data
        };
    }
};

} // namespace headsetcontrol
