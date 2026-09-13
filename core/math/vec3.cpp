#include "core/math/vec3.hpp"

#include <iomanip>

namespace sf::math {

std::ostream& operator<<(std::ostream& os, const Vec3& v) {
    const auto flags = os.flags();
    const auto prec = os.precision();
    os << std::setprecision(17) << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    os.flags(flags);
    os.precision(prec);
    return os;
}

}  // namespace sf::math
