#include "core/audio/decoder.hpp"

#include "core/text.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

namespace mp::core::audio {

namespace {

std::string ffmpegError(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

bool sameLayout(const AVChannelLayout& a, const AVChannelLayout& b) {
    return av_channel_layout_compare(&a, &b) == 0;
}

// Display name and lossless flag for the format badge (SPEC §2 "FLAC 24/96").
void describeCodec(const AVFormatContext* fmt, const AVCodecParameters* par, TrackInfo& info) {
    const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);
    info.lossless = desc != nullptr && (desc->props & AV_CODEC_PROP_LOSSLESS) != 0;

    switch (par->codec_id) {
        case AV_CODEC_ID_FLAC: info.codec = "FLAC"; break;
        case AV_CODEC_ID_ALAC: info.codec = "ALAC"; break;
        case AV_CODEC_ID_MP3: info.codec = "MP3"; break;
        case AV_CODEC_ID_AAC: info.codec = "AAC"; break;
        case AV_CODEC_ID_OPUS: info.codec = "Opus"; break;
        case AV_CODEC_ID_VORBIS: info.codec = "Vorbis"; break;
        case AV_CODEC_ID_WAVPACK: info.codec = "WavPack"; break;
        case AV_CODEC_ID_APE: info.codec = "APE"; break;
        case AV_CODEC_ID_DSD_LSBF:
        case AV_CODEC_ID_DSD_MSBF:
        case AV_CODEC_ID_DSD_LSBF_PLANAR:
        case AV_CODEC_ID_DSD_MSBF_PLANAR:
            info.codec = "DSD";
            info.dsd = true;
            info.lossless = true;
            break;
        default: {
            const std::string container = fmt->iformat != nullptr ? fmt->iformat->name : "";
            if (desc != nullptr && std::strncmp(desc->name, "pcm_", 4) == 0) {
                info.codec = container.find("aiff") != std::string::npos ? "AIFF"
                             : container.find("wav") != std::string::npos ? "WAV"
                                                                           : "PCM";
                info.lossless = true;
            } else {
                info.codec = desc != nullptr ? desc->name : "?";
                std::transform(info.codec.begin(), info.codec.end(), info.codec.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            }
        }
    }
}

}  // namespace

struct Decoder::Impl {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int stream = -1;
    AVRational timeBase{1, 1};

    int outRate = 0;
    int outChannels = 0;
    AVChannelLayout outLayout{};

    // Input format the resampler was built for; rebuilt if a stream changes format mid-way.
    int swrInRate = 0;
    int swrInFormat = -1;
    AVChannelLayout swrInLayout{};

    std::vector<float> pending;   // converted frames not yet handed out
    std::size_t pendingPos = 0;   // in frames
    bool inputDone = false;       // demuxer reached the end, decoder told to drain
    bool decoderDone = false;     // decoder fully drained, resampler flushed
    bool finished = false;
    std::int64_t skipToPts = AV_NOPTS_VALUE;  // after a seek: drop audio before this
    std::int64_t positionFrames = 0;
    TrackInfo info;

    ~Impl() {
        swr_free(&swr);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&fmt);
        av_channel_layout_uninit(&outLayout);
        av_channel_layout_uninit(&swrInLayout);
    }

    std::size_t pendingFrames() const { return pending.size() / static_cast<std::size_t>(outChannels); }

    bool ensureResampler(const AVFrame* f) {
        AVChannelLayout inLayout{};
        if (f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || f->ch_layout.nb_channels == 0) {
            av_channel_layout_default(&inLayout, std::max(1, f->ch_layout.nb_channels));
        } else {
            av_channel_layout_copy(&inLayout, &f->ch_layout);
        }

        if (swr != nullptr && swrInRate == f->sample_rate && swrInFormat == f->format &&
            sameLayout(swrInLayout, inLayout)) {
            av_channel_layout_uninit(&inLayout);
            return true;
        }

        swr_free(&swr);
        const int r = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, outRate, &inLayout,
                                          static_cast<AVSampleFormat>(f->format), f->sample_rate, 0, nullptr);
        if (r < 0 || swr_init(swr) < 0) {
            swr_free(&swr);
            av_channel_layout_uninit(&inLayout);
            return false;
        }
        swrInRate = f->sample_rate;
        swrInFormat = f->format;
        av_channel_layout_uninit(&swrInLayout);
        swrInLayout = inLayout;  // takes ownership
        return true;
    }

