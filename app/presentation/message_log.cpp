#include "app/presentation/message_log.hpp"

namespace sf::app {

void MessageLog::post(const std::string& text, MessageLevel level) {
    history_.push_back(HistoryEntry{text, level, clock_});
    while (history_.size() > HISTORY_LIMIT) {
        history_.pop_front();
    }
    entries_.push_back(VisibleMessage{text, level, 0.0, 1.0F});
    while (entries_.size() > MAX_VISIBLE) {
        entries_.pop_front();
    }
}

void MessageLog::advance(double delta) {
    clock_ += delta;
    std::deque<VisibleMessage> survivors;
    for (auto& entry : entries_) {
        entry.age += delta;
        if (entry.age > HOLD_SECONDS + FADE_SECONDS) {
            continue;
        }
        entry.opacity = entry.age > HOLD_SECONDS
                            ? static_cast<float>(1.0 - (entry.age - HOLD_SECONDS) / FADE_SECONDS)
                            : 1.0F;
        survivors.push_back(entry);
    }
    entries_ = std::move(survivors);
}

Colour MessageLog::colour(MessageLevel level) {
    switch (level) {
        case MessageLevel::Mission: return palette::NAV;
        case MessageLevel::Warning: return palette::WARNING;
        case MessageLevel::Critical: return palette::CRITICAL;
        case MessageLevel::Info: break;
    }
    return palette::PRIMARY;
}

const char* MessageLog::level_name(MessageLevel level) {
    switch (level) {
        case MessageLevel::Mission: return "MISSION";
        case MessageLevel::Warning: return "WARNING";
        case MessageLevel::Critical: return "CRITICAL";
        case MessageLevel::Info: break;
    }
    return "INFO";
}

}  // namespace sf::app
