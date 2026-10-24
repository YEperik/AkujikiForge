#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace ps2probe {

struct MetfRecord {
    std::uint32_t field0{};
    std::uint32_t field1{};
    std::uint32_t ordinal{};
};

struct MetfTable {
    std::uint32_t record_count{};
    std::uint32_t payload_size{};
    std::vector<MetfRecord> records;
};

// Parses the observed METF layout: count followed by count records of three LE u32 fields.
// parent_end is an absolute, exclusive end offset for the containing bundle.
std::optional<MetfTable> parse_metf_table(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t chunk_offset,
    std::uint64_t parent_end,
    std::string& rejection_reason);

// Extracts null-terminated .hts names from a bounded byte range.
std::vector<std::string> extract_hts_paths(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t range_start,
    std::uint64_t range_end);

// Correlates MLE0/METL/HMDL/METF/HTSF slots inside the observed 47 child IZCA bundles.
void print_material_link_report(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<std::uint32_t>& child_offsets,
    std::ostream& output);

} // namespace ps2probe
