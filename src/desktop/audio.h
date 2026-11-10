#pragma once

#include <memory>
#include <string>

class Synth;

constexpr int kSampleRate = 48000;

// Playback device driving Synth::process from miniaudio's callback.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    static void listDevices();

    // `deviceFilter` picks the first playback device whose name contains it
    // (case-insensitive); empty means the system default.
    bool start(Synth& synth, const std::string& deviceFilter);
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
