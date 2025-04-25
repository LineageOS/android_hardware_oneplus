/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "TouchscreenGestureService"

#include <TouchscreenGesture.h>
#include <TouchscreenGestureConfig.h>
#include <android-base/logging.h>
#include <fstream>

namespace aidl {
namespace vendor {
namespace lineage {
namespace touch {

ndk::ScopedAStatus TouchscreenGesture::getSupportedGestures(std::vector<Gesture>* _aidl_return) {
    std::vector<Gesture> gestures;

    for (const auto& entry : kGestureInfoMap) {
        if (access(entry.second.path, F_OK) != -1) {
            gestures.push_back({entry.first, entry.second.name, entry.second.keycode});
        }
    }

    *_aidl_return = gestures;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TouchscreenGesture::setGestureEnabled(const Gesture& gesture, bool enabled) {
    const auto entry = kGestureInfoMap.find(gesture.id);
    if (entry == kGestureInfoMap.end()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    std::ofstream file(entry->second.path);
    file << (enabled ? "1" : "0");
    LOG(DEBUG) << "Wrote file " << entry->second.path << " fail " << file.fail();

    return ndk::ScopedAStatus::ok();
}

}  // namespace touch
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
