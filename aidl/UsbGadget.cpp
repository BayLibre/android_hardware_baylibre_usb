/*
 * Copyright (C) 2020 The Android Open Source Project
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

#define LOG_TAG "android.hardware.usb.gadget-service.generic"

#include "UsbGadget.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <aidl/android/frameworks/stats/IStats.h>

namespace aidl {
namespace android {
namespace hardware {
namespace usb {
namespace gadget {

string enabledPath;

UsbGadget::UsbGadget() : mGadgetIrqPath("") {
    loadConfiguration();
}

void UsbGadget::loadConfiguration() {
    // Load SoC-specific configuration from properties
    mGadgetName = UsbConfig::getControllerName();
    mI2cPath = UsbConfig::getI2cPath();
    mBigCore = UsbConfig::getBigCore();
    mMediumCore = UsbConfig::getMediumCore();
    mAccessoryLimitCurrent = UsbConfig::getAccessoryLimitCurrent();
    mAccessoryLimitEnable = UsbConfig::getAccessoryLimitEnable();

    // Auto-detect UDC controller if not specified
    if (mGadgetName.empty()) {
        DIR* dir = opendir("/sys/class/udc");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                if (entry->d_type == DT_LNK || entry->d_type == DT_DIR) {
                    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                        mGadgetName = entry->d_name;
                        ALOGI("Auto-detected USB controller: %s", mGadgetName.c_str());
                        break;
                    }
                }
            }
            closedir(dir);
        }
        if (mGadgetName.empty()) {
            ALOGE("Failed to detect USB controller, using default: 11110000.dwc3");
            mGadgetName = "11110000.dwc3";
        }
    } else {
        ALOGI("Using configured USB controller: %s", mGadgetName.c_str());
    }

    // Build paths from configuration
    mUdcPath = "/sys/class/udc/" + mGadgetName + "/";
    mSpeedPath = mUdcPath + "current_speed";

    // Build Type-C port path
    std::string typecPort = UsbConfig::getTypecPort();
    mTypecPortPath = std::string(kTypecBasePath) + typecPort + "/";

    ALOGI("USB HAL Configuration:");
    ALOGI("  Controller: %s", mGadgetName.c_str());
    ALOGI("  UDC Path: %s", mUdcPath.c_str());
    ALOGI("  Speed Path: %s", mSpeedPath.c_str());
    ALOGI("  Type-C Port: %s", mTypecPortPath.c_str());
    ALOGI("  I2C Path: %s", mI2cPath.empty() ? "not configured" : mI2cPath.c_str());
    ALOGI("  CPU Affinity - Big: %s, Medium: %s", mBigCore.c_str(), mMediumCore.c_str());
    ALOGI("  Accessory Limit Current: %s", mAccessoryLimitCurrent.empty() ? "not configured" : mAccessoryLimitCurrent.c_str());
    ALOGI("  Accessory Limit Enable: %s", mAccessoryLimitEnable.empty() ? "not configured" : mAccessoryLimitEnable.c_str());
}

Status UsbGadget::getUsbGadgetIrqPath() {
    std::string irqs;
    size_t read_pos = 0;
    size_t found_pos = 0;

    if (!ReadFileToString(kProcInterruptsPath, &irqs)) {
        ALOGE("cannot read all interrupts");
        return Status::ERROR;
    }

    while (true) {
        found_pos = irqs.find_first_of("\n", read_pos);
        if (found_pos == std::string::npos) {
            ALOGI("the string of all interrupts is unexpected");
            return Status::ERROR;
        }

        std::string single_irq = irqs.substr(read_pos, found_pos - read_pos);

        // Search for gadget controller IRQ (e.g., "dwc3", "usb@ff500000", etc.)
        if (single_irq.find("dwc3", 0) != std::string::npos ||
            single_irq.find("usb", 0) != std::string::npos ||
            single_irq.find(mGadgetName, 0) != std::string::npos) {
            unsigned int irq_number;
            size_t irq_pos = single_irq.find_first_of(":");
            if (!ParseUint(single_irq.substr(0, irq_pos), &irq_number)) {
                ALOGD("Could not parse IRQ number, trying next");
                read_pos = found_pos + 1;
                continue;
            }

            mGadgetIrqPath = kProcIrqPath + single_irq.substr(0, irq_pos) + kSmpAffinityList;
            ALOGI("Found USB gadget IRQ path: %s", mGadgetIrqPath.c_str());
            break;
        }

        if (found_pos == irqs.npos) {
            ALOGI("USB gadget doesn't start");
            return Status::ERROR;
        }

        read_pos = found_pos + 1;
    }

    return Status::SUCCESS;
}

void currentFunctionsAppliedCallback(bool functionsApplied, void *payload) {
    UsbGadget *gadget = (UsbGadget *)payload;
    gadget->mCurrentUsbFunctionsApplied = functionsApplied;
}

ScopedAStatus UsbGadget::getCurrentUsbFunctions(const shared_ptr<IUsbGadgetCallback>& callback,
                                                int64_t in_transactionId) {
    if (callback == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    ScopedAStatus ret = callback->getCurrentUsbFunctionsCb(
        mCurrentUsbFunctions,
        mCurrentUsbFunctionsApplied ? Status::FUNCTIONS_APPLIED : Status::FUNCTIONS_NOT_APPLIED,
	in_transactionId);
    if (!ret.isOk())
        ALOGE("Call to getCurrentUsbFunctionsCb failed %s", ret.getDescription().c_str());

    return ScopedAStatus::ok();
}

ScopedAStatus UsbGadget::getUsbSpeed(const shared_ptr<IUsbGadgetCallback> &callback,
	int64_t in_transactionId) {
    std::string current_speed;
    if (ReadFileToString(mSpeedPath, &current_speed)) {
        current_speed = Trim(current_speed);
        ALOGI("current USB speed is %s", current_speed.c_str());
        if (current_speed == "low-speed")
            mUsbSpeed = UsbSpeed::LOWSPEED;
        else if (current_speed == "full-speed")
            mUsbSpeed = UsbSpeed::FULLSPEED;
        else if (current_speed == "high-speed")
            mUsbSpeed = UsbSpeed::HIGHSPEED;
        else if (current_speed == "super-speed")
            mUsbSpeed = UsbSpeed::SUPERSPEED;
        else if (current_speed == "super-speed-plus")
            mUsbSpeed = UsbSpeed::SUPERSPEED_10Gb;
        else if (current_speed == "UNKNOWN")
            mUsbSpeed = UsbSpeed::UNKNOWN;
        else
            mUsbSpeed = UsbSpeed::UNKNOWN;
    } else {
        ALOGE("Fail to read current speed");
        mUsbSpeed = UsbSpeed::UNKNOWN;
    }

    if (callback) {
        ScopedAStatus ret = callback->getUsbSpeedCb(mUsbSpeed, in_transactionId);

        if (!ret.isOk())
            ALOGE("Call to getUsbSpeedCb failed %s", ret.getDescription().c_str());
    }

    return ScopedAStatus::ok();
}

Status UsbGadget::tearDownGadget() {
    return Status::SUCCESS;
}

ScopedAStatus UsbGadget::reset(const shared_ptr<IUsbGadgetCallback> &callback,
        int64_t in_transactionId) {
    if (callback)
        callback->resetCb(Status::SUCCESS, in_transactionId);
    return ScopedAStatus::ok();
}

Status UsbGadget::setupFunctions(long functions,
	const shared_ptr<IUsbGadgetCallback> &callback, uint64_t timeout,
	int64_t in_transactionId) {
    bool ffsEnabled = false;
    if (timeout == 0) {
	ALOGI("timeout not setup");
    }

    if ((functions & GadgetFunction::ADB) != 0) {
        ffsEnabled = true;
    }

    if ((functions & GadgetFunction::NCM) != 0) {
        ALOGI("setCurrentUsbFunctions ncm");
    }

    // Pull up the gadget right away when there are no ffs functions.
    if (!ffsEnabled) {
        mCurrentUsbFunctionsApplied = true;
        if (callback)
            callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS, in_transactionId);
        return Status::SUCCESS;
    }

    return Status::SUCCESS;
}

Status UsbGadget::getI2cBusHelper(string *name) {
    // Skip if I2C controller not configured
    if (mI2cPath.empty()) {
        ALOGD("I2C controller not configured, skipping");
        return Status::ERROR;
    }

    DIR *dp = opendir(mI2cPath.c_str());
    if (dp != NULL) {
        struct dirent *ep;

        while ((ep = readdir(dp))) {
            if (ep->d_type == DT_DIR) {
                if (string::npos != string(ep->d_name).find("i2c-")) {
                    std::strtok(ep->d_name, "-");
                    *name = std::strtok(NULL, "-");
                }
            }
        }
        closedir(dp);
        return Status::SUCCESS;
    }

    ALOGE("Failed to open I2C path: %s", mI2cPath.c_str());
    return Status::ERROR;
}

ScopedAStatus UsbGadget::setCurrentUsbFunctions(int64_t functions,
                                               const shared_ptr<IUsbGadgetCallback> &callback,
					       int64_t timeoutMs,
					       int64_t in_transactionId) {
    std::unique_lock<std::mutex> lk(mLockSetCurrentFunction);

    string accessoryCurrentLimitEnablePath, accessoryCurrentLimitPath, path;

    mCurrentUsbFunctions = functions;
    mCurrentUsbFunctionsApplied = false;

    // Only configure I2C accessory limits if configured via properties
    if (!mAccessoryLimitCurrent.empty() && !mAccessoryLimitEnable.empty() &&
        !mI2cPath.empty() && getI2cBusHelper(&path) == Status::SUCCESS) {
        std::string i2cBasePath = mI2cPath + "/i2c-";
        accessoryCurrentLimitPath = i2cBasePath + path + "/" + mAccessoryLimitCurrent;
        accessoryCurrentLimitEnablePath = i2cBasePath + path + "/" + mAccessoryLimitEnable;
    } else {
        ALOGD("Accessory current limits not configured or I2C controller not available");
    }

    // Get the gadget IRQ number before tearDownGadget()
    if (mGadgetIrqPath.empty())
        getUsbGadgetIrqPath();

    // Unlink the gadget and stop the monitor if running.
    Status status = tearDownGadget();
    if (status != Status::SUCCESS) {
        goto error;
    }

    ALOGI("Returned from tearDown gadget");

    // Leave the gadget pulled down to give time for the host to sense disconnect.
    //usleep(kDisconnectWaitUs);

    if (functions == GadgetFunction::NONE) {
        if (callback == NULL)
            return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "callback == NULL");
        ScopedAStatus ret = callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS, in_transactionId);
        if (!ret.isOk())
            ALOGE("Error while calling setCurrentUsbFunctionsCb %s", ret.getDescription().c_str());
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Error while calling setCurrentUsbFunctionsCb");
    }

    status = setupFunctions(functions, callback, timeoutMs, in_transactionId);
    if (status != Status::SUCCESS) {
        goto error;
    }

    if (functions & GadgetFunction::NCM) {
        if (!mGadgetIrqPath.empty()) {
            if (!WriteStringToFile(mBigCore, mGadgetIrqPath))
                ALOGI("Cannot move gadget IRQ to big core, path:%s", mGadgetIrqPath.c_str());
        }
    } else {
        if (!mGadgetIrqPath.empty()) {
            if (!WriteStringToFile(mMediumCore, mGadgetIrqPath))
                ALOGI("Cannot move gadget IRQ to medium core, path:%s", mGadgetIrqPath.c_str());
        }
    }

    // Note: Platform-specific USB type/power mode detection not available on this SoC
    // The following code is disabled as CURRENT_USB_TYPE_PATH and
    // CURRENT_USB_POWER_OPERATION_MODE_PATH are platform-specific (e.g., Pixel devices)
    // and not applicable to generic implementations.

    // For platforms that support accessory current limiting, configure via UsbConfig properties:
    // vendor.usb.accessory.limit_current and vendor.usb.accessory.limit_enable

    // Simplified accessory current limit handling
    if (functions & GadgetFunction::ACCESSORY) {
        if (!accessoryCurrentLimitPath.empty() && !accessoryCurrentLimitEnablePath.empty()) {
            if (!WriteStringToFile("1300000", accessoryCurrentLimitPath)) {
                ALOGI("Write 1.3A to limit current fail");
            } else {
                if (!WriteStringToFile("1", accessoryCurrentLimitEnablePath)) {
                    ALOGI("Enable limit current fail");
                }
            }
        }
    } else {
        if (!accessoryCurrentLimitEnablePath.empty()) {
            if (!WriteStringToFile("0", accessoryCurrentLimitEnablePath))
                ALOGI("unvote accessory limit current failed");
        }
    }

    ALOGI("Usb Gadget setcurrent functions called successfully");
    return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Usb Gadget setcurrent functions called successfully");


error:
    ALOGI("Usb Gadget setcurrent functions failed");
    if (callback == NULL)
        return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Usb Gadget setcurrent functions failed");
    ScopedAStatus ret = callback->setCurrentUsbFunctionsCb(functions, status, in_transactionId);
    if (!ret.isOk())
        ALOGE("Error while calling setCurrentUsbFunctionsCb %s", ret.getDescription().c_str());
    return ScopedAStatus::fromServiceSpecificErrorWithMessage(
                -1, "Error while calling setCurrentUsbFunctionsCb");
}
}  // namespace gadget
}  // namespace usb
}  // namespace hardware
}  // namespace android
}  // aidl
