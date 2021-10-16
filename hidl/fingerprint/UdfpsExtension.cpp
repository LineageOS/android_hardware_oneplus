/*
 * Copyright (C) 2021 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include <compositionengine/UdfpsExtension.h>
#include <vendor/oneplus/hardware/display/1.0/IOneplusDisplay.h>

#define OP_DISPLAY_SET_DIM 10

using ::android::sp;
using ::vendor::oneplus::hardware::display::V1_0::IOneplusDisplay;

static const sp<IOneplusDisplay> gVendorDisplayService = IOneplusDisplay::getService();

uint32_t getUdfpsZOrder(uint32_t z, bool touched) {
    gVendorDisplayService->setMode(OP_DISPLAY_SET_DIM, !!touched);
    return touched ? 0xfc8 : z;
}

uint64_t getUdfpsUsageBits(uint64_t usageBits, bool) {
    return usageBits;
}
