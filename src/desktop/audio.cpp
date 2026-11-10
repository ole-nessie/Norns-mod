#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

#include "audio.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "synth.h"

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

void dataCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
    auto* synth = static_cast<Synth*>(device->pUserData);
    synth->process(static_cast<float*>(output), static_cast<int>(frames),
                   static_cast<int>(device->playback.channels));
}

}  // namespace

struct AudioOutput::Impl {
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
    bool deviceReady = false;
};

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() { stop(); }

void AudioOutput::listDevices() {
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::fprintf(stderr, "audio: could not initialise audio backend\n");
        return;
    }
    ma_device_info* playback = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, &playback, &count, nullptr, nullptr) == MA_SUCCESS) {
        std::printf("Playback devices (%s):\n", ma_get_backend_name(context.backend));
        for (ma_uint32 i = 0; i < count; ++i) {
            std::printf("  %s%s\n", playback[i].name, playback[i].isDefault ? "  [default]" : "");
        }
    }
    ma_context_uninit(&context);
}

bool AudioOutput::start(Synth& synth, const std::string& deviceFilter) {
    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) != MA_SUCCESS) {
        std::fprintf(stderr, "audio: could not initialise audio backend\n");
        return false;
    }
    impl_->contextReady = true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kSampleRate;
    config.periodSizeInFrames = 128;
    config.periods = 3;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = dataCallback;
    config.pUserData = &synth;

    if (!deviceFilter.empty()) {
        ma_device_info* playback = nullptr;
        ma_uint32 count = 0;
        ma_context_get_devices(&impl_->context, &playback, &count, nullptr, nullptr);
        const std::string wanted = lower(deviceFilter);
        for (ma_uint32 i = 0; i < count; ++i) {
            if (lower(playback[i].name).find(wanted) != std::string::npos) {
                config.playback.pDeviceID = &playback[i].id;
                std::fprintf(stderr, "audio: using \"%s\"\n", playback[i].name);
                break;
            }
        }
        if (!config.playback.pDeviceID) {
            std::fprintf(stderr, "audio: no playback device matches \"%s\" (try --list-devices)\n",
                         deviceFilter.c_str());
            return false;
        }
    }

    if (ma_device_init(&impl_->context, &config, &impl_->device) != MA_SUCCESS) {
        std::fprintf(stderr, "audio: could not open playback device\n");
        return false;
    }
    impl_->deviceReady = true;

    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        std::fprintf(stderr, "audio: could not start playback device\n");
        return false;
    }
    std::fprintf(stderr, "audio: %s, %u Hz, %u ch\n", ma_get_backend_name(impl_->context.backend),
                 impl_->device.sampleRate, impl_->device.playback.channels);
    return true;
}

void AudioOutput::stop() {
    if (impl_->deviceReady) {
        ma_device_uninit(&impl_->device);
        impl_->deviceReady = false;
    }
    if (impl_->contextReady) {
        ma_context_uninit(&impl_->context);
        impl_->contextReady = false;
    }
}