    // Resamples `f` (or flushes the resampler when f is null) and appends to `pending`.
    void convert(const AVFrame* f) {
        if (swr == nullptr) {
            return;
        }
        const int inSamples = f != nullptr ? f->nb_samples : 0;
        const int maxOut = swr_get_out_samples(swr, inSamples);
        if (maxOut <= 0) {
            return;
        }
        const std::size_t old = pending.size();
        pending.resize(old + static_cast<std::size_t>(maxOut) * static_cast<std::size_t>(outChannels));
        auto* outPtr = reinterpret_cast<std::uint8_t*>(pending.data() + old);
        const int got = swr_convert(swr, &outPtr, maxOut,
                                    f != nullptr ? const_cast<const std::uint8_t**>(f->extended_data) : nullptr,
                                    inSamples);
        pending.resize(old + static_cast<std::size_t>(std::max(got, 0)) * static_cast<std::size_t>(outChannels));

        // After a seek, FFmpeg lands on a packet at or before the target. Drop the extra part.
        if (f != nullptr && skipToPts != AV_NOPTS_VALUE && f->best_effort_timestamp != AV_NOPTS_VALUE) {
            const std::int64_t dropIn =
                av_rescale_q(skipToPts - f->best_effort_timestamp, timeBase, AVRational{1, f->sample_rate});
            if (dropIn <= 0) {
                skipToPts = AV_NOPTS_VALUE;
            } else if (dropIn >= f->nb_samples) {
                pending.resize(old);  // whole frame is before the target
            } else {
                const auto dropOut = static_cast<std::size_t>(av_rescale(dropIn, outRate, f->sample_rate));
                const std::size_t produced = (pending.size() - old) / static_cast<std::size_t>(outChannels);
                const std::size_t n = std::min(dropOut, produced) * static_cast<std::size_t>(outChannels);
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(old),
                              pending.begin() + static_cast<std::ptrdiff_t>(old + n));
                skipToPts = AV_NOPTS_VALUE;
            }
        }
    }

    // Decodes until `pending` has frames. Returns false at the end of the track.
    bool decodeMore() {
        pending.clear();
        pendingPos = 0;
        if (decoderDone) {
            return false;
        }
        for (;;) {
            int r = avcodec_receive_frame(codec, frame);
            if (r == 0) {
                if (ensureResampler(frame)) {
                    convert(frame);
                }
                av_frame_unref(frame);
                if (!pending.empty()) {
                    return true;
                }
                continue;
            }
            if (r != AVERROR(EAGAIN)) {
                // AVERROR_EOF (fully drained) or a decoder error: the track ends here.
                // Flushing the resampler hands out its last few samples, so nothing is cut off
                // and nothing is added: this is what keeps track joins gapless.
                decoderDone = true;
                convert(nullptr);
                return !pending.empty();
            }

            // The decoder wants more input.
            if (inputDone) {
                decoderDone = true;  // shouldn't happen: a drained decoder returns EOF
                return false;
            }
            r = av_read_frame(fmt, packet);
            if (r < 0) {
                avcodec_send_packet(codec, nullptr);  // end of file: ask the decoder to drain
                inputDone = true;
                continue;
            }
            if (packet->stream_index == stream) {
                // A damaged packet returns AVERROR_INVALIDDATA; skip it and keep going.
                avcodec_send_packet(codec, packet);
            }
            av_packet_unref(packet);
        }
    }
};

Decoder::Decoder(std::unique_ptr<Impl> impl) : d_(std::move(impl)) {}
Decoder::~Decoder() = default;

