/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

namespace aidl::android::hardware::biometrics::face {

// Transport between the st_auth SDK and its counterpart in the faceapp trusted app, as set up by
// the stock vendor.oneplus.faceunlock.hal service. Requests and responses are passed in ION
// buffers, whose addresses the QSEECom driver patches into the command.
class AuthTee {
  public:
    static bool open();
    static void close();

    // A TEECommunicationFunc, which carries no context, hence the static state.
    static int communicate(void* send, int sendLen, void* rsp, int rspLen);
};

}  // namespace aidl::android::hardware::biometrics::face
