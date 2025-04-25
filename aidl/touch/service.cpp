/*
 * SPDX-FileCopyrightText: 2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.touch@1.0-service.oneplus"

#include "TouchscreenGesture.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::vendor::lineage::touch::TouchscreenGesture;

int main() {
    binder_status_t status = STATUS_OK;

    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<TouchscreenGesture> tg = ndk::SharedRefBase::make<TouchscreenGesture>();

    const std::string instanceTg = std::string(TouchscreenGesture::descriptor) + "/default";
    status = AServiceManager_addService(tg->asBinder().get(), instanceTg.c_str());
    if (status != STATUS_OK) {
        LOG(WARNING) << "Can't register ITouchscreenGesture/default";
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
