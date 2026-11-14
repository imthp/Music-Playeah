#pragma once

#include <memory>
#include <string>

namespace mp::core::audio {

class Renderer;

// The sound card, through miniaudio (WASAPI shared mode on Windows, CoreAudio on macOS,
// PulseAudio/ALSA on Linux). Always float32 stereo at the device's own rate, so Windows never
// resamples behind our back (D5). Its callback just calls Renderer::render.
class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // Opens the default playback device. Doesn't start it.
    // nullDevice: miniaudio's silent "null" backend, which runs the callback in real time
    // without a sound card. Used by tests.
    bool open(std::string& error, bool nullDevice = false);
    // Starts pulling audio from `renderer`, which must outlive the running device.
    bool start(Renderer& renderer, std::string& error);
    void close();

    int sampleRate() const noexcept;
    int channels() const noexcept;
    std::string deviceName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mp::core::audio
