/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

namespace aidl::android::hardware::biometrics::face {

// Passes st_auth requests to the faceapp trusted app like the stock faceunlock service does.
class AuthTee {
  public:
    static bool open();
    static void close();
    static int communicate(void* send, int sendLen, void* rsp, int rspLen);
};

}  // namespace aidl::android::hardware::biometrics::face
