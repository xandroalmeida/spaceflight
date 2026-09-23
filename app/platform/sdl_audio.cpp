#include "app/platform/sdl_audio.hpp"

#include <algorithm>

namespace sf::platform {
namespace {

const char* const kClips[][2] = {
    {"engine", "audio/engine_loop.wav"},     {"ventilation", "audio/ventilation.wav"},
    {"rcs", "audio/rcs_thump.wav"},          {"switch", "audio/switch_click.wav"},
    {"button", "audio/button_press.wav"},    {"warning", "audio/warning_tone.wav"},
    {"notify", "audio/computer_notify.wav"},
};

}  // namespace

SdlAudio::SdlAudio(const std::string& asset_directory) {
    // SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK is a C cast in SDL's header.
    const auto default_playback = static_cast<SDL_AudioDeviceID>(0xFFFFFFFFu);
    device_ = SDL_OpenAudioDevice(default_playback, nullptr);
    if (device_ == 0) {
        SDL_Log("no audio device: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec output{};
    SDL_GetAudioDeviceFormat(device_, &output, nullptr);
    for (const auto& entry : kClips) {
        Clip clip{};
        Uint8* buffer = nullptr;
        Uint32 length = 0;
        const std::string path = asset_directory + "/" + entry[1];
        if (!SDL_LoadWAV(path.c_str(), &clip.spec, &buffer, &length)) {
            SDL_Log("cannot load %s: %s", path.c_str(), SDL_GetError());
            continue;
        }
        clip.data.assign(buffer, buffer + length);
        SDL_free(buffer);
        clips_.emplace(entry[0], std::move(clip));
    }
    for (int i = 0; i < 8; ++i) {
        SDL_AudioStream* stream = SDL_CreateAudioStream(&clips_.begin()->second.spec, &output);
        if (stream != nullptr && SDL_BindAudioStream(device_, stream)) {
            shots_.push_back(stream);
        }
    }
}

SdlAudio::~SdlAudio() {
    for (auto& [name, loop] : loops_) {
        SDL_DestroyAudioStream(loop.stream);
    }
    for (auto* stream : shots_) {
        SDL_DestroyAudioStream(stream);
    }
    if (device_ != 0) {
        SDL_CloseAudioDevice(device_);
    }
}

void SdlAudio::set_loop(const std::string& clip, double level, double pitch) {
    if (device_ == 0) {
        return;
    }
    auto it = loops_.find(clip);
    if (it == loops_.end()) {
        const auto found = clips_.find(clip);
        if (found == clips_.end()) {
            return;
        }
        SDL_AudioSpec output{};
        SDL_GetAudioDeviceFormat(device_, &output, nullptr);
        SDL_AudioStream* stream = SDL_CreateAudioStream(&found->second.spec, &output);
        if (stream == nullptr || !SDL_BindAudioStream(device_, stream)) {
            return;
        }
        it = loops_.emplace(clip, Loop{stream, &found->second}).first;
    }
    SDL_SetAudioStreamGain(it->second.stream, static_cast<float>(std::clamp(level, 0.0, 1.0)));
    SDL_SetAudioStreamFrequencyRatio(it->second.stream, static_cast<float>(std::clamp(pitch, 0.25, 4.0)));
}

void SdlAudio::pump() {
    for (auto& [name, loop] : loops_) {
        const auto size = static_cast<int>(loop.clip->data.size());
        if (SDL_GetAudioStreamQueued(loop.stream) < size) {
            SDL_PutAudioStreamData(loop.stream, loop.clip->data.data(), size);
        }
    }
}

void SdlAudio::play(const std::string& clip, double level, double pitch) {
    if (device_ == 0 || shots_.empty()) {
        return;
    }
    const auto found = clips_.find(clip);
    if (found == clips_.end()) {
        return;
    }
    SDL_AudioStream* stream = shots_[next_shot_];
    next_shot_ = (next_shot_ + 1) % shots_.size();
    SDL_ClearAudioStream(stream);
    SDL_SetAudioStreamFormat(stream, &found->second.spec, nullptr);
    SDL_SetAudioStreamGain(stream, static_cast<float>(std::clamp(level, 0.0, 1.0)));
    SDL_SetAudioStreamFrequencyRatio(stream, static_cast<float>(std::clamp(pitch, 0.25, 4.0)));
    SDL_PutAudioStreamData(stream, found->second.data.data(), static_cast<int>(found->second.data.size()));
}

}  // namespace sf::platform
