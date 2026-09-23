#include "app/presentation/format.hpp"

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace sf::app::fmt {

std::string format(const char* pattern, ...) {
    va_list args;
    va_start(args, pattern);
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(nullptr, 0, pattern, copy);
    va_end(copy);
    std::string out;
    if (needed > 0) {
        std::vector<char> buffer(static_cast<std::size_t>(needed) + 1);
        std::vsnprintf(buffer.data(), buffer.size(), pattern, args);
        out.assign(buffer.data(), static_cast<std::size_t>(needed));
    }
    va_end(args);
    return out;
}

std::string fixed(double value, int decimals) { return format("%.*f", decimals, value); }

std::string num(double value, int decimals) {
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value > 0.0 ? "inf" : "-inf";
    }
    return fixed(value, decimals);
}

std::string sci(double value, int digits) {
    if (!std::isfinite(value) || value == 0.0) {
        return "0";
    }
    int exponent = static_cast<int>(std::floor(std::log(std::abs(value)) / std::log(10.0)));
    if (exponent >= -3 && exponent < 6) {
        return num(value, std::max(digits - 1 - exponent, 0));
    }
    double mantissa = value / std::pow(10.0, exponent);
    if (std::abs(mantissa) >= 10.0) {
        mantissa /= 10.0;
        exponent += 1;
    }
    return num(mantissa, digits - 1) + format("e%+d", exponent);
}

std::string group(double value, int decimals) {
    std::string text = num(value, decimals);
    const bool negative = !text.empty() && text.front() == '-';
    if (negative) {
        text.erase(0, 1);
    }
    const auto dot = text.find('.');
    const std::string whole = text.substr(0, dot);
    std::string grouped;
    int counted = 0;
    for (std::size_t i = whole.size(); i-- > 0;) {
        grouped.insert(grouped.begin(), whole[i]);
        ++counted;
        if (counted % 3 == 0 && i > 0) {
            grouped.insert(grouped.begin(), ' ');
        }
    }
    if (dot != std::string::npos) {
        grouped += text.substr(dot);
    }
    return (negative ? "-" : "") + grouped;
}

std::string distance(double metres) {
    if (!std::isfinite(metres)) {
        return "--";
    }
    const double m = std::abs(metres);
    if (m < 1000.0) {
        return format("%.0f m", metres);
    }
    if (m < 0.01 * AU_M) {
        return group(metres / 1000.0, m < 1.0e7 ? 1 : 0) + " km";
    }
    return format("%.4f AU", metres / AU_M);
}

std::string speed(double ms) {
    if (!std::isfinite(ms)) {
        return "--";
    }
    const double v = std::abs(ms);
    if (v < 1000.0) {
        return format("%.1f m/s", ms);
    }
    if (v < 0.001 * C_MS) {
        return format("%.3f km/s", ms / 1000.0);
    }
    return format("%.5f c", ms / C_MS);
}

std::string duration(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return "--";
    }
    if (seconds < 120.0) {
        return format("%.1f s", seconds);
    }
    if (seconds < 7200.0) {
        return format("%.1f min", seconds / 60.0);
    }
    const auto total = static_cast<long long>(seconds);
    if (seconds < 86400.0) {
        return format("%lldh %02lldm", total / 3600, (total % 3600) / 60);
    }
    if (seconds < 3.15576e7) {
        return format("%lldd %02lldh", total / 86400, (total % 86400) / 3600);
    }
    const auto years = static_cast<long long>(seconds / 3.15576e7);
    const auto days = static_cast<long long>((seconds - static_cast<double>(years) * 3.15576e7) / 86400.0);
    return format("%lldy %lldd", years, days);
}

std::string count(long long value) {
    const double v = std::abs(static_cast<double>(value));
    if (v < 1000.0) {
        return std::to_string(value);
    }
    if (v < 1.0e6) {
        return format("%.0fk", static_cast<double>(value) / 1000.0);
    }
    return format("%.1fM", static_cast<double>(value) / 1.0e6);
}

std::string countdown(double seconds) {
    if (!std::isfinite(seconds)) {
        return "T-  --:--:--";
    }
    const char* sign_text = seconds >= 0.0 ? "T-" : "T+";
    const auto total = static_cast<long long>(std::abs(seconds));
    const long long days = total / 86400;
    const long long hours = (total % 86400) / 3600;
    const long long minutes = (total % 3600) / 60;
    const long long secs = total % 60;
    if (days > 0) {
        return format("%s%lldd %02lld:%02lld:%02lld", sign_text, days, hours, minutes, secs);
    }
    return format("%s%02lld:%02lld:%02lld", sign_text, hours, minutes, secs);
}

std::string mass(double kg) {
    if (!std::isfinite(kg)) {
        return "--";
    }
    if (std::abs(kg) < 1000.0) {
        return format("%.1f kg", kg);
    }
    return format("%.2f t", kg / 1000.0);
}

std::string force(double newtons) {
    if (!std::isfinite(newtons)) {
        return "--";
    }
    if (std::abs(newtons) < 1000.0) {
        return format("%.1f N", newtons);
    }
    if (std::abs(newtons) < 1.0e6) {
        return format("%.2f kN", newtons / 1000.0);
    }
    return format("%.2f MN", newtons / 1.0e6);
}

std::string percent(double fraction) { return format("%.0f %%", fraction * 100.0); }

std::string angle(double degrees) { return format("%.2f°", degrees); }

std::string warp(double factor) { return group(factor, 0) + "x"; }

std::string upper(std::string text) {
    for (auto& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

std::size_t display_width(const std::string& text) {
    std::size_t width = 0;
    for (const char c : text) {
        // Count every byte that is not a UTF-8 continuation byte.
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
            ++width;
        }
    }
    return width;
}

std::string rpad(const std::string& text, std::size_t width) {
    const std::size_t have = display_width(text);
    return have >= width ? text : text + std::string(width - have, ' ');
}

std::string on_off(bool value) { return value ? "ON" : "OFF"; }

}  // namespace sf::app::fmt
