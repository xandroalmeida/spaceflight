#include "core/time/coordinate_time.hpp"

#include <iomanip>
#include <sstream>

namespace sf::time {

std::string CoordinateTime::to_string() const {
    std::ostringstream os;
    os << "J2000" << (seconds_since_j2000() < 0.0 ? " - " : " + ") << std::setprecision(17)
       << std::abs(seconds_since_j2000()) << " s (TDB)";
    return os.str();
}

}  // namespace sf::time
