/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <map>

#include "TouchscreenGesture.h"

namespace aidl {
namespace vendor {
namespace lineage {
namespace touch {

const std::map<int32_t, TouchscreenGesture::GestureInfo> kGestureInfoMap = {
    {0, {251, "Two fingers down swipe", "/proc/touchpanel/double_swipe_enable"}},
    {1, {252, "Down arrow", "/proc/touchpanel/down_arrow_enable"}},
    {2, {253, "Left arrow", "/proc/touchpanel/left_arrow_enable"}},
    {3, {254, "Right arrow", "/proc/touchpanel/right_arrow_enable"}},
    {4, {247, "Letter M", "/proc/touchpanel/letter_m_enable"}},
    {5, {250, "Letter O", "/proc/touchpanel/letter_o_enable"}},
    {6, {248, "Letter S", "/proc/touchpanel/letter_s_enable"}},
    {7, {246, "Letter W", "/proc/touchpanel/letter_w_enable"}},
    {8, {255, "Single Tap", "/proc/touchpanel/single_tap_enable"}},
};

}  // namespace touch
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
