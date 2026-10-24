#include "ps2probe/probe.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

namespace ps2probe {
namespace {

struct KnownSignature {
    const char* text;
    std::size_t length;
};

constexpr std::array<KnownSignature, 18> kKnownSignatures{{
    {"IZCA", 4}, {"IZMA", 4}, {"EOFC", 4}, {"MLX0", 4},
    {"MXTL", 4}, {"HMDL", 4}, {"KFMD", 4}, {"HMOT", 4},
    {"MXTF", 4}, {"HTSF", 4}, {"PPPL", 4}, {"COLC", 4},
    {"PECK", 4}, {"ISHD", 4}, {"IECS", 4}, {"ISBD", 4},
    {"MFNT", 4}, {"MFGT", 4}
}};

bool range_fits(std::size_t size, std::uint64_t offset, std::uint64_t length) {
    return offset <= static_cast<std::uint64_t>(size) &&
           length <= static_cast<std::uint64_t>(size) - offset;
}

bool has_magic(const std::vector<std::uint8_t>& bytes, const char* magic, std::size_t offset = 0) {
    return range_fits(bytes.size(), offset, 4) &&
           bytes[offset] == static_cast<std::uint8_t>(magic[0]) &&
           bytes[offset + 1] == static_cast<std::uint8_t>(magic[1]) &&
           bytes[offset + 2] == static_cast<std::uint8_t>(magic[2]) &&
           bytes[offset + 3] == static_cast<std::uint8_t>(magic[3]);
}

} // namespace

ReadResult read_file(const std::filesystem::path& path, std::uint64_t maximum_bytes) {
    ReadResult result;
    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) {
        result.error = "cannot determine file size: " + ec.message();
        return result;
    }
    if (file_size > maximum_bytes) {
        result.error = "file exceeds configured read limit of " +
                       std::to_string(maximum_bytes) + " bytes";
        return result;
    }
    if (file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        result.error = "file is too large for this platform's address space";
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = "cannot open file";
        return result;
    }

    result.bytes.resize(static_cast<std::size_t>(file_size));
    if (!result.bytes.empty()) {
        input.read(reinterpret_cast<char*>(result.bytes.data()),
                   static_cast<std::streamsize>(result.bytes.size()));
        if (!input || static_cast<std::size_t>(input.gcount()) != result.bytes.size()) {
            result.bytes.clear();
            result.error = "short read while loading file";
            return result;
        }
    }
    result.ok = true;
    return result;
}

std::uint32_t read_u32_le(const std::vector<std::uint8_t>& bytes,
                          std::size_t offset,
                          bool& ok) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        ok = false;
        return 0;
    }
    ok = true;
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

double shannon_entropy(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return 0.0;
    }
    std::array<std::uint64_t, 256> counts{};
    for (const auto byte : bytes) {
        ++counts[byte];
    }
    const double n = static_cast<double>(bytes.size());
    double entropy = 0.0;
    for (const auto count : counts) {
        if (count == 0) {
            continue;
        }
        const double probability = static_cast<double>(count) / n;
        entropy -= probability * std::log2(probability);
    }
    return entropy;
}

