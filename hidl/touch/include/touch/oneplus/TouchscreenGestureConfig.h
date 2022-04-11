/*
 * Copyright (C) 2021-2022 The LineageOS Project
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

#include <map>

#include "TouchscreenGesture.h"

namespace vendor {
namespace lineage {
namespace touch {
namespace V1_0 {
namespace implementation {

const std::map<int32_t, TouchscreenGesture::GestureInfo> kGestureInfoMap = {
    {0, {1344, "Two fingers down swipe"}},
    {1, {1340, "Up arrow"}},
    {2, {1339, "Down arrow"}},
    {3, {1342, "Left arrow"}},
    {4, {1341, "Right arrow"}},
    {5, {1348, "One finger up swipe"}},
    {6, {1347, "One finger down swipe"}},
    {7, {1346, "One finger left swipe"}},
    {8, {1345, "One finger right swipe"}},
    {9, {1349, "Letter M"}},
    {10, {1343, "Letter O"}},
    {11, {1355, "Letter S"}},
    {12, {1350, "Letter W"}},
    {13, {1353, "Single Tap"}},
};

}  // namespace implementation
}  // namespace V1_0
}  // namespace touch
}  // namespace lineage
}  // namespace vendor
