#pragma once

#include <algorithm>
#include <iterator>
#include <string>

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <Functiondiscoverykeys_devpkey.h>

#include "AudioDevice.h"

template<typename T>
struct MMAutoPtr {
    static_assert(std::is_base_of_v<IUnknown, T>);
    MMAutoPtr() = default;
    MMAutoPtr(const MMAutoPtr<T>& rhs) {
        operator=(rhs);
    }
    MMAutoPtr(MMAutoPtr<T>&& rhs) noexcept {
        operator=(std::move(rhs));
    }
    ~MMAutoPtr() {
        if (p)
            p->Release();
    }
    MMAutoPtr& operator=(const MMAutoPtr<T>& rhs) {
        if (this != &rhs) {
            if (p)
                p->Release();
            p = rhs.p;
            if (p)
                p->AddRef();
        }
        return *this;
    }
    MMAutoPtr& operator=(MMAutoPtr<T>&& rhs) noexcept {
        if (this != &rhs) {
            if (p)
                p->Release();
            p = rhs.p;
            rhs.p = nullptr;
        }
        return *this;
    }

    void reset(T* newPtr = nullptr, bool releaseOld = true) {
        if (releaseOld && p)
            p->Release();
        p = newPtr;
    }
    T* get() const {
        return p;
    }
    T& operator*() const {
        return *p;
    }
    T* operator->() const {
        return p;
    }
    T** pp() {
        return &p;
    }
    LPVOID* pv() {
        return (LPVOID*)&p;
    }

private:
    T* p{};
};

static std::string toCString(std::wstring_view wsv) {
    std::string ret;
    ret.reserve(wsv.size());

    std::transform(wsv.begin(), wsv.end(),
        std::back_insert_iterator<std::string>(ret),
        [](wchar_t wchar)
        {
            return static_cast<char>(wchar > 127 ? L'?' : wchar);
        });
    return ret;
}

struct WasapiAudioDeviceEnumerator : public AudioDeviceEnumerator {
	WasapiAudioDeviceEnumerator();
	std::vector<AudioDeviceDescriptor> listCapturingDevices() const override;
	std::vector<AudioDeviceDescriptor> listRenderingDevices() const override;
    std::unique_ptr<AudioCapturer> createCapturer(const DeviceInitParams& params) const override;
    std::unique_ptr<AudioRenderer> createRenderer(const DeviceInitParams& params) const override;

private:
	MMAutoPtr<IMMDeviceEnumerator> enumerator;

    std::vector<AudioDeviceDescriptor> listDevices(EDataFlow dataFlow) const;
    MMAutoPtr<IAudioClient2> initializeClient(const DeviceInitParams& params) const;
};

struct WasapiAudioCapturer : public AudioCapturer {
    void start() override;
    void stop() override;
    Buffer getBuffer() override;
    void releaseBuffer(int numberOfFramesRead) override;

    MMAutoPtr<IAudioClient2> client;
    MMAutoPtr<IAudioCaptureClient> capture;
};

struct WasapiAudioRenderer : public AudioRenderer {
    void start() override;
    void stop() override;
    void* getBuffer(int numberOfFramesToWrite) override;
    void releaseBuffer(int numberOfFramesWritten) override;

    MMAutoPtr<IAudioClient2> client;
    MMAutoPtr<IAudioRenderClient> render;
};
