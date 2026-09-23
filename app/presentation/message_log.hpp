#pragma once

// The mission messages, discreet (rule 67).
//
// Each line appears, holds, and fades. No entry animation, no box, no arcade
// sound: what the ship says to the pilot has to read as telemetry and not as an
// unlocked achievement.
//
// Every line is kept in the history, which the technical read-out can show in
// full. A message that went by unseen is a message that did not exist.

#include "app/presentation/palette.hpp"

#include <deque>
#include <string>
#include <vector>

namespace sf::app {

enum class MessageLevel { Info, Mission, Warning, Critical };

struct VisibleMessage {
    std::string text;
    MessageLevel level{MessageLevel::Info};
    double age{0.0};
    float opacity{1.0F};
};

struct HistoryEntry {
    std::string text;
    MessageLevel level{MessageLevel::Info};
    double at_seconds{0.0};
};

class MessageLog {
public:
    static constexpr double HOLD_SECONDS = 6.0;
    static constexpr double FADE_SECONDS = 1.6;
    static constexpr std::size_t MAX_VISIBLE = 6;
    static constexpr std::size_t HISTORY_LIMIT = 200;

    void post(const std::string& text, MessageLevel level = MessageLevel::Info);
    void advance(double delta);

    [[nodiscard]] const std::deque<VisibleMessage>& visible() const { return entries_; }
    [[nodiscard]] const std::deque<HistoryEntry>& history() const { return history_; }
    [[nodiscard]] bool shown() const { return shown_; }
    void set_shown(bool value) { shown_ = value; }

    [[nodiscard]] static Colour colour(MessageLevel level);
    [[nodiscard]] static const char* level_name(MessageLevel level);

private:
    std::deque<VisibleMessage> entries_;
    std::deque<HistoryEntry> history_;
    double clock_{0.0};
    bool shown_{true};
};

}  // namespace sf::app
