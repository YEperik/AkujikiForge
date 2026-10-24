#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ps2probe {

struct SignatureHit {
    std::size_t offset{};
    std::string signature;
};

struct IndexGroup {
    std::uint32_t table_offset{};
    std::uint32_t count{};
    std::vector<std::uint32_t> targets;
};

struct IzcaIndexDirectory {
    std::uint32_t declared_size{};
    std::uint32_t field_08{};
    std::uint32_t group_count{};
    std::vector<IndexGroup> groups;
};

struct BdaEntry {
    std::string name;
    std::uint32_t data_offset{};
    std::uint32_t field_14{};
    std::uint32_t field_18{};
    std::uint32_t data_size{};
};

struct BdaDirectory {
    std::uint32_t entry_count{};
    std::vector<BdaEntry> entries;
};

struct ReadResult {
    bool ok{};
    std::vector<std::uint8_t> bytes;
    std::string error;
};

ReadResult read_file(const std::filesystem::path& path,
                     std::uint64_t maximum_bytes = (1ULL << 30U));
std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes,
                          std::size_t offset,
                          bool& ok);
double shannon_entropy(const std::vector<std::uint8_t>& bytes);
std::vector<SignatureHit> scan_signatures(const std::vector<std::uint8_t>& bytes);
std::optional<IzcaIndexDirectory> parse_izca_index_directory(
    const std::vector<std::uint8_t>& bytes,
    std::string& rejection_reason,
    std::size_t container_base_offset = 0);
std::optional<BdaDirectory> parse_bda_directory(
    const std::vector<std::uint8_t>& bytes,
    std::string& rejection_reason);
std::string printable_tag_at(const std::vector<std::uint8_t>& bytes,
                             std::uint32_t offset);
std::string hex_value(std::uint64_t value);

} // namespace ps2probe
