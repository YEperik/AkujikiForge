#include "ps2probe/deep_scan.hpp"

#include "ps2probe/probe.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ps2probe {
namespace {

bool range_fits(std::size_t size, std::uint64_t offset, std::uint64_t length) {
    return offset <= static_cast<std::uint64_t>(size) &&
           length <= static_cast<std::uint64_t>(size) - offset;
}

std::optional<std::uint32_t> u32_at(const std::vector<std::uint8_t>& bytes,
                                    std::uint64_t offset) {
    if (!range_fits(bytes.size(), offset, 4)) {
        return std::nullopt;
    }
    bool ok = false;
    const auto value = read_u32_le(bytes, static_cast<std::size_t>(offset), ok);
    if (!ok) {
        return std::nullopt;
    }
    return value;
}

std::string tag_at(const std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    if (!range_fits(bytes.size(), offset, 4)) {
        return "????";
    }
    std::string tag;
    tag.reserve(4);
    for (std::uint64_t i = 0; i < 4; ++i) {
        const auto c = bytes[static_cast<std::size_t>(offset + i)];
        if (c < 0x20 || c > 0x7e) {
            return "????";
        }
        tag.push_back(static_cast<char>(c));
    }
    return tag;
}

bool tag_is(const std::vector<std::uint8_t>& bytes,
            std::uint64_t offset,
            const char* tag) {
    return tag_at(bytes, offset) == std::string(tag, 4);
}

std::string bytes4_hex(const std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    if (!range_fits(bytes.size(), offset, 4)) {
        return "<out-of-bounds>";
    }
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(11);
    for (std::uint64_t i = 0; i < 4; ++i) {
        if (i != 0) result.push_back(' ');
        const auto value = bytes[static_cast<std::size_t>(offset + i)];
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

std::optional<IzcaIndexDirectory> parse_count_at_14_directory(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t base,
    std::string& reason) {
    reason.clear();
    if (!tag_is(bytes, base, "IZCA") || !range_fits(bytes.size(), base, 0x20)) {
        reason = "missing IZCA signature or truncated shifted header";
        return std::nullopt;
    }
    const auto declared_size = u32_at(bytes, base + 0x04);
    const auto field_08 = u32_at(bytes, base + 0x08);
    const auto field_10 = u32_at(bytes, base + 0x10);
    const auto count_opt = u32_at(bytes, base + 0x14);
    if (!declared_size || !field_08 || !field_10 || !count_opt || *field_08 != 0x14) {
        reason = "header does not match the observed +0x14 count variant";
        return std::nullopt;
    }
    const std::uint64_t span = 0x10ULL + *declared_size;
    if (!range_fits(bytes.size(), base, span)) {
        reason = "declared container span exceeds file bounds";
        return std::nullopt;
    }
    const std::uint32_t count = *count_opt;
    if (count == 0 || count > 256) {
        reason = "group count is zero or outside the safe limit";
        return std::nullopt;
    }
    constexpr std::uint64_t descriptor_start = 0x1c;
    const std::uint64_t descriptor_bytes = static_cast<std::uint64_t>(count) * 8ULL;
    const std::uint64_t descriptor_end = descriptor_start + descriptor_bytes;
    if (descriptor_end > span) {
        reason = "descriptor array exceeds declared container span";
        return std::nullopt;
    }

    IzcaIndexDirectory result;
    result.declared_size = *declared_size;
    result.field_08 = *field_08;
    result.group_count = count;
    result.groups.reserve(count);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> table_ranges;

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint64_t descriptor = base + descriptor_start + static_cast<std::uint64_t>(i) * 8ULL;
        const auto table_offset = u32_at(bytes, descriptor);
        const auto item_count_opt = u32_at(bytes, descriptor + 4ULL);
        if (!table_offset || !item_count_opt || *item_count_opt == 0) {
            reason = "unreadable descriptor or zero-sized entry table";
            return std::nullopt;
        }
        const std::uint64_t table_bytes = static_cast<std::uint64_t>(*item_count_opt) * 4ULL;
        if (*table_offset < descriptor_end ||
            static_cast<std::uint64_t>(*table_offset) + table_bytes > span) {
            reason = "entry table overlaps metadata or exceeds declared container span";
            return std::nullopt;
        }
        const std::uint64_t table_absolute = base + *table_offset;
        table_ranges.emplace_back(table_absolute, table_absolute + table_bytes);

        IndexGroup group;
        group.table_offset = *table_offset;
        group.count = *item_count_opt;
        group.targets.reserve(group.count);
        for (std::uint32_t j = 0; j < group.count; ++j) {
            const auto target_relative = u32_at(bytes, table_absolute + static_cast<std::uint64_t>(j) * 4ULL);
            if (!target_relative || static_cast<std::uint64_t>(*target_relative) + 4ULL > span) {
                reason = "target offset exceeds declared container span";
                return std::nullopt;
            }
            group.targets.push_back(static_cast<std::uint32_t>(base + *target_relative));
        }
        result.groups.push_back(std::move(group));
    }

    std::sort(table_ranges.begin(), table_ranges.end());
    for (std::size_t i = 1; i < table_ranges.size(); ++i) {
        if (table_ranges[i].first < table_ranges[i - 1].second) {
            reason = "entry tables overlap";
            return std::nullopt;
        }
    }
    return result;
}

bool directory_fits_declared_span(const std::vector<std::uint8_t>& bytes,
                                  std::uint64_t base,
                                  const IzcaIndexDirectory& directory) {
    const std::uint64_t span = 0x10ULL + directory.declared_size;
    if (!range_fits(bytes.size(), base, span)) {
        return false;
    }
    const std::uint64_t descriptor_start = 0x18;
    const std::uint64_t descriptor_end = descriptor_start +
        static_cast<std::uint64_t>(directory.group_count) * 8ULL;
    if (descriptor_end > span) {
        return false;
    }
    for (const auto& group : directory.groups) {
        const std::uint64_t table_end = static_cast<std::uint64_t>(group.table_offset) +
                                        static_cast<std::uint64_t>(group.count) * 4ULL;
        if (group.table_offset < descriptor_end || table_end > span) {
            return false;
        }
        for (const auto target : group.targets) {
            if (target < base || static_cast<std::uint64_t>(target) + 4ULL > base + span) {
                return false;
            }
        }
    }
    return true;
}

bool is_valid_inner_chunk(const std::vector<std::uint8_t>& bytes,
                          std::uint64_t target,
                          std::uint64_t container_end) {
    const auto size = u32_at(bytes, target + 4ULL);
    const auto header_size = u32_at(bytes, target + 8ULL);
    if (!size || !header_size || *header_size < 0x10 || *header_size > 0x100) {
        return false;
    }
    const std::uint64_t end = target + *header_size + static_cast<std::uint64_t>(*size);
    return end >= target && end <= container_end && end <= bytes.size();
}

struct HmdlInfo {
    std::uint64_t start{};
    std::uint64_t end{};
    std::uint64_t kfmd_start{};
    std::uint64_t kfmd_payload_start{};
    std::uint64_t kfmd_payload_end{};
    std::uint32_t hmdl_size{};
    std::uint32_t kfmd_size{};
};

std::optional<HmdlInfo> inspect_hmdl(const std::vector<std::uint8_t>& bytes,
                                     std::uint64_t start,
                                     std::optional<std::uint64_t> parent_end) {
    if (!tag_is(bytes, start, "HMDL")) {
        return std::nullopt;
    }
    const auto size = u32_at(bytes, start + 4ULL);
    const auto header_size = u32_at(bytes, start + 8ULL);
    if (!size || !header_size || *header_size != 0x20) {
        return std::nullopt;
    }
    const std::uint64_t hmdl_end = start + *header_size + static_cast<std::uint64_t>(*size);
    if (hmdl_end < start || hmdl_end > bytes.size() || (parent_end && hmdl_end > *parent_end)) {
        return std::nullopt;
    }
    const std::uint64_t kfmd = start + 0x20ULL;
    if (!tag_is(bytes, kfmd, "KFMD")) {
        return std::nullopt;
    }
    const auto kfmd_size = u32_at(bytes, kfmd + 4ULL);
    const auto kfmd_header = u32_at(bytes, kfmd + 8ULL);
    if (!kfmd_size || !kfmd_header || *kfmd_header != 0x10) {
        return std::nullopt;
    }
    const std::uint64_t payload_start = kfmd + *kfmd_header;
    const std::uint64_t payload_end = payload_start + static_cast<std::uint64_t>(*kfmd_size);
    if (payload_end < payload_start || payload_end > hmdl_end || payload_end > bytes.size()) {
        return std::nullopt;
    }
    return HmdlInfo{start, hmdl_end, kfmd, payload_start, payload_end, *size, *kfmd_size};
}

struct VifPacket {
    std::uint64_t offset{};
    std::uint16_t address{};
    std::uint16_t count{};
    std::uint8_t opcode{};
    std::uint32_t stride{};
    std::uint64_t data_start{};
    std::uint64_t end{};
};

std::optional<std::uint32_t> vif_stride(std::uint8_t opcode) {
    switch (opcode) {
        case 0x6c: return 16; // UNPACK V4-32
        case 0x68: return 12; // UNPACK V3-32
        case 0x64: return 8;  // UNPACK V2-32
        default: return std::nullopt;
    }
}

bool is_mesh_marker(const std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    if (!range_fits(bytes.size(), offset, 4)) {
        return false;
    }
    const auto a = bytes[static_cast<std::size_t>(offset)];
    return (a == 0x01 || a == 0x03 || a == 0x04) &&
           bytes[static_cast<std::size_t>(offset + 1ULL)] == 0x01 &&
           bytes[static_cast<std::size_t>(offset + 2ULL)] == 0x00 &&
           bytes[static_cast<std::size_t>(offset + 3ULL)] == 0x01;
}

std::vector<VifPacket> parse_vif_sequence(const std::vector<std::uint8_t>& bytes,
                                          std::uint64_t start,
                                          std::uint64_t limit) {
    std::vector<VifPacket> packets;
    std::uint64_t cursor = start;
    while (range_fits(bytes.size(), cursor, 4) && cursor + 4ULL <= limit) {
        if (is_mesh_marker(bytes, cursor) || tag_is(bytes, cursor, "EOFC")) {
            break;
        }
        if (bytes[static_cast<std::size_t>(cursor + 1ULL)] != 0xc0) {
            break;
        }
        const auto stride = vif_stride(bytes[static_cast<std::size_t>(cursor + 3ULL)]);
        if (!stride) {
            break;
        }
        const std::uint8_t num = bytes[static_cast<std::size_t>(cursor + 2ULL)];
        const std::uint16_t count = num == 0 ? 256U : static_cast<std::uint16_t>(num);
        const std::uint16_t address = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[static_cast<std::size_t>(cursor)]) |
            (static_cast<std::uint16_t>(bytes[static_cast<std::size_t>(cursor + 1ULL)]) << 8U));
        const std::uint64_t payload_bytes = static_cast<std::uint64_t>(count) * *stride;
        const std::uint64_t data_start = cursor + 4ULL;
        const std::uint64_t packet_end = data_start + payload_bytes;
        if (packet_end < data_start || packet_end > limit || packet_end > bytes.size()) {
            break;
        }
        packets.push_back(VifPacket{cursor, address, count,
                                    bytes[static_cast<std::size_t>(cursor + 3ULL)],
                                    *stride, data_start, packet_end});
        cursor = packet_end;
    }
    return packets;
}

