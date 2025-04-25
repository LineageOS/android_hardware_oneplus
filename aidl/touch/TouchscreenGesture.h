/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/lineage/touch/BnTouchscreenGesture.h>
#include <map>

namespace aidl {
namespace vendor {
namespace lineage {
namespace touch {

class TouchscreenGesture : public BnTouchscreenGesture {
  public:
    ndk::ScopedAStatus getSupportedGestures(std::vector<Gesture>* _aidl_return) override;
    ndk::ScopedAStatus setGestureEnabled(const Gesture& gesture, bool enabled) override;

    typedef struct {
        int32_t keycode;
        const char* name;
        const char* path;
    } GestureInfo;
};

}  // namespace touch
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
