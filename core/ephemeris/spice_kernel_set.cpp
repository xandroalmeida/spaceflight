#include "core/ephemeris/spice_kernel_set.hpp"

#include "core/ephemeris/errors.hpp"
#include "core/ephemeris/spice_internal.hpp"

extern "C" {
#include "SpiceUsr.h"
}

#include <algorithm>
#include <cstdlib>
#include <sstream>

#ifndef SPACEFLIGHT_DEFAULT_KERNEL_DIR
#define SPACEFLIGHT_DEFAULT_KERNEL_DIR ""
#endif

namespace sf::ephemeris {
namespace {

namespace fs = std::filesystem;

// Load order matters: leap seconds first (other kernels' time tags depend on
// them), then constants, then trajectories.  Within a group, alphabetical, so
// that two runs on the same directory load the same things in the same order.
int type_rank(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (ext == ".tls") return 0;   // leapseconds
    if (ext == ".tpc") return 1;   // text PCK (constants)
    if (ext == ".tf")  return 2;   // frame kernel
    if (ext == ".bpc") return 3;   // binary PCK (orientation)
    if (ext == ".bsp") return 4;   // SPK (ephemerides)
    if (ext == ".bds") return 5;   // DSK
    if (ext == ".tm")  return 6;   // meta-kernel
    return -1;                     // not a kernel
}

bool is_kernel(const fs::path& p) { return type_rank(p) >= 0; }

}  // namespace

fs::path SpiceKernelSet::default_directory() {
    if (const char* env = std::getenv("SPACEFLIGHT_KERNEL_DIR"); env != nullptr && *env != '\0') {
        return fs::path{env};
    }
    return fs::path{SPACEFLIGHT_DEFAULT_KERNEL_DIR};
}

SpiceKernelSet SpiceKernelSet::from_directory(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        throw KernelLoadError("kernel directory not found: " + dir.string() +
                              " (run scripts/fetch_kernels.sh)");
    }

    std::vector<fs::path> kernels;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && is_kernel(entry.path())) {
            kernels.push_back(entry.path());
        }
    }
    if (kernels.empty()) {
        throw KernelLoadError("no SPICE kernels under " + dir.string() +
                              " (run scripts/fetch_kernels.sh)");
    }

    std::sort(kernels.begin(), kernels.end(), [](const fs::path& a, const fs::path& b) {
        const int ra = type_rank(a);
        const int rb = type_rank(b);
        return ra != rb ? ra < rb : a.string() < b.string();
    });

    return SpiceKernelSet{std::move(kernels)};
}

SpiceKernelSet::SpiceKernelSet(std::vector<fs::path> kernels) {
    detail::ensure_spice_error_handling();
    const std::lock_guard lock{detail::spice_mutex()};

    loaded_.reserve(kernels.size());
    for (const auto& k : kernels) {
        furnsh_c(k.string().c_str());
        try {
            detail::throw_if_spice_failed("furnsh(" + k.string() + ")");
        } catch (...) {
            unload_all();
            throw;
        }
        loaded_.push_back(k);
        if (type_rank(k) == 0) {
            has_lsk_ = true;
        }
    }
}

SpiceKernelSet::~SpiceKernelSet() { unload_all(); }

SpiceKernelSet::SpiceKernelSet(SpiceKernelSet&& other) noexcept
    : loaded_(std::move(other.loaded_)), has_lsk_(other.has_lsk_) {
    other.loaded_.clear();
    other.has_lsk_ = false;
}

SpiceKernelSet& SpiceKernelSet::operator=(SpiceKernelSet&& other) noexcept {
    if (this != &other) {
        unload_all();
        loaded_ = std::move(other.loaded_);
        has_lsk_ = other.has_lsk_;
        other.loaded_.clear();
        other.has_lsk_ = false;
    }
    return *this;
}

void SpiceKernelSet::unload_all() noexcept {
    if (loaded_.empty()) {
        return;
    }
    const std::lock_guard lock{detail::spice_mutex()};
    for (auto it = loaded_.rbegin(); it != loaded_.rend(); ++it) {
        unload_c(it->string().c_str());
        if (failed_c()) {
            reset_c();  // nothing useful to do while unwinding
        }
    }
    loaded_.clear();
    has_lsk_ = false;
}

std::vector<fs::path> SpiceKernelSet::spk_files() const {
    std::vector<fs::path> spks;
    for (const auto& k : loaded_) {
        if (type_rank(k) == 4) {
            spks.push_back(k);
        }
    }
    return spks;
}

std::string SpiceKernelSet::summary() const {
    std::ostringstream os;
    os << loaded_.size() << " kernel(s)";
    for (const auto& k : loaded_) {
        os << "\n  " << k.filename().string();
    }
    return os.str();
}

}  // namespace sf::ephemeris
