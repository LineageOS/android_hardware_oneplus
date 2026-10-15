/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.biometrics.face-service.oneplus"

#include "AuthTee.h"

#include <android-base/logging.h>
#include <dlfcn.h>
#include <ion/ion.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace aidl::android::hardware::biometrics::face {

namespace {

constexpr const char* kAppPath = "/vendor/firmware_mnt/image";
constexpr const char* kAppName = "faceapp";
constexpr uint32_t kSharedBufferSize = 0xc8000;
constexpr unsigned int kQseecomHeapMask = 1u << 27;

struct QSEECom_handle {
    uint8_t* ion_sbuffer;
};

struct QSEECom_ion_fd_info {
    struct {
        int32_t fd;
        uint32_t cmd_buf_offset;
    } data[4];
};

// The driver replaces the addresses with those of the ION buffers.
struct Command {
    uint64_t request;
    uint32_t requestLen;
    uint32_t reserved0;
    uint64_t response;
    uint32_t responseLen;
    uint32_t reserved1;
};
static_assert(sizeof(Command) == 32);

int (*sStartApp)(QSEECom_handle**, const char*, const char*, uint32_t);
int (*sShutdownApp)(QSEECom_handle**);
int (*sSendModifiedCmd)(QSEECom_handle*, void*, uint32_t, void*, uint32_t, QSEECom_ion_fd_info*);
QSEECom_handle* sHandle;

class IonBuffer {
  public:
    ~IonBuffer() {
        if (mData) {
            dma_buf_sync sync = {.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW};
            ioctl(mFd, DMA_BUF_IOCTL_SYNC, &sync);
            munmap(mData, mSize);
        }
        if (mFd >= 0) ::close(mFd);
        if (mIon >= 0) ion_close(mIon);
    }

    bool alloc(size_t len) {
        mLen = len;
        mSize = (len + 0xfff) & ~size_t(0xfff);
        mIon = ion_open();
        if (mIon < 0) {
            LOG(ERROR) << "Failed to open ION";
            return false;
        }
        if (int ret = ion_alloc_fd(mIon, mSize, 0, kQseecomHeapMask, 0, &mFd); ret != 0) {
            LOG(ERROR) << "Failed to allocate " << mSize << " bytes of ION: " << ret;
            mFd = -1;
            return false;
        }
        void* data = mmap(nullptr, mSize, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);
        if (data == MAP_FAILED) {
            PLOG(ERROR) << "Failed to map ION";
            return false;
        }
        mData = static_cast<uint8_t*>(data);
        dma_buf_sync sync = {.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW};
        if (ioctl(mFd, DMA_BUF_IOCTL_SYNC, &sync) != 0) {
            PLOG(ERROR) << "Failed to sync ION";
            return false;
        }
        return true;
    }

    int fd() const { return mFd; }
    uint8_t* data() const { return mData; }
    uint32_t len() const { return mLen; }

  private:
    int mIon = -1;
    int mFd = -1;
    uint8_t* mData = nullptr;
    size_t mSize = 0;
    uint32_t mLen = 0;
};

bool load() {
    if (sStartApp) return true;

    void* lib = dlopen("libQSEEComAPI.so", RTLD_NOW);
    if (!lib) {
        LOG(ERROR) << "Failed to load libQSEEComAPI: " << dlerror();
        return false;
    }
    auto startApp = reinterpret_cast<decltype(sStartApp)>(dlsym(lib, "QSEECom_start_app"));
    sShutdownApp = reinterpret_cast<decltype(sShutdownApp)>(dlsym(lib, "QSEECom_shutdown_app"));
    sSendModifiedCmd = reinterpret_cast<decltype(sSendModifiedCmd)>(
            dlsym(lib, "QSEECom_send_modified_cmd_64"));
    if (!startApp || !sShutdownApp || !sSendModifiedCmd) {
        LOG(ERROR) << "Missing QSEECom symbols";
        dlclose(lib);
        return false;
    }
    sStartApp = startApp;
    return true;
}

}  // namespace

bool AuthTee::open() {
    if (sHandle) return true;
    if (!load()) return false;

    // Loading the trusted app occasionally fails with a transient scm_call error when the secure
    // app region is briefly contended, so retry a few times before giving up.
    constexpr int kMaxAttempts = 5;
    for (int attempt = 1; attempt <= kMaxAttempts; attempt++) {
        QSEECom_handle* handle = nullptr;
        int ret = sStartApp(&handle, kAppPath, kAppName, kSharedBufferSize);
        if (ret == 0 && handle && handle->ion_sbuffer) {
            sHandle = handle;
            return true;
        }

        LOG(WARNING) << "Failed to start " << kAppName << " (attempt " << attempt << "): " << ret;
        if (handle) sShutdownApp(&handle);
        if (attempt < kMaxAttempts) usleep(50000);
    }
    LOG(ERROR) << "Giving up starting " << kAppName;
    return false;
}

void AuthTee::close() {
    if (!sHandle) return;
    if (int ret = sShutdownApp(&sHandle); ret != 0) {
        LOG(ERROR) << "Failed to shut down " << kAppName << ": " << ret;
    }
    sHandle = nullptr;
}

int AuthTee::communicate(void* send, int sendLen, void* rsp, int rspLen) {
    if (!sHandle || !send || sendLen <= 0 || rspLen < 0 || (rspLen > 0 && !rsp)) {
        LOG(ERROR) << "Invalid TEE request of " << sendLen << " bytes";
        return -1;
    }

    IonBuffer request, response;
    // The stock service always provides room for a status word.
    if (!request.alloc(sendLen) || !response.alloc(rspLen ? rspLen : sizeof(uint32_t))) return -1;
    memcpy(request.data(), send, sendLen);

    auto* cmd = reinterpret_cast<Command*>(sHandle->ion_sbuffer);
    memset(cmd, 0, sizeof(*cmd));
    cmd->requestLen = request.len();
    cmd->responseLen = response.len();

    QSEECom_ion_fd_info ifd = {};
    ifd.data[0] = {request.fd(), offsetof(Command, request)};
    ifd.data[1] = {response.fd(), offsetof(Command, response)};

    int32_t status = 0;
    int ret = sSendModifiedCmd(sHandle, cmd, sizeof(*cmd), &status, sizeof(status), &ifd);
    if (ret != 0) {
        LOG(ERROR) << "Failed to send TEE command: " << ret;
        return -1;
    }
    if (rspLen) memcpy(rsp, response.data(), rspLen);
    return status;
}

}  // namespace aidl::android::hardware::biometrics::face
