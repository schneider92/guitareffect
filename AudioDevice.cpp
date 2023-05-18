#include "AudioDevice.h"
#include "WasapiAudioDevice.h"

const std::vector<std::string>& AudioDeviceEnumerator::listAvailableProviders() {
	static const std::vector<std::string> providers{
		"WASAPI",
	};
	return providers;
}

std::unique_ptr<AudioDeviceEnumerator> AudioDeviceEnumerator::getInstance(const std::string& provider) {
	if (provider == "WASAPI") {
		return std::make_unique<WasapiAudioDeviceEnumerator>();
	}
	else {
		return nullptr;
	}
}
