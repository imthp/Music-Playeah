#include "core/audio/output.hpp"

#include <atomic>

#include <miniaudio.h>  // implementation is compiled once, in miniaudio_impl.cpp

#include "core/audio/renderer.hpp"

namespace mp::core::audio {

struct AudioOutput::Impl {
    ma_context context{};
    ma_device device{};
    bool contextOpened = false;
    bool opened = false;
    std::atomic<Renderer*> renderer{nullptr};

    static void callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* out = static_cast<float*>(output);
        if (Renderer* r = self->renderer.load(std::memory_order_acquire)) {
            r->render(out, frames);
        } else {
            for (ma_uint32 i = 0; i < frames * device->playback.channels; ++i) {
                out[i] = 0.0f;
            }
        }
    }
};

AudioOutput::AudioOutput() : d_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() { close(); }

bool AudioOutput::open(std::string& error, bool nullDevice) {
    close();
    if (nullDevice) {
        const ma_backend backends[] = {ma_backend_null};
        if (ma_context_init(backends, 1, nullptr, &d_->context) != MA_SUCCESS) {
            error = "Can't create null audio context";
            return false;
        }
        d_->contextOpened = true;
    }
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 0;  // 0 = the device's own rate: no resampling in the OS mixer
    config.dataCallback = &Impl::callback;
    config.pUserData = d_.get();
    config.performanceProfile = ma_performance_profile_low_latency;

    const ma_result r = ma_device_init(d_->contextOpened ? &d_->context : nullptr, &config, &d_->device);
    if (r != MA_SUCCESS) {
        error = std::string("Can't open audio device: ") + ma_result_description(r);
        return false;
    }
    d_->opened = true;
    return true;
}

bool AudioOutput::start(Renderer& renderer, std::string& error) {
    if (!d_->opened) {
        error = "Audio device not open";
        return false;
    }
    d_->renderer.store(&renderer, std::memory_order_release);
    const ma_result r = ma_device_start(&d_->device);
    if (r != MA_SUCCESS) {
        error = std::string("Can't start audio device: ") + ma_result_description(r);
        return false;
    }
    return true;
}

void AudioOutput::close() {
    if (d_->opened) {
        ma_device_uninit(&d_->device);  // stops the callback and waits for it to return
        d_->opened = false;
    }
    if (d_->contextOpened) {
        ma_context_uninit(&d_->context);
        d_->contextOpened = false;
    }
    d_->renderer.store(nullptr, std::memory_order_release);
}

int AudioOutput::sampleRate() const noexcept {
    return d_->opened ? static_cast<int>(d_->device.sampleRate) : 0;
}

int AudioOutput::channels() const noexcept {
    return d_->opened ? static_cast<int>(d_->device.playback.channels) : 0;
}

std::string AudioOutput::deviceName() const {
    if (!d_->opened) {
        return {};
    }
    char name[MA_MAX_DEVICE_NAME_LENGTH + 1] = {};
    ma_device_get_name(const_cast<ma_device*>(&d_->device), ma_device_type_playback, name, sizeof(name), nullptr);
    return name;
}

}  // namespace mp::core::audio
