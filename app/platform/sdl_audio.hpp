#pragma once

// The AudioSink over SDL audio streams: one stream per looping clip, a pool of
// eight for the one-shots. Eight and not one: a single player would cut the
// previous click at every click, and twelve thrusters lighting together are
// twelve shots in one frame.

#include "app/presentation/audio_director.hpp"

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

namespace sf::platform {

class SdlAudio final : public app::AudioSink {
public:
    explicit SdlAudio(const std::string& asset_directory);
    ~SdlAudio() override;
    SdlAudio(const SdlAudio&) = delete;
    SdlAudio& operator=(const SdlAudio&) = delete;

    [[nodiscard]] bool available() const { return device_ != 0; }

    void set_loop(const std::string& clip, double level, double pitch) override;
    void play(const std::string& clip, double level, double pitch) override;

    // Keeps the loops fed. Once per frame.
    void pump();

private:
    struct Clip {
        SDL_AudioSpec spec{};
        std::vector<Uint8> data;
    };
    struct Loop {
        SDL_AudioStream* stream{nullptr};
        const Clip* clip{nullptr};
    };

    SDL_AudioDeviceID device_{0};
    std::map<std::string, Clip> clips_;
    std::map<std::string, Loop> loops_;
    std::vector<SDL_AudioStream*> shots_;
    std::size_t next_shot_{0};
};

}  // namespace sf::platform
