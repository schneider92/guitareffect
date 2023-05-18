#include <iostream>
#include <thread>

#include "AudioDevice.h"

void printDeviceList(const char* name, const std::vector<AudioDeviceDescriptor>& list) {
    std::cout << name << " devices: " << list.size() << "\n";
    const auto len = list.size();
    for (size_t i = 0; i < len; ++i) {
        std::cout << " " << i << " - " << list[i].friendlyName << "\n";
    }
};

int readidx(int max) {
    std::string s;
    std::cin >> s;
    try {
        int ret = std::stoi(s);
        if ((ret < 0) || (ret >= max))
            ret = -1;
        return ret;
    }
    catch (std::invalid_argument&) {
        return -1;
    }
}

static std::atomic_bool stopRequest;
static std::atomic_int volume = 1000;
static float angle = 0;

static int run(AudioCapturer* capture, AudioRenderer* render) {
    // start both
    capture->start();
    render->start();

    // read from capture and write to render - forever
    int i = 0, sum = 0, skip = 0;
    while (!stopRequest) {
        // read capture buffer
        auto captureBuf = capture->getBuffer();
        const auto count = captureBuf.availableFrameCount;
        if (count == 0) {
            capture->releaseBuffer(0);
            ++skip;
            //std::this_thread::yield();
            //std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
            continue;
        }

        // get render buffer
        auto renderBuf = render->getBuffer(count);
        if (renderBuf == nullptr) {
            std::cerr << "null buffer returned";
            return -1;
        }

        // render samples
        if (false) {
            auto outBuf = reinterpret_cast<int16_t*>(renderBuf);
            static constexpr float pi2 = 3.14159265358979 * 2;
            static constexpr float angleInc = pi2 / 100;
            const auto vol = volume.load();
            for (unsigned i = 0; i < count; ++i) {
                angle += angleInc;
                const int16_t v = vol * sin(angle);
                outBuf[2 * i] = outBuf[2 * i + 1] = v;
            }
            if (angle > pi2)
                angle -= pi2 * 100;
        }

        // copy data - handle effects here, TODO
        else {
            memcpy(renderBuf, captureBuf.frames, count * 4);
        }

        // release both buffers
        render->releaseBuffer(count);
        capture->releaseBuffer(count);

        ++i;
        sum += count;
        if (i == 20) {
//            std::cout << "STATS:\nsum=" << sum << "\nskip=" << skip << "\n\n";
            i = sum = skip = 0;
        }
    }

    render->stop();
    capture->stop();

    return 0;
}

int main()
{
    // params
    DeviceInitParams params;
    params.channels = 2;
    params.samplingRate_hz = 48000;
    params.bitsPerSample = 16;
    params.bufferSize_us = 1000;

    // enumerate audio devices
    const auto enumerator = AudioDeviceEnumerator::getInstance("WASAPI");

    // choose capture device
    const auto captureDevices = enumerator->listCapturingDevices();
    printDeviceList("capture", captureDevices);
    std::cout << "choose one\n> ";
    auto idx = readidx(captureDevices.size());
    if (idx < 0) {
        std::cerr << "invalid capture device";
        return -1;
    }
    params.deviceId = captureDevices[idx].id;
    auto capture = enumerator->createCapturer(params);
    if (!capture) {
        std::cerr << "Create capture device failed";
        return -1;
    }

    // choose render device
    const auto renderDevices = enumerator->listRenderingDevices();
    printDeviceList("render", renderDevices);
    std::cout << "choose one\n> ";
    idx = readidx(renderDevices.size());
    if (idx < 0) {
        std::cerr << "invalid render device";
        return -1;
    }
    params.deviceId = renderDevices[idx].id;
    auto render = enumerator->createRenderer(params);
    if (!render) {
        std::cerr << "Create render device failed";
        return -1;
    }

    std::thread thr{ run, capture.get(), render.get() };

    std::cout << "STARTED\n";
    std::string s;
    while (true) {
        std::cout << "New volume=";
        std::cin >> s;
        if (s._Starts_with("q"))
            break;
        volume = std::stoi(s);
    }

    stopRequest = true;
    thr.join();

    std::cout << "STOPPED\n";
}
