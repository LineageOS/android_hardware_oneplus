/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace aidl::android::hardware::biometrics::face {

// Client of the faceapp trusted app, which stores one face feature and compares against it.
// The stock service keeps the feature and the frames on two separate QSEECom sessions; the trusted
// app rejects commands that are issued on the wrong session, so both are mirrored here.
class FaceTee {
  public:
    static constexpr int kFrameWidth = 640;
    static constexpr int kFrameHeight = 480;
    static constexpr size_t kFrameSize = kFrameWidth * kFrameHeight * 3 / 2;
    static constexpr int kMaxFrames = 5;

    ~FaceTee();

    bool open();
    bool saveFeature(const uint8_t* feature, size_t size);
    bool removeFeature();
    // Returns the similarity score in [0, 1].
    std::optional<float> compare(const uint8_t* feature, size_t size);
    bool saveFrame(int index, const uint8_t* nv21);
    bool removeFrame(int index);

  private:
    struct QSEECom_handle {
        uint8_t* ion_sbuffer;
    };

    bool load();
    bool start(QSEECom_handle** handle);
    uint32_t send(QSEECom_handle* handle, size_t requestSize, size_t responseSize);

    void* mLib = nullptr;
    QSEECom_handle* mFeatureHandle = nullptr;
    QSEECom_handle* mFrameHandle = nullptr;
    int (*mStartApp)(QSEECom_handle**, const char*, const char*, uint32_t) = nullptr;
    int (*mShutdownApp)(QSEECom_handle**) = nullptr;
    int (*mSendCmd)(QSEECom_handle*, void*, uint32_t, void*, uint32_t) = nullptr;
};

}  // namespace aidl::android::hardware::biometrics::face
