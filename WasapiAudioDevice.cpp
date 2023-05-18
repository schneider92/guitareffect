#include <iostream>
#include <exception>

#include "WasapiAudioDevice.h"

WasapiAudioDeviceEnumerator::WasapiAudioDeviceEnumerator() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_SPEED_OVER_MEMORY);
    if (hr != S_OK) {
        std::cerr << "CoInitialize failed";
        throw std::runtime_error{ "" };
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), enumerator.pv());
    if (hr != S_OK) {
        std::cerr << "get device enumerator failed";
        throw std::runtime_error{ "" };
    }
}

std::vector<AudioDeviceDescriptor> WasapiAudioDeviceEnumerator::listCapturingDevices() const {
    return listDevices(eCapture);
}

std::vector<AudioDeviceDescriptor> WasapiAudioDeviceEnumerator::listRenderingDevices() const {
    return listDevices(eRender);
}

std::unique_ptr<AudioCapturer> WasapiAudioDeviceEnumerator::createCapturer(const DeviceInitParams& params) const {
    MMAutoPtr<IAudioClient2> client = initializeClient(params);
    if (!client.get())
        return {};

    MMAutoPtr<IAudioCaptureClient> capture;
    HRESULT hr = client->GetService(__uuidof(IAudioCaptureClient), capture.pv());
    if (hr != S_OK) {
        std::cerr << "get captureclient failed";
        return nullptr;
    }

    auto ret = std::make_unique<WasapiAudioCapturer>();
    ret->client = std::move(client);
    ret->capture = std::move(capture);
    return ret;
}

std::unique_ptr<AudioRenderer> WasapiAudioDeviceEnumerator::createRenderer(const DeviceInitParams& params) const {
    MMAutoPtr<IAudioClient2> client = initializeClient(params);
    if (!client.get())
        return {};

    MMAutoPtr<IAudioRenderClient> render;
    HRESULT hr = client->GetService(__uuidof(IAudioRenderClient), render.pv());
    if (hr != S_OK) {
        std::cerr << "get renderclient failed";
        return nullptr;
    }

    auto ret = std::make_unique<WasapiAudioRenderer>();
    ret->client = std::move(client);
    ret->render = std::move(render);
    return ret;
}

static void saveDeviceId(AudioDeviceDescriptor::ID& dst, LPCWSTR wstrid) {
    static constexpr const auto wcharsize = sizeof(WCHAR);
    const auto idsize = std::wstring_view{ wstrid }.size() * wcharsize;
    dst.resize(idsize + wcharsize);
    auto buf = dst.data();
    memcpy(buf, wstrid, idsize);
    memset(buf + idsize, 0, wcharsize); // add nullterm
}

std::vector<AudioDeviceDescriptor> WasapiAudioDeviceEnumerator::listDevices(EDataFlow dataFlow) const {
    MMAutoPtr<IMMDeviceCollection> deviceCollection;
    HRESULT hr = enumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, deviceCollection.pp());
    if (hr != S_OK) {
        std::cerr << "EnumAudioEndpoints failed";
        throw std::runtime_error{ "" };
    }

    UINT devcount;
    hr = deviceCollection->GetCount(&devcount);
    if (hr != S_OK) {
        std::cerr << "device collection GetCount failed";
        throw std::runtime_error{ "" };
    }

    std::vector<AudioDeviceDescriptor> ret;
    ret.reserve(devcount);

    for (UINT i = 0; i < devcount; ++i) {
        MMAutoPtr<IMMDevice> dev;
        hr = deviceCollection->Item(i, dev.pp());
        if (hr != S_OK) {
            std::cerr << "device get Item failed";
            throw std::runtime_error{ "" };
        }

        AudioDeviceDescriptor& devDesc = ret.emplace_back();

        LPWSTR wstrid;
        hr = dev->GetId(&wstrid);
        if (hr != S_OK) {
            std::cerr << "device GetId failed";
            throw std::runtime_error{ "" };
        }
        saveDeviceId(devDesc.id, wstrid);
        CoTaskMemFree(wstrid);

        MMAutoPtr<IPropertyStore> props;
        hr = dev->OpenPropertyStore(STGM_READ, props.pp());
        if (hr != S_OK) {
            std::cerr << "OpenPropertyStore failed";
            throw std::runtime_error{ "" };
        }
        PROPVARIANT val;
        hr = props->GetValue(PKEY_DeviceInterface_FriendlyName, &val);
        if (hr != S_OK) {
            std::cerr << "GetValue(friendly name) failed";
            throw std::runtime_error{ "" };
        }

        if (val.vt == VT_LPWSTR) {
            devDesc.friendlyName = toCString(std::wstring_view{ val.pwszVal });
        }
        else {
            std::cerr << "no friendly name";
            throw std::runtime_error{ "" };
        }
    }
    return ret;
}

