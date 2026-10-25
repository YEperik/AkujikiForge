#include "akuijikiforge/inspector.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace akuijikiforge {

ReadResult read_file(const std::filesystem::path& path, std::uint64_t maximum_bytes) {
    ReadResult result;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        result.error = "cannot read file metadata: " + ec.message();
        return result;
    }
    if (size > maximum_bytes) {
        result.error = "file exceeds the configured read limit";
        return result;
    }
    if (size > static_cast<std::uint64_t>(result.bytes.max_size())) {
        result.error = "file is too large for this platform";
        return result;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.error = "cannot open file";
        return result;
    }

    result.bytes.resize(static_cast<std::size_t>(size));
    if (!result.bytes.empty()) {
        stream.read(reinterpret_cast<char*>(result.bytes.data()),
                    static_cast<std::streamsize>(result.bytes.size()));
        if (!stream || static_cast<std::size_t>(stream.gcount()) != result.bytes.size()) {
            result.bytes.clear();
            result.error = "failed while reading file contents";
            return result;
        }
    }
    result.ok = true;
    return result;
}

double shannon_entropy(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return 0.0;
    std::array<std::uint64_t, 256> counts{};
    for (const auto byte : bytes) ++counts[byte];

    double entropy = 0.0;
    const double total = static_cast<double>(bytes.size());
    for (const auto count : counts) {
        if (count == 0) continue;
        const double probability = static_cast<double>(count) / total;
        entropy -= probability * std::log2(probability);
    }
    return entropy;
}

std::vector<SignatureMatch> detect_signatures(const std::vector<std::uint8_t>& bytes) {
    struct Candidate { const char* label; std::array<std::uint8_t, 8> bytes; std::size_t length; };
    static constexpr Candidate signatures[] = {
        {"ELF executable", {0x7F, 'E', 'L', 'F'}, 4},
        {"PNG image", {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, 8},
        {"JPEG image", {0xFF, 0xD8, 0xFF}, 3},
        {"GIF image", {'G', 'I', 'F', '8'}, 4},
        {"RIFF container", {'R', 'I', 'F', 'F'}, 4},
        {"Ogg container", {'O', 'g', 'g', 'S'}, 4},
        {"FLAC audio", {'f', 'L', 'a', 'C'}, 4},
        {"ZIP archive", {'P', 'K', 0x03, 0x04}, 4},
        {"PDF document", {'%', 'P', 'D', 'F', '-'}, 5},
        {"BMP image", {'B', 'M'}, 2},
        {"ID3 audio tag", {'I', 'D', '3'}, 3},
    };

    std::vector<SignatureMatch> matches;
    for (const auto& candidate : signatures) {
        if (bytes.size() < candidate.length) continue;
        bool matches_header = true;
        for (std::size_t i = 0; i < candidate.length; ++i) {
            if (bytes[i] != candidate.bytes[i]) {
                matches_header = false;
                break;
            }
        }
        if (matches_header) matches.push_back({candidate.label, 0});
    }
    return matches;
}

std::string hex_preview(const std::vector<std::uint8_t>& bytes, std::size_t maximum_bytes) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    const auto count = bytes.size() < maximum_bytes ? bytes.size() : maximum_bytes;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) output << ' ';
        output << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    }
    if (bytes.size() > count) output << " …";
    return output.str();
}

} // namespace akuijikiforge
