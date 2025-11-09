# Generic USB HAL - AIDL Implementation

This is a generic USB HAL implementation that can be used across multiple SoC vendors by configuring SoC-specific parameters through Android vendor properties instead of hardcoded values.

## Overview

The HAL supports:
- USB Type-C role switching (host/device)
- USB gadget functions (ADB, MTP, PTP, RNDIS, etc.)
- USB speed detection
- Optional I2C-based USB PD controllers
- CPU affinity configuration for USB IRQs

## Configuration via Vendor Properties

### Required Properties

#### `vendor.usb.controller`
**USB controller name (UDC gadget device name)**
- **Type:** String
- **Default:** Auto-detected from `/sys/class/udc/`
- **Description:** Name of the USB Device Controller gadget
- **Examples:**
  - Exynos: `11110000.dwc3`
  - Amlogic SM1/G12: `ff500000.usb`
  - Rockchip: `fe900000.usb`
  - TI AM62x: `31000000.usb`

**How to find your controller:**
```bash
adb shell ls /sys/class/udc/
```

#### `vendor.usb.typec.port`
**Type-C port name**
- **Type:** String
- **Default:** `port0`
- **Description:** Type-C port name in `/sys/class/typec/`
- **Examples:** `port0`, `port1`

**How to find your port:**
```bash
adb shell ls /sys/class/typec/
```

### Optional Properties

#### `vendor.usb.i2c.path`
**I2C controller path for USB PD**
- **Type:** String
- **Default:** Empty (disabled)
- **Description:** Path to I2C controller for external USB PD/Type-C chips (e.g., max77759)
- **Example:** `/sys/devices/platform/10d50000.hsi2c` (Exynos with max77759)
- **Note:** Most SoCs use integrated USB PD and don't need this. Leave empty if not applicable.

#### `vendor.usb.affinity.big`
**Big CPU core for USB IRQ affinity**
- **Type:** String (CPU core number)
- **Default:** `6`
- **Description:** Which big CPU core to pin USB interrupts to
- **Examples:** `6` (octa-core), `3` (quad-core)

#### `vendor.usb.affinity.medium`
**Medium CPU core for USB IRQ affinity**
- **Type:** String (CPU core number)
- **Default:** `4`
- **Description:** Which medium CPU core to use for USB
- **Examples:** `4`, `2`

#### `vendor.usb.accessory.limit_current`
**USB accessory current limit sysfs path**
- **Type:** String (relative path from I2C bus)
- **Default:** Empty (disabled)
- **Description:** Sysfs node for configuring USB accessory mode current limit (relative to I2C bus path)
- **Example:** `i2c-max77759tcpc/usb_limit_accessory_current` (Google Pixel with MAX77759)
- **Note:** Only needed for external USB PD controllers with accessory mode current limiting. Most SoCs don't need this.

#### `vendor.usb.accessory.limit_enable`
**USB accessory current limit enable sysfs path**
- **Type:** String (relative path from I2C bus)
- **Default:** Empty (disabled)
- **Description:** Sysfs node for enabling USB accessory mode current limit (relative to I2C bus path)
- **Example:** `i2c-max77759tcpc/usb_limit_accessory_enable` (Google Pixel with MAX77759)
- **Note:** Only needed for external USB PD controllers with accessory mode current limiting. Most SoCs don't need this.

## Configuration Examples

### Example 1: Amlogic SM1/G12 (Khadas VIM3L, Odroid-C4)

Add to `device.mk`:
```makefile
PRODUCT_VENDOR_PROPERTIES += \
    vendor.usb.controller=ff500000.usb \
    vendor.usb.typec.port=port0 \
    vendor.usb.affinity.big=3 \
    vendor.usb.affinity.medium=2
```

### Example 2: Rockchip RK3588

```makefile
PRODUCT_VENDOR_PROPERTIES += \
    vendor.usb.controller=fe900000.usb \
    vendor.usb.typec.port=port0 \
    vendor.usb.affinity.big=7 \
    vendor.usb.affinity.medium=4
```

### Example 3: Google Pixel with MAX77759 USB PD Controller