MMAutoPtr<IAudioClient2> WasapiAudioDeviceEnumerator::initializeClient(const DeviceInitParams& params) const {
    const auto wstrId = reinterpret_cast<LPCWSTR>(params.deviceId.data());
    MMAutoPtr<IMMDevice> dev;
    HRESULT hr = enumerator->GetDevice(wstrId, dev.pp());
    if (hr != S_OK) {
        std::cerr << "get device failed";
        return {};
    }

    MMAutoPtr<IAudioClient2> client;
    hr = dev->Activate(__uuidof(IAudioClient2), CLSCTX_ALL, nullptr, client.pv());
    if (hr != S_OK) {
        std::cerr << "activate device failed";
        return {};
    }

    AudioClientProperties properties = { 0 };
    properties.cbSize = sizeof(AudioClientProperties);
    properties.eCategory = AudioCategory_Media;
    properties.Options |= AUDCLNT_STREAMOPTIONS_RAW;
    hr = client->SetClientProperties(&properties);
    if (hr != S_OK) {
        std::cerr << "SetClientProperties failed";
        throw std::runtime_error{ "" };
    }

    WAVEFORMATEX mixFormat = {};
    mixFormat.wFormatTag = WAVE_FORMAT_PCM;
    mixFormat.nChannels = params.channels;
    mixFormat.nSamplesPerSec = params.samplingRate_hz;
    mixFormat.wBitsPerSample = params.bitsPerSample;
    mixFormat.nBlockAlign = (params.channels * params.bitsPerSample) / 8;
    mixFormat.nAvgBytesPerSec = params.samplingRate_hz * mixFormat.nBlockAlign;

    const REFERENCE_TIME bufferDuration = 1000; // in 100ns units
    const REFERENCE_TIME devicePeriod = 30000;
    const DWORD flags = 0;// (AUDCLNT_STREAMFLAGS_RATEADJUST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
    hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, flags, bufferDuration, devicePeriod, &mixFormat, nullptr);
    if (hr != S_OK) {
        std::cerr << "initialize device failed";
        return {};
    }

    UINT32 bufferSize;
    hr = client->GetBufferSize(&bufferSize);
    if (hr != S_OK) {
        std::cerr << "GetBufferSize failed";
        return {};
    }
    std::cout << "buffer size is " << bufferSize << "\n";

    REFERENCE_TIME defaultPeriod, minimumPeriod;
    hr = client->GetDevicePeriod(&defaultPeriod, &minimumPeriod);
    if (hr != S_OK) {
        std::cerr << "GetDevicePeriod failed";
        return {};
    }
    std::cout << "default period is " << defaultPeriod << ", minimum period is " << minimumPeriod << "\n";

    REFERENCE_TIME latency;
    hr = client->GetStreamLatency(&latency);
    if (hr != S_OK) {
        std::cerr << "GetStreamLatency failed";
        return {};
    }
    std::cout << "stream latency is " << latency << "\n";

    return client;
}





void WasapiAudioCapturer::start() {
    HRESULT hr = client->Start();
    if (hr != S_OK) {
        std::cerr << "failed to start capture";
        throw std::runtime_error{ "" };
    }
}

void WasapiAudioCapturer::stop() {
    HRESULT hr = client->Stop();
    if (hr != S_OK) {
        std::cerr << "failed to stop capture";
        throw std::runtime_error{ "" };
    }
}

AudioCapturer::Buffer WasapiAudioCapturer::getBuffer() {
    BYTE* buf{};
    UINT32 frameCount = 0;
    UINT32 packetSize;
    DWORD dwFlags;
    HRESULT hr = capture->GetNextPacketSize(&packetSize);
    if (hr != S_OK) {
        std::cerr << "GetNextPacketSize failed";
        throw std::runtime_error{ "" };
    }
    if (packetSize > 0) {
        hr = capture->GetBuffer(&buf, &frameCount, &dwFlags, nullptr, nullptr);
        if (hr == AUDCLNT_S_BUFFER_EMPTY) {
            std::cerr << "unexpected AUDCLNT_S_BUFFER_EMPTY\n";
            throw std::runtime_error{ "" };
        }
        else if (hr != S_OK) {
            std::cerr << "getBuffer failed";
            throw std::runtime_error{ "" };
        }
        else {
            //        std::cout << "packet size=" << packetSize << ", frameCount=" << frameCount << "\n";
        }
    }

    Buffer ret;
    ret.frames = buf;
    ret.availableFrameCount = frameCount;
    return ret;
}

void WasapiAudioCapturer::releaseBuffer(int numberOfFramesRead) {
    HRESULT hr = capture->ReleaseBuffer(numberOfFramesRead);
    if (hr != S_OK) {
        std::cerr << "releaseBuffer failed";
        throw std::runtime_error{ "" };
    }
}





void WasapiAudioRenderer::start() {
    HRESULT hr = client->Start();
    if (hr != S_OK) {
        std::cerr << "failed to start render";
        throw std::runtime_error{ "" };
    }
}

void WasapiAudioRenderer::stop() {
    HRESULT hr = client->Stop();
    if (hr != S_OK) {
        std::cerr << "failed to stop render";
        throw std::runtime_error{ "" };
    }
}

void* WasapiAudioRenderer::getBuffer(int numberOfFramesToWrite) {
    BYTE* buf;
    HRESULT hr = render->GetBuffer(numberOfFramesToWrite, &buf);
    if (hr != S_OK) {
        std::cerr << "getBuffer failed";
        return nullptr;
    }
    return buf;
}

void WasapiAudioRenderer::releaseBuffer(int numberOfFramesWritten) {
    HRESULT hr = render->ReleaseBuffer(numberOfFramesWritten, 0);
    if (hr != S_OK) {
        std::cerr << "releaseBuffer failed";
        throw std::runtime_error{ "" };
    }
}
