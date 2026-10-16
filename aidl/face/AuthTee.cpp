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
#include <vector>

namespace aidl::android::hardware::biometrics::face {

namespace {

// faceapp lives in /vendor/etc/faceunlock on sm8150/sm8250/sm8350, or the firmware partition on
// sdm845.
constexpr const char* kAppPaths[] = {"/vendor/etc/faceunlock", "/vendor/firmware_mnt/image"};
constexpr const char* kAppName = "faceapp";
constexpr uint32_t kSharedBufferSize = 0xc8000;

constexpr int kMaxHeapName = 32;
struct IonHeapData {
    char name[kMaxHeapName];
    uint32_t type;
    uint32_t heap_id;
    uint32_t reserved0, reserved1, reserved2;
};

// The qseecom heap id varies by platform, so resolve it by name instead of hardcoding the bit;
// struct ion_heap_data isn't in the legacy ion/ion.h, so it is declared locally above.
unsigned int qseecomHeapMask() {
    static const unsigned int mask = [] {
        constexpr unsigned int kLegacyMask = 1u << 27;
        int fd = ion_open();
        if (fd < 0) return kLegacyMask;

        unsigned int found = 0;
        int count = 0;
        if (ion_query_heap_cnt(fd, &count) == 0 && count > 0) {
            std::vector<IonHeapData> heaps(count);
            if (ion_query_get_heaps(fd, count, heaps.data()) == 0) {
                for (const char* name : {"qsecom_ta", "qsecom"}) {
                    for (const IonHeapData& heap : heaps) {
                        if (strncmp(heap.name, name, kMaxHeapName) == 0) {
                            found = 1u << heap.heap_id;
                            break;
                        }
                    }
                    if (found) break;
                }
            }
        }
        ion_close(fd);
        return found ? found : kLegacyMask;
    }();
    return mask;
}

struct QSEECom_handle {
    uint8_t* ion_sbuffer;
};

struct QSEECom_ion_fd_info {
    struct {
        int32_t fd;
        uint32_t cmd_buf_offset;
    } data[4];
};

// The driver patches in the addresses of the ION buffers.
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
        if ((mIon = ion_open()) < 0 ||
            ion_alloc_fd(mIon, mSize, 0, qseecomHeapMask(), 0, &mFd) != 0) {
            LOG(ERROR) << "Failed to allocate " << mSize << " bytes of ION";
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
        return ioctl(mFd, DMA_BUF_IOCTL_SYNC, &sync) == 0;
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
    sShutdownApp = reinterpret_cast<decltype(sShutdownApp)>(dlsym(lib, "QSEECom_shutdown_app"));
    sSendModifiedCmd = reinterpret_cast<decltype(sSendModifiedCmd)>(
            dlsym(lib, "QSEECom_send_modified_cmd_64"));
    sStartApp = reinterpret_cast<decltype(sStartApp)>(dlsym(lib, "QSEECom_start_app"));
    if (!sStartApp || !sShutdownApp || !sSendModifiedCmd) {
        LOG(ERROR) << "Missing QSEECom symbols";
        sStartApp = nullptr;
        return false;
    }
    return true;
}

}  // namespace

bool AuthTee::open() {
    if (sHandle) return true;
    if (!load()) return false;

    // Loading the trusted app occasionally fails with a transient scm_call error, and the TA lives
    // in a different directory depending on the device, so retry across both candidate paths.
    for (int attempt = 1; attempt <= 5; attempt++) {
        for (const char* path : kAppPaths) {
            QSEECom_handle* handle = nullptr;
            int ret = sStartApp(&handle, path, kAppName, kSharedBufferSize);
            if (ret == 0 && handle && handle->ion_sbuffer) {
                sHandle = handle;
                return true;
            }
            LOG(WARNING) << "Failed to start " << kAppName << " from " << path << " (attempt "
                         << attempt << "): " << ret;
            if (handle) sShutdownApp(&handle);
        }
        usleep(50000);
    }
    return false;
}

void AuthTee::close() {
    if (sHandle) sShutdownApp(&sHandle);
    sHandle = nullptr;
}

int AuthTee::communicate(void* send, int sendLen, void* rsp, int rspLen) {
    if (!sHandle || sendLen <= 0 || rspLen < 0) return -1;

    IonBuffer request, response;
    if (!request.alloc(sendLen) || !response.alloc(rspLen ? rspLen : sizeof(uint32_t))) return -1;
    memcpy(request.data(), send, sendLen);

    auto* cmd = reinterpret_cast<Command*>(sHandle->ion_sbuffer);
    *cmd = {.requestLen = request.len(), .responseLen = response.len()};

    QSEECom_ion_fd_info ifd = {};
    ifd.data[0] = {request.fd(), offsetof(Command, request)};
    ifd.data[1] = {response.fd(), offsetof(Command, response)};

    int32_t status = 0;
    if (int ret = sSendModifiedCmd(sHandle, cmd, sizeof(*cmd), &status, sizeof(status), &ifd);
        ret != 0) {
        LOG(ERROR) << "Failed to send TEE command: " << ret;
        return -1;
    }
    if (rspLen) memcpy(rsp, response.data(), rspLen);
    return status;
}

}  // namespace aidl::android::hardware::biometrics::face
