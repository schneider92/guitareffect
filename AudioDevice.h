#pragma once

#include <string>
#include <vector>
#include <memory>

struct AudioDeviceDescriptor {
	using ID = std::vector<char>;
	ID id;
	std::string friendlyName;
};

struct AudioCapturer {
	virtual ~AudioCapturer() = default;

	virtual void start() = 0;
	virtual void stop() = 0;

	struct Buffer {
		void* frames;
		int availableFrameCount;
	};

	virtual Buffer getBuffer() = 0;
	virtual void releaseBuffer(int numberOfFramesRead) = 0;
};

struct AudioRenderer {
	virtual ~AudioRenderer() = default;

	virtual void start() = 0;
	virtual void stop() = 0;

	virtual void* getBuffer(int numberOfFramesToWrite) = 0;
	virtual void releaseBuffer(int numberOfFramesWritten) = 0;
};

struct DeviceInitParams {
	AudioDeviceDescriptor::ID deviceId;
	int samplingRate_hz;
	int bitsPerSample;
	int channels;
	long long bufferSize_us;
};

struct AudioDeviceEnumerator {
	virtual ~AudioDeviceEnumerator() = default;

	static const std::vector<std::string>& listAvailableProviders();
	static std::unique_ptr<AudioDeviceEnumerator> getInstance(const std::string& provider);

	virtual std::vector<AudioDeviceDescriptor> listCapturingDevices() const = 0;
	virtual std::vector<AudioDeviceDescriptor> listRenderingDevices() const = 0;
	virtual std::unique_ptr<AudioCapturer> createCapturer(const DeviceInitParams& params) const = 0;
	virtual std::unique_ptr<AudioRenderer> createRenderer(const DeviceInitParams& params) const = 0;
};