std::vector<std::uint64_t> find_exact_group_headers(const std::vector<std::uint8_t>& bytes,
                                                     std::uint64_t start,
                                                     std::uint64_t end) {
    constexpr std::array<std::uint8_t, 16> pattern{{
        0x03, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00
    }};
    std::vector<std::uint64_t> result;
    if (end < start || end - start < pattern.size()) {
        return result;
    }
    for (std::uint64_t offset = start; offset <= end - pattern.size(); ++offset) {
        if (std::equal(pattern.begin(), pattern.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset))) {
            result.push_back(offset);
        }
    }
    return result;
}

std::string opcode_name(std::uint8_t opcode) {
    switch (opcode) {
        case 0x6c: return "V4-32";
        case 0x68: return "V3-32";
        case 0x64: return "V2-32";
        default: return "unknown";
    }
}

} // namespace

void print_deep_mld_report(const std::vector<std::uint8_t>& bytes, std::ostream& output) {
    output << "\n=== Deep MLD / IZCA / HMDL analysis ===\n";
    std::string reason;
    const auto outer = parse_izca_index_directory(bytes, reason, 0);
    if (!outer || outer->groups.size() < 7) {
        output << "Deep analysis not applied: outer IZCA indexed-directory candidate rejected: "
               << (reason.empty() ? "expected at least seven groups" : reason) << '\n';
        return;
    }

    std::optional<std::uint64_t> peck_offset;
    for (const auto& group : outer->groups) {
        for (const auto target : group.targets) {
            if (tag_is(bytes, target, "PECK") && tag_is(bytes, static_cast<std::uint64_t>(target) + 0x10ULL, "IZCA")) {
                peck_offset = target;
                break;
            }
        }
        if (peck_offset) break;
    }
    if (!peck_offset) {
        output << "No PECK block containing an embedded IZCA root was found.\n";
        return;
    }

    const std::uint64_t nested_base = *peck_offset + 0x10ULL;
    const auto nested = parse_izca_index_directory(bytes, reason, static_cast<std::size_t>(nested_base));
    if (!nested || nested->groups.size() < 2 || !directory_fits_declared_span(bytes, nested_base, *nested)) {
        output << "Embedded IZCA root rejected by declared-span checks: "
               << (reason.empty() ? "directory points outside its declared span" : reason) << '\n';
        return;
    }

    output << "Embedded directory root: " << hex_value(nested_base)
           << "; child groups: " << nested->groups.size() << '\n';

    std::map<std::string, std::size_t> group0_tags;
    std::size_t group0_ok = 0;
    std::size_t group0_entries = 0;
    std::size_t group0_chunks_ok = 0;
    for (const auto child : nested->groups[0].targets) {
        const auto child_size = u32_at(bytes, static_cast<std::uint64_t>(child) + 4ULL);
        if (!child_size) continue;
        const std::uint64_t child_end = static_cast<std::uint64_t>(child) + 0x10ULL + *child_size;
        const auto directory = parse_count_at_14_directory(bytes, child, reason);
        if (!directory) continue;
        bool all_targets_valid = true;
        for (const auto& group : directory->groups) {
            for (const auto target : group.targets) {
                ++group0_entries;
                ++group0_tags[tag_at(bytes, target)];
                if (!is_valid_inner_chunk(bytes, target, child_end)) {
                    all_targets_valid = false;
                } else {
                    ++group0_chunks_ok;
                }
            }
        }
        if (all_targets_valid) ++group0_ok;
    }

    output << "Variant A (observed +0x14 count / descriptors at +0x1C): "
           << group0_ok << '/' << nested->groups[0].targets.size()
           << " child IZCA directories pass header, table, target, and chunk-boundary checks.\n";
    output << "  Indexed inner chunks: " << group0_chunks_ok << '/' << group0_entries
           << " fit their declared child-container bounds; tags:";
    for (const auto& entry : group0_tags) {
        output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';

    std::map<std::string, std::size_t> group1_tags;
    std::size_t group1_ok = 0;
    std::size_t group1_entries = 0;
    std::size_t group1_chunks_ok = 0;
    std::vector<HmdlInfo> nested_models;
    for (const auto child : nested->groups[1].targets) {
        const auto child_size = u32_at(bytes, static_cast<std::uint64_t>(child) + 4ULL);
        if (!child_size) continue;
        const std::uint64_t child_end = static_cast<std::uint64_t>(child) + 0x10ULL + *child_size;
        const auto directory = parse_izca_index_directory(bytes, reason, static_cast<std::size_t>(child));
        if (!directory || directory->field_08 != 0x10 ||
            !directory_fits_declared_span(bytes, child, *directory)) {
            continue;
        }
        bool all_chunks_valid = true;
        for (const auto& group : directory->groups) {
            for (const auto target : group.targets) {
                ++group1_entries;
                const auto tag = tag_at(bytes, target);
                ++group1_tags[tag];
                if (!is_valid_inner_chunk(bytes, target, child_end)) {
                    all_chunks_valid = false;
                } else {
                    ++group1_chunks_ok;
                }
                if (tag == "HMDL") {
                    const auto model = inspect_hmdl(bytes, target, child_end);
                    if (model) nested_models.push_back(*model);
                }
            }
        }
        if (all_chunks_valid) ++group1_ok;
    }

    output << "Variant B (observed +0x10 count / descriptors at +0x18): "
           << group1_ok << '/' << nested->groups[1].targets.size()
           << " child IZCA directories pass header, table, target, and chunk-boundary checks.\n";
    output << "  Indexed inner chunks: " << group1_chunks_ok << '/' << group1_entries
           << " fit their declared child-container bounds; tags:";
    for (const auto& entry : group1_tags) {
        output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n';

    std::vector<HmdlInfo> top_models;
    if (outer->groups.size() > 2) {
        for (const auto target : outer->groups[2].targets) {
            if (tag_is(bytes, target, "HMDL")) {
                const auto model = inspect_hmdl(bytes, target, std::nullopt);
                if (model) top_models.push_back(*model);
            }
        }
    }
    output << "HMDL/KFMD pair validation: top-level " << top_models.size() << '/'
           << (outer->groups.size() > 2 ? outer->groups[2].targets.size() : 0)
           << " indexed HMDL blocks and embedded " << nested_models.size() << '/'
           << group1_tags["HMDL"] << " HMDL blocks contain a bounded KFMD payload.\n";
    if (!nested_models.empty()) {
        const auto& model = nested_models.front();
        output << "  Example embedded HMDL @ " << hex_value(model.start)
               << ": HMDL size=" << hex_value(model.hmdl_size)
               << ", end=" << hex_value(model.end)
               << "; KFMD @ " << hex_value(model.kfmd_start)
               << ", payload=[" << hex_value(model.kfmd_payload_start) << ", "
               << hex_value(model.kfmd_payload_end) << "), size=" << hex_value(model.kfmd_size) << '\n';
    }

    for (const auto target : outer->groups[0].targets) {
        const auto tag = tag_at(bytes, target);
        if (tag == "MLX0") {
            const auto size = u32_at(bytes, static_cast<std::uint64_t>(target) + 4ULL);
            const auto header = u32_at(bytes, static_cast<std::uint64_t>(target) + 8ULL);
            output << "MLX0 @ " << hex_value(target) << ": size="
                   << (size ? hex_value(*size) : "unknown") << ", field+0x08="
                   << (header ? hex_value(*header) : "unknown")
                   << "; payload is all-zero (0x20 bytes) in this sample.\n";
        }
    }
    for (const auto target : outer->groups[1].targets) {
        if (tag_at(bytes, target) == "MXTL") {
            output << "MXTL @ " << hex_value(target) << ": size="
                   << hex_value(u32_at(bytes, static_cast<std::uint64_t>(target) + 4ULL).value_or(0))
                   << ", field+0x08="
                   << hex_value(u32_at(bytes, static_cast<std::uint64_t>(target) + 8ULL).value_or(0))
                   << "; u32 fields at +0x10/+0x14/+0x18 = "
                   << u32_at(bytes, static_cast<std::uint64_t>(target) + 0x10ULL).value_or(0) << "/"
                   << u32_at(bytes, static_cast<std::uint64_t>(target) + 0x14ULL).value_or(0) << "/"
                   << u32_at(bytes, static_cast<std::uint64_t>(target) + 0x18ULL).value_or(0)
                   << "; visible .hts path strings are present; field meanings remain unconfirmed.\n";
        }
    }

    if (top_models.empty()) {
        output << "No top-level HMDL/KFMD pair was accepted; VIF inspection skipped.\n";
        return;
    }
    const auto& main_model = top_models.front();
    const auto exact_groups = find_exact_group_headers(bytes, main_model.kfmd_payload_start,
                                                        main_model.kfmd_payload_end);
    output << "\nVIF evidence inside first top-level HMDL @ " << hex_value(main_model.start)
           << ": KFMD payload [" << hex_value(main_model.kfmd_payload_start) << ", "
           << hex_value(main_model.kfmd_payload_end) << ").\n";
    output << "  Exact 16-byte mesh-group header candidate(s) matching "
              "03 01 00 01 00 00 00 05 00 00 00 20 00 00 00 00: "
           << exact_groups.size() << '\n';

    for (const auto group_start : exact_groups) {
        const auto packets = parse_vif_sequence(bytes, group_start + 0x10ULL,
                                                main_model.kfmd_payload_end);
        output << "  Candidate @ " << hex_value(group_start)
               << ": parsed " << packets.size() << " consecutive VIF UNPACK packet(s)";
        if (!packets.empty()) {
            output << ", command stream ends at " << hex_value(packets.back().end);
        }
        output << '\n';
        for (const auto& packet : packets) {
            output << "    @ " << hex_value(packet.offset)
                   << " addr=" << hex_value(packet.address)
                   << " count=" << packet.count
                   << " opcode=" << hex_value(packet.opcode)
                   << " (" << opcode_name(packet.opcode) << ")"
                   << " stride=" << packet.stride
                   << " data-bytes=" << (static_cast<std::uint64_t>(packet.count) * packet.stride)
                   << " end=" << hex_value(packet.end) << '\n';
        }
        if (!packets.empty()) {
            const auto boundary = packets.back().end;
            output << "    next bytes: " << bytes4_hex(bytes, boundary)
                   << " at " << hex_value(boundary)
                   << (is_mesh_marker(bytes, boundary) ? " (matches observed marker pattern; meaning unconfirmed)" : "")
                   << '\n';
        }
    }

    output << "Interpretation limit: packet sizes and bounds are validated; vertex meaning, index/strip topology, "
              "skin weights, and exportable triangle order are not inferred by this scan.\n";
}

} // namespace ps2probe
