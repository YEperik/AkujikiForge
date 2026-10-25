#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace akuijikiforge {

struct ReadResult {
    bool ok{false};
    std::vector<std::uint8_t> bytes;
    std::string error;
};

struct SignatureMatch {
    std::string label;
    std::size_t offset{};
};

ReadResult read_file(const std::filesystem::path& path,
                     std::uint64_t maximum_bytes = (1ULL << 30U));
double shannon_entropy(const std::vector<std::uint8_t>& bytes);
std::vector<SignatureMatch> detect_signatures(const std::vector<std::uint8_t>& bytes);
std::string hex_preview(const std::vector<std::uint8_t>& bytes, std::size_t maximum_bytes = 32);

} // namespace akuijikiforge
