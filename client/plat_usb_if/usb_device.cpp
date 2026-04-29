/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#include "usb_device.h"
#include "usb_device_linux.h"

//------------------------------------------------------------------------------
// USBDevice::create
//
// Creates a platform specific USB device implementation
// A serial number may be given to attach to a specific instance of a device.
// If empty, then the first attached instance of the device found will be used.
//------------------------------------------------------------------------------
std::unique_ptr<USBDevice>
USBDevice::create(const USBDeviceIdentifier* devDesc, const std::string& serialNumber) {
    return std::unique_ptr<USBDevice>(new USBDeviceLinux(devDesc, serialNumber));
}
