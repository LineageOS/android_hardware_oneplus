/*
 * SPDX-FileCopyrightText: 2022-2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "AntiFlickerService"

#include <android-base/logging.h>
#include <livedisplay/oneplus/AntiFlicker.h>
#include <fstream>

namespace aidl {
namespace vendor {
namespace lineage {
namespace livedisplay {

static constexpr const char* kDcDimmingPath = "/sys/class/drm/card0-DSI-1/dimlayer_bl_en";

ndk::ScopedAStatus AntiFlicker::getEnabled(bool* _aidl_return) {
    std::ifstream file(kDcDimmingPath);
    int result = -1;
    if (file.fail()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    file >> result;
    LOG(DEBUG) << "Got result " << result << " fail " << file.fail();
    *_aidl_return = result > 0;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus AntiFlicker::setEnabled(bool enabled) {
    std::ofstream file(kDcDimmingPath);
    file << (enabled ? "1" : "0");
    if (file.fail()) {
        LOG(DEBUG) << "setEnabled fail " << file.fail();
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace livedisplay
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
