/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <android-base/properties.h>
#include <string>

namespace aidl {
namespace android {
namespace hardware {
namespace usb {
namespace gadget {

using ::android::base::GetProperty;

/**
 * USB HAL Configuration Helper
 * Reads SoC-specific USB configuration from vendor properties
 */
class UsbConfig {
public:
    /**
     * Get USB controller name (UDC gadget name)
     * Property: vendor.usb.controller
     * Default: "" (auto-detect from /sys/class/udc/)
     * Example: "11110000.dwc3" (Exynos), "ff500000.usb" (Amlogic)
     */
    static std::string getControllerName() {
        return GetProperty("vendor.usb.controller", "");
    }

    /**
     * Get UDC path
     * Constructed from controller name if available
     */
    static std::string getUdcPath() {
        std::string controller = getControllerName();
        if (!controller.empty()) {
            return "/sys/class/udc/" + controller + "/";
        }
        // Fallback: try to auto-detect
        return "";
    }

    /**
     * Get I2C path for USB PD/Type-C controller
     * Property: vendor.usb.i2c.path
     * Default: "" (disabled)
     * Example: "/sys/devices/platform/10d50000.hsi2c"
     */
    static std::string getI2cPath() {
        return GetProperty("vendor.usb.i2c.path", "");
    }

    /**
     * Get Type-C port name
     * Property: vendor.usb.typec.port
     * Default: "port0"
     * Example: "port0", "port1"
     */
    static std::string getTypecPort() {
        return GetProperty("vendor.usb.typec.port", "port0");
    }

    /**
     * Get big core CPU affinity for USB IRQ
     * Property: vendor.usb.affinity.big
     * Default: "6"
     */
    static std::string getBigCore() {
        return GetProperty("vendor.usb.affinity.big", "6");
    }

    /**
     * Get medium core CPU affinity for USB IRQ
     * Property: vendor.usb.affinity.medium
     * Default: "4"
     */
    static std::string getMediumCore() {
        return GetProperty("vendor.usb.affinity.medium", "4");
    }

    /**
     * Check if I2C-based USB PD controller is enabled
     */
    static bool hasI2cController() {
        return !getI2cPath().empty();
    }

    /**
     * Get accessory current limit sysfs path (relative to I2C bus path)
     * Property: vendor.usb.accessory.limit_current
     * Default: "" (disabled)
     * Example: "i2c-max77759tcpc/usb_limit_accessory_current" (Pixel)
     */
    static std::string getAccessoryLimitCurrent() {
        return GetProperty("vendor.usb.accessory.limit_current", "");
    }

    /**
     * Get accessory current limit enable sysfs path (relative to I2C bus path)
     * Property: vendor.usb.accessory.limit_enable
     * Default: "" (disabled)
     * Example: "i2c-max77759tcpc/usb_limit_accessory_enable" (Pixel)
     */
    static std::string getAccessoryLimitEnable() {
        return GetProperty("vendor.usb.accessory.limit_enable", "");
    }
};

}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
}  // namespace aidl