```makefile
PRODUCT_VENDOR_PROPERTIES += \
    vendor.usb.controller=11110000.dwc3 \
    vendor.usb.typec.port=port0 \
    vendor.usb.i2c.path=/sys/devices/platform/10d50000.hsi2c \
    vendor.usb.accessory.limit_current=i2c-max77759tcpc/usb_limit_accessory_current \
    vendor.usb.accessory.limit_enable=i2c-max77759tcpc/usb_limit_accessory_enable \
    vendor.usb.affinity.big=6 \
    vendor.usb.affinity.medium=4
```

**Note:** The accessory current limit properties are specific to devices with MAX77759 or similar USB PD controllers that support current limiting in accessory mode. Omit these for other platforms.

### Example 4: Auto-detection (No Configuration)

If you don't set `vendor.usb.controller`, the HAL will automatically detect the first USB controller found in `/sys/class/udc/`. This works for most single-USB-controller SoCs.

## SELinux Configuration

Add to your device's `sepolicy/property.te`:
```
vendor_internal_prop(vendor_usb_prop)
```

Add to your device's `sepolicy/property_contexts`:
```
vendor.usb.                    u:object_r:vendor_usb_prop:s0
```

## Build Integration

### 1. Add HAL to device packages

In your `device.mk`:
```makefile
PRODUCT_PACKAGES += \
    android.hardware.usb-service.generic
```

### 2. Copy manifest fragments

```makefile
PRODUCT_COPY_FILES += \
    hardware/baylibre/usb/aidl/android.hardware.usb-service.generic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/vintf/manifest/android.hardware.usb-service.generic.xml \
    hardware/baylibre/usb/aidl/android.hardware.usb.gadget-service.generic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/vintf/manifest/android.hardware.usb.gadget-service.generic.xml
```

## Kernel Requirements

Your kernel must support:
- USB Gadget ConfigFS
- USB Type-C framework (optional, for Type-C role switching)
- DWC3 USB controller (or compatible)

Check kernel config:
```
CONFIG_USB_CONFIGFS=y
CONFIG_USB_CONFIGFS_F_FS=y
CONFIG_USB_CONFIGFS_F_MTP=y
CONFIG_USB_CONFIGFS_F_PTP=y
CONFIG_USB_CONFIGFS_RNDIS=y
CONFIG_USB_DWC3=y
CONFIG_TYPEC=y (optional)
```

## Troubleshooting

### HAL logs
```bash
adb logcat | grep "android.hardware.usb"
```

Look for the configuration log on startup:
```
USB HAL Configuration:
  Controller: ff500000.usb
  UDC Path: /sys/class/udc/ff500000.usb/
  Speed Path: /sys/class/udc/ff500000.usb/current_speed
  Type-C Port: /sys/class/typec/port0/
  I2C Path: not configured
  CPU Affinity - Big: 3, Medium: 2
  Accessory Limit Current: not configured
  Accessory Limit Enable: not configured
```

### Check USB controller detection
```bash
adb shell ls /sys/class/udc/
```

### Check Type-C port
```bash
adb shell ls /sys/class/typec/
adb shell cat /sys/class/typec/port0/data_role
adb shell cat /sys/class/typec/port0/power_role
```

### Verify properties are set
```bash
adb shell getprop | grep vendor.usb
```

## Migration from Hardcoded HAL

If you're migrating from a SoC-specific HAL with hardcoded values:

1. Identify your USB controller name from `/sys/class/udc/`
2. Add the `vendor.usb.controller` property to your device.mk
3. Add SELinux property types and contexts
4. Remove the old SoC-specific HAL from PRODUCT_PACKAGES
5. Add this generic HAL to PRODUCT_PACKAGES
6. Test USB functionality (ADB, MTP, etc.)

## Supported SoCs

This generic HAL has been tested on:
- **Amlogic:** SM1, G12A, G12B (VIM3L, Odroid-C4, etc.)
- **Exynos:** (Original implementation)
- **Rockchip:** RK3588, RK3568 (configurable)
- **TI:** AM62x, AM67a (configurable)
- **Others:** Any SoC with DWC3 USB and ConfigFS support

## Contributing

To add support for a new SoC:
1. Identify the USB controller device name
2. Set `vendor.usb.controller` property
3. Test basic USB functionality
4. Add your SoC to the "Supported SoCs" list above
5. Submit example configuration

## License

Apache 2.0 - See LICENSE file
