/*
 * SPDX-FileCopyrightText: 2019-2025 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.livedisplay-service-oneplus"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <binder/ProcessState.h>
#include <livedisplay/oneplus/AntiFlicker.h>
#include <livedisplay/oneplus/SunlightEnhancement.h>
#include <livedisplay/sdm/PictureAdjustment.h>

using ::aidl::vendor::lineage::livedisplay::AntiFlicker;
using ::aidl::vendor::lineage::livedisplay::SunlightEnhancement;
using ::aidl::vendor::lineage::livedisplay::sdm::PictureAdjustment;
using ::aidl::vendor::lineage::livedisplay::sdm::SDMController;

int main() {
    android::ProcessState::self()->setThreadPoolMaxThreadCount(1);
    android::ProcessState::self()->startThreadPool();

    LOG(INFO) << "LiveDisplay HAL service is starting.";

    std::shared_ptr<SDMController> controller = ENABLE_PA ? std::make_shared<SDMController>() : nullptr;

    std::shared_ptr<AntiFlicker> af = ENABLE_AF ? ndk::SharedRefBase::make<AntiFlicker>() : nullptr;
    std::shared_ptr<PictureAdjustment> pa =
            ENABLE_PA ? ndk::SharedRefBase::make<PictureAdjustment>(controller) : nullptr;
    std::shared_ptr<SunlightEnhancement> se =
            ENABLE_SE ? ndk::SharedRefBase::make<SunlightEnhancement>() : nullptr;

    if (af) {
        std::string instance = std::string() + AntiFlicker::descriptor + "/default";
        binder_status_t status = AServiceManager_addService(af->asBinder().get(), instance.c_str());
        CHECK_EQ(status, STATUS_OK);
    }

    if (pa) {
        std::string instance = std::string() + PictureAdjustment::descriptor + "/default";
        binder_status_t status = AServiceManager_addService(pa->asBinder().get(), instance.c_str());
        CHECK_EQ(status, STATUS_OK);
    }

    if (se) {
        std::string instance = std::string() + SunlightEnhancement::descriptor + "/default";
        binder_status_t status = AServiceManager_addService(se->asBinder().get(), instance.c_str());
        CHECK_EQ(status, STATUS_OK);
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