std::unique_ptr<Decoder> Decoder::open(const std::string& utf8Path, int outRate, int outChannels,
                                       std::string& error) {
    static std::once_flag logOnce;
    std::call_once(logOnce, [] { av_log_set_level(AV_LOG_ERROR); });

    auto d = std::make_unique<Impl>();
    d->outRate = outRate;
    d->outChannels = outChannels;
    av_channel_layout_default(&d->outLayout, outChannels);

    // FFmpeg's file protocol takes UTF-8 on every platform, including Windows.
    // Paths over 260 characters need the Windows extended-length form (longPathSafe changes nothing else).
    const std::string openPath = pathToUtf8(longPathSafe(pathFromUtf8(utf8Path)));
    int r = avformat_open_input(&d->fmt, openPath.c_str(), nullptr, nullptr);
    if (r < 0) {
        error = "Can't open file: " + ffmpegError(r);
        return nullptr;
    }
    r = avformat_find_stream_info(d->fmt, nullptr);
    if (r < 0) {
        error = "Can't read stream info: " + ffmpegError(r);
        return nullptr;
    }
    const AVCodec* codec = nullptr;
    d->stream = av_find_best_stream(d->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (d->stream < 0 || codec == nullptr) {
        error = "No playable audio stream";
        return nullptr;
    }
    AVStream* st = d->fmt->streams[d->stream];
    d->timeBase = st->time_base;

    // Tell the demuxer to skip other streams (cover art is stored as a video stream).
    for (unsigned i = 0; i < d->fmt->nb_streams; ++i) {
        if (static_cast<int>(i) != d->stream) {
            d->fmt->streams[i]->discard = AVDISCARD_ALL;
        }
    }

    d->codec = avcodec_alloc_context3(codec);
    if (d->codec == nullptr || avcodec_parameters_to_context(d->codec, st->codecpar) < 0) {
        error = "Can't set up decoder";
        return nullptr;
    }
    // Needed so the decoder applies encoder delay/padding from the container (gapless MP3/AAC).
    d->codec->pkt_timebase = st->time_base;
    r = avcodec_open2(d->codec, codec, nullptr);
    if (r < 0) {
        error = "Can't open decoder: " + ffmpegError(r);
        return nullptr;
    }

    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (d->packet == nullptr || d->frame == nullptr) {
        error = "Out of memory";
        return nullptr;
    }

    // ---- track info ----
    TrackInfo& info = d->info;
    const AVCodecParameters* par = st->codecpar;
    describeCodec(d->fmt, par, info);
    info.channels = par->ch_layout.nb_channels;
    info.sampleRate = info.dsd ? par->sample_rate * 8 : par->sample_rate;  // FFmpeg reports DSD rate / 8
    if (info.lossless && !info.dsd) {
        int bits = par->bits_per_raw_sample;
        if (bits == 0) bits = d->codec->bits_per_raw_sample;
        if (bits == 0) bits = par->bits_per_coded_sample;
        if (bits == 0) bits = av_get_bytes_per_sample(d->codec->sample_fmt) * 8;
        info.bitDepth = bits;
    }
    const std::int64_t bitrate = par->bit_rate > 0 ? par->bit_rate : d->fmt->bit_rate;
    info.bitrateKbps = static_cast<int>(std::max<std::int64_t>(0, bitrate) / 1000);
    if (d->fmt->duration != AV_NOPTS_VALUE && d->fmt->duration > 0) {
        info.durationMs = av_rescale(d->fmt->duration, 1000, AV_TIME_BASE);
    } else if (st->duration != AV_NOPTS_VALUE && st->duration > 0) {
        info.durationMs = av_rescale_q(st->duration, st->time_base, AVRational{1, 1000});
    }

    return std::unique_ptr<Decoder>(new Decoder(std::move(d)));
}

const TrackInfo& Decoder::info() const noexcept { return d_->info; }
int Decoder::outRate() const noexcept { return d_->outRate; }
int Decoder::outChannels() const noexcept { return d_->outChannels; }
bool Decoder::finished() const noexcept { return d_->finished; }
std::int64_t Decoder::positionFrames() const noexcept { return d_->positionFrames; }

std::int64_t Decoder::remainingMs() const noexcept {
    if (d_->info.durationMs <= 0) {
        return -1;
    }
    const std::int64_t played = d_->positionFrames * 1000 / d_->outRate;
    return std::max<std::int64_t>(0, d_->info.durationMs - played);
}

std::size_t Decoder::read(float* out, std::size_t maxFrames) {
    Impl& d = *d_;
    const auto ch = static_cast<std::size_t>(d.outChannels);
    std::size_t done = 0;
    while (done < maxFrames) {
        const std::size_t avail = d.pendingFrames() - d.pendingPos;
        if (avail > 0) {
            const std::size_t n = std::min(avail, maxFrames - done);
            std::memcpy(out + done * ch, d.pending.data() + d.pendingPos * ch, n * ch * sizeof(float));
            d.pendingPos += n;
            done += n;
            continue;
        }
        if (d.finished) {
            break;
        }
        if (!d.decodeMore()) {
            d.finished = true;
        }
    }
    d.positionFrames += static_cast<std::int64_t>(done);
    return done;
}

bool Decoder::seek(std::int64_t ms) {
    Impl& d = *d_;
    ms = std::max<std::int64_t>(0, ms);
    std::int64_t ts = av_rescale_q(ms, AVRational{1, 1000}, d.timeBase);
    const AVStream* st = d.fmt->streams[d.stream];
    if (st->start_time != AV_NOPTS_VALUE) {
        ts += st->start_time;
    }
    if (av_seek_frame(d.fmt, d.stream, ts, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }
    avcodec_flush_buffers(d.codec);
    swr_free(&d.swr);  // rebuilt on the next frame, without old samples in its filter
    d.pending.clear();
    d.pendingPos = 0;
    d.inputDone = false;
    d.decoderDone = false;
    d.finished = false;
    d.skipToPts = ts;
    d.positionFrames = ms * d.outRate / 1000;
    return true;
}

}  // namespace mp::core::audio
