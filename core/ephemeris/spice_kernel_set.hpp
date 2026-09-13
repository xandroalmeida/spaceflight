#pragma once

// RAII ownership of a set of loaded SPICE kernels.
//
// CSPICE keeps the kernel pool in global state, so "which kernels are loaded" is
// a property of the process, not of an object.  This class does not pretend
// otherwise: it owns the *furnsh/unload pair* for the files it loaded, in a
// deterministic order, and it is the only place in the codebase allowed to call
// furnsh.  See ADR-0003 and kernels/MANIFEST.md.

#include <filesystem>
#include <string>
#include <vector>

namespace sf::ephemeris {

class SpiceKernelSet {
public:
    // Loads every recognised kernel found directly under `dir` (recursively),
    // in type order: LSK, then text PCK/FK, then binary PCK, then SPK.
    // Throws KernelLoadError if the directory is missing or yields no kernels.
    static SpiceKernelSet from_directory(const std::filesystem::path& dir);

    // Kernel directory to use when the caller did not choose one:
    //   $SPACEFLIGHT_KERNEL_DIR, else the path configured at build time.
    static std::filesystem::path default_directory();

    explicit SpiceKernelSet(std::vector<std::filesystem::path> kernels);
    ~SpiceKernelSet();

    SpiceKernelSet(const SpiceKernelSet&) = delete;
    SpiceKernelSet& operator=(const SpiceKernelSet&) = delete;
    SpiceKernelSet(SpiceKernelSet&& other) noexcept;
    SpiceKernelSet& operator=(SpiceKernelSet&& other) noexcept;

    [[nodiscard]] const std::vector<std::filesystem::path>& loaded() const noexcept { return loaded_; }
    [[nodiscard]] std::vector<std::filesystem::path> spk_files() const;
    [[nodiscard]] bool has_leapseconds() const noexcept { return has_lsk_; }
    [[nodiscard]] std::string summary() const;

private:
    void unload_all() noexcept;

    std::vector<std::filesystem::path> loaded_;
    bool has_lsk_{false};
};

}  // namespace sf::ephemeris