std::vector<SignatureHit> scan_signatures(const std::vector<std::uint8_t>& bytes) {
    std::vector<SignatureHit> hits;
    for (const auto& known : kKnownSignatures) {
        if (bytes.size() < known.length) {
            continue;
        }
        for (std::size_t i = 0; i <= bytes.size() - known.length; ++i) {
            bool match = true;
            for (std::size_t j = 0; j < known.length; ++j) {
                if (bytes[i + j] != static_cast<std::uint8_t>(known.text[j])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                hits.push_back({i, std::string(known.text, known.length)});
            }
        }
    }
    std::sort(hits.begin(), hits.end(), [](const SignatureHit& left, const SignatureHit& right) {
        if (left.offset != right.offset) {
            return left.offset < right.offset;
        }
        return left.signature < right.signature;
    });
    return hits;
}

std::optional<IzcaIndexDirectory> parse_izca_index_directory(
    const std::vector<std::uint8_t>& bytes,
    std::string& rejection_reason,
    std::size_t container_base_offset) {
    rejection_reason.clear();
    if (!has_magic(bytes, "IZCA", container_base_offset)) {
        rejection_reason = "container base does not begin with IZCA";
        return std::nullopt;
    }
    if (!range_fits(bytes.size(), container_base_offset, 0x18)) {
        rejection_reason = "file is too small for the candidate index header";
        return std::nullopt;
    }

    bool ok_size = false;
    bool ok_field = false;
    bool ok_count = false;
    const std::uint32_t declared_size = read_u32_le(bytes, container_base_offset + 0x04, ok_size);
    const std::uint32_t field_08 = read_u32_le(bytes, container_base_offset + 0x08, ok_field);
    const std::uint32_t group_count = read_u32_le(bytes, container_base_offset + 0x10, ok_count);
    if (!ok_size || !ok_field || !ok_count || group_count == 0 || group_count > 256) {
        rejection_reason = "candidate group count is missing or outside the safe range";
        return std::nullopt;
    }

    constexpr std::uint64_t descriptor_start_relative = 0x18;
    const std::uint64_t descriptor_bytes = static_cast<std::uint64_t>(group_count) * 8U;
    if (!range_fits(bytes.size(), static_cast<std::uint64_t>(container_base_offset) + descriptor_start_relative,
                    descriptor_bytes)) {
        rejection_reason = "descriptor array would exceed the file bounds";
        return std::nullopt;
    }
    const std::uint64_t descriptor_end_relative = descriptor_start_relative + descriptor_bytes;

    IzcaIndexDirectory directory;
    directory.declared_size = declared_size;
    directory.field_08 = field_08;
    directory.group_count = group_count;
    directory.groups.reserve(group_count);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> table_ranges;

    for (std::uint32_t i = 0; i < group_count; ++i) {
        const std::uint64_t descriptor_absolute = static_cast<std::uint64_t>(container_base_offset) +
                                                  descriptor_start_relative + static_cast<std::uint64_t>(i) * 8U;
        bool ok_offset = false;
        bool ok_item_count = false;
        const auto table_offset_relative = read_u32_le(bytes, static_cast<std::size_t>(descriptor_absolute), ok_offset);
        const auto item_count = read_u32_le(bytes, static_cast<std::size_t>(descriptor_absolute + 4U), ok_item_count);
        if (!ok_offset || !ok_item_count || item_count == 0) {
            rejection_reason = "a descriptor is unreadable or has zero items";
            return std::nullopt;
        }
        if (table_offset_relative < descriptor_end_relative) {
            rejection_reason = "a pointer table overlaps the candidate header/descriptor array";
            return std::nullopt;
        }
        const std::uint64_t table_absolute = static_cast<std::uint64_t>(container_base_offset) + table_offset_relative;
        const std::uint64_t table_bytes = static_cast<std::uint64_t>(item_count) * 4U;
        if (!range_fits(bytes.size(), table_absolute, table_bytes)) {
            rejection_reason = "a pointer table exceeds file bounds";
            return std::nullopt;
        }
        table_ranges.emplace_back(table_absolute, table_absolute + table_bytes);

        IndexGroup group;
        group.table_offset = table_offset_relative;
        group.count = item_count;
        group.targets.reserve(item_count);
        for (std::uint32_t j = 0; j < item_count; ++j) {
            const std::uint64_t item_absolute = table_absolute + static_cast<std::uint64_t>(j) * 4U;
            bool ok_target = false;
            const auto target_relative = read_u32_le(bytes, static_cast<std::size_t>(item_absolute), ok_target);
            if (!ok_target || target_relative > bytes.size() - container_base_offset) {
                rejection_reason = "an indexed target points outside the file";
                return std::nullopt;
            }
            const std::uint64_t target_absolute = static_cast<std::uint64_t>(container_base_offset) + target_relative;
            if (!range_fits(bytes.size(), target_absolute, 4)) {
                rejection_reason = "an indexed target does not have a full 4-byte tag";
                return std::nullopt;
            }
            group.targets.push_back(static_cast<std::uint32_t>(target_absolute));
        }
        directory.groups.push_back(std::move(group));
    }

    std::sort(table_ranges.begin(), table_ranges.end());
    for (std::size_t i = 1; i < table_ranges.size(); ++i) {
        if (table_ranges[i].first < table_ranges[i - 1].second) {
            rejection_reason = "candidate pointer tables overlap; header variant not accepted";
            return std::nullopt;
        }
    }
    return directory;
}

std::optional<BdaDirectory> parse_bda_directory(
    const std::vector<std::uint8_t>& bytes,
    std::string& rejection_reason) {
    rejection_reason.clear();
    if (bytes.size() < 0x20) {
        rejection_reason = "file is too small for the candidate BDA directory";
        return std::nullopt;
    }
    constexpr std::size_t marker_offset = 0x10;
    constexpr char marker[] = "B Data";
    for (std::size_t i = 0; i < sizeof(marker) - 1; ++i) {
        if (bytes[marker_offset + i] != static_cast<std::uint8_t>(marker[i])) {
            rejection_reason = "B Data marker not present at 0x10";
            return std::nullopt;
        }
    }

    bool ok_count = false;
    const std::uint32_t entry_count = read_u32_le(bytes, 0, ok_count);
    if (!ok_count || entry_count == 0 || entry_count > 128) {
        rejection_reason = "candidate entry count is missing or outside the safe range";
        return std::nullopt;
    }

    constexpr std::uint64_t entries_start = 0x20;
    constexpr std::uint64_t entry_size = 0x20;
    const std::uint64_t entries_bytes = static_cast<std::uint64_t>(entry_count) * entry_size;
    if (!range_fits(bytes.size(), entries_start, entries_bytes)) {
        rejection_reason = "BDA entry table exceeds file bounds";
        return std::nullopt;
    }
    const std::uint64_t entries_end = entries_start + entries_bytes;

    BdaDirectory directory;
    directory.entry_count = entry_count;
    directory.entries.reserve(entry_count);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> data_ranges;
    for (std::uint32_t i = 0; i < entry_count; ++i) {
        const std::size_t base = static_cast<std::size_t>(entries_start +
                                    static_cast<std::uint64_t>(i) * entry_size);
        std::string name;
        for (std::size_t j = 0; j < 16; ++j) {
            const auto byte = bytes[base + j];
            if (byte == 0 || byte == 0xff) {
                break;
            }
            if (byte < 0x20 || byte > 0x7e) {
                rejection_reason = "BDA entry has a non-printable filename field";
                return std::nullopt;
            }
            name.push_back(static_cast<char>(byte));
        }
        if (name.empty()) {
            rejection_reason = "BDA entry has an empty filename";
            return std::nullopt;
        }

        bool ok_offset = false;
        bool ok_field14 = false;
        bool ok_field18 = false;
        bool ok_size = false;
        const auto data_offset = read_u32_le(bytes, base + 0x10, ok_offset);
        const auto field_14 = read_u32_le(bytes, base + 0x14, ok_field14);
        const auto field_18 = read_u32_le(bytes, base + 0x18, ok_field18);
        const auto data_size = read_u32_le(bytes, base + 0x1c, ok_size);
        if (!ok_offset || !ok_field14 || !ok_field18 || !ok_size || data_size == 0) {
            rejection_reason = "BDA entry contains unreadable fields or an empty data range";
            return std::nullopt;
        }
        if (data_offset < entries_end || !range_fits(bytes.size(), data_offset, data_size)) {
            rejection_reason = "BDA entry data range overlaps the directory or exceeds file bounds";
            return std::nullopt;
        }
        data_ranges.emplace_back(data_offset, static_cast<std::uint64_t>(data_offset) + data_size);
        directory.entries.push_back({name, data_offset, field_14, field_18, data_size});
    }

    std::sort(data_ranges.begin(), data_ranges.end());
    for (std::size_t i = 1; i < data_ranges.size(); ++i) {
        if (data_ranges[i].first < data_ranges[i - 1].second) {
            rejection_reason = "BDA entry data ranges overlap; candidate layout not accepted";
            return std::nullopt;
        }
    }
    return directory;
}

std::string printable_tag_at(const std::vector<std::uint8_t>& bytes,
                             std::uint32_t offset) {
    if (!range_fits(bytes.size(), offset, 4)) {
        return "----";
    }
    std::string tag;
    tag.reserve(4);
    for (std::size_t i = 0; i < 4; ++i) {
        const auto byte = bytes[static_cast<std::size_t>(offset) + i];
        tag.push_back(byte >= 0x20 && byte <= 0x7e ? static_cast<char>(byte) : '.');
    }
    return tag;
}

std::string hex_value(std::uint64_t value) {
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << value;
    return output.str();
}

} // namespace ps2probe
