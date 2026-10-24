#include "ps2probe/deep_scan.hpp"

#include "ps2probe/probe.hpp"
#include "ps2probe/material_links.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
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
    std::uint16_t immediate{};
    std::uint16_t address{};
    std::uint16_t count{};
    std::uint8_t opcode{};
    bool usn{};
    bool flg{};
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
        const std::uint16_t immediate = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[static_cast<std::size_t>(cursor)]) |
            (static_cast<std::uint16_t>(bytes[static_cast<std::size_t>(cursor + 1ULL)]) << 8U));
        const std::uint16_t address = static_cast<std::uint16_t>(immediate & 0x03ffU);
        const bool usn = (immediate & 0x4000U) != 0U;
        const bool flg = (immediate & 0x8000U) != 0U;
        const std::uint64_t payload_bytes = static_cast<std::uint64_t>(count) * *stride;
        const std::uint64_t data_start = cursor + 4ULL;
        const std::uint64_t packet_end = data_start + payload_bytes;
        if (packet_end < data_start || packet_end > limit || packet_end > bytes.size()) {
            break;
        }
        packets.push_back(VifPacket{cursor, immediate, address, count,
                                    bytes[static_cast<std::size_t>(cursor + 3ULL)],
                                    usn, flg, *stride, data_start, packet_end});
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
        const auto first = bytes[static_cast<std::size_t>(offset)];
        if ((first == 0x03U || first == 0x04U) &&
            std::equal(pattern.begin() + 1, pattern.end(),
                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + 1ULL))) {
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

bool is_separator_17(const std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    return range_fits(bytes.size(), offset, 4) &&
           bytes[static_cast<std::size_t>(offset)] == 0x00U &&
           bytes[static_cast<std::size_t>(offset + 1ULL)] == 0x00U &&
           bytes[static_cast<std::size_t>(offset + 2ULL)] == 0x00U &&
           bytes[static_cast<std::size_t>(offset + 3ULL)] == 0x17U;
}

std::optional<float> f32_le_at(const std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    const auto bits = u32_at(bytes, offset);
    if (!bits) return std::nullopt;
    float value{};
    const std::uint32_t raw = *bits;
    std::memcpy(&value, &raw, sizeof(value));
    if (!std::isfinite(value)) return std::nullopt;
    return value;
}

struct VifChainEvidence {
    std::size_t model_index{};
    std::uint64_t separator_offset{};
    std::uint64_t command_start{};
    std::uint64_t chain_end{};
    std::uint64_t terminal_marker{};
    std::string terminal_kind;
    std::vector<VifPacket> packets;
};

std::vector<VifChainEvidence> scan_vif_chains(
    const std::vector<std::uint8_t>& bytes,
    const std::vector<HmdlInfo>& models,
    std::size_t& raw_separator_hits,
    std::size_t& candidate_starts) {
    std::vector<VifChainEvidence> candidates;
    raw_separator_hits = 0;
    candidate_starts = 0;

    for (std::size_t model_index = 0; model_index < models.size(); ++model_index) {
        const auto& model = models[model_index];
        for (std::uint64_t offset = model.kfmd_payload_start;
             offset + 4ULL <= model.kfmd_payload_end; ++offset) {
            if (!is_separator_17(bytes, offset)) continue;
            ++raw_separator_hits;

            const std::uint64_t command = offset + 4ULL;
            if (!range_fits(bytes.size(), command, 4) || command + 4ULL > model.kfmd_payload_end ||
                bytes[static_cast<std::size_t>(command + 1ULL)] != 0xc0U ||
                !vif_stride(bytes[static_cast<std::size_t>(command + 3ULL)])) {
                continue;
            }
            ++candidate_starts;
            auto packets = parse_vif_sequence(bytes, command, model.kfmd_payload_end);
            if (packets.size() < 3) continue;

            const std::uint64_t end = packets.back().end;
            std::string terminal_kind;
            if (is_separator_17(bytes, end)) {
                terminal_kind = "00 00 00 17";
            } else if (is_mesh_marker(bytes, end)) {
                terminal_kind = bytes4_hex(bytes, end);
            } else {
                continue;
            }
            candidates.push_back(VifChainEvidence{model_index, offset, command, end,
                                                   end, terminal_kind, std::move(packets)});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const VifChainEvidence& left,
                                                       const VifChainEvidence& right) {
        if (left.model_index != right.model_index) return left.model_index < right.model_index;
        return left.separator_offset < right.separator_offset;
    });

    std::vector<VifChainEvidence> selected;
    std::map<std::size_t, std::uint64_t> last_chain_end;
    selected.reserve(candidates.size());
    for (auto& candidate : candidates) {
        const auto previous = last_chain_end.find(candidate.model_index);
        // A candidate marker inside an already accepted packet chain is a byte-pattern
        // coincidence, not a second chain. Equality is permitted for adjacent chains.
        if (previous != last_chain_end.end() && candidate.separator_offset < previous->second) {
            continue;
        }
        last_chain_end[candidate.model_index] = candidate.chain_end;
        selected.push_back(std::move(candidate));
    }
    return selected;
}

struct VifAttributeStats {
    std::size_t first_vectors{};
    std::size_t first_finite_vectors{};
    std::size_t first_unit_vectors{};
    double first_min_length{std::numeric_limits<double>::infinity()};
    double first_max_length{0.0};
    std::vector<double> second_lengths;
    std::size_t uv_scalar_count{};
    std::size_t uv_in_unit_range{};
    std::size_t uv_non_finite{};
    std::size_t tail_word_count{};
    std::size_t tail_bit_0008{};
    std::size_t tail_bit_0010{};
    std::size_t tail_bit_2000{};
    std::size_t tail_bit_4000{};
    std::size_t tail_bit_8000{};
    std::size_t group_start_bit_4000{};
    std::size_t group_end_bit_8000{};
    std::size_t group_end_bit_2000{};
    std::map<std::uint32_t, std::size_t> tail_word_values;
};

void accumulate_vif_attribute_stats(const std::vector<std::uint8_t>& bytes,
                                    const std::vector<VifChainEvidence>& chains,
                                    std::size_t& matched_chains,
                                    std::size_t& triplet_groups,
                                    std::size_t& quad_groups,
                                    std::size_t& unmatched_chains,
                                    std::size_t& count_mismatch_groups,
                                    std::size_t& packet_count,
                                    std::map<std::uint8_t, std::size_t>& opcode_counts,
                                    std::map<std::string, std::size_t>& terminal_counts,
                                    VifAttributeStats& stats) {
    for (const auto& chain : chains) {
        terminal_counts[chain.terminal_kind]++;
        for (const auto& packet : chain.packets) {
            ++packet_count;
            ++opcode_counts[packet.opcode];
        }

        std::vector<std::pair<std::size_t, std::size_t>> chunks;
        bool pattern_matches = true;
        std::size_t cursor = 0;
        while (cursor < chain.packets.size()) {
            const auto remaining = chain.packets.size() - cursor;
            if (remaining >= 4 &&
                chain.packets[cursor].opcode == 0x6c &&
                chain.packets[cursor + 1].opcode == 0x6c &&
                chain.packets[cursor + 2].opcode == 0x68 &&
                chain.packets[cursor + 3].opcode == 0x64) {
                chunks.emplace_back(cursor, 4);
                cursor += 4;
            } else if (remaining >= 3 &&
                       chain.packets[cursor].opcode == 0x6c &&
                       chain.packets[cursor + 1].opcode == 0x6c &&
                       chain.packets[cursor + 2].opcode == 0x64) {
                chunks.emplace_back(cursor, 3);
                cursor += 3;
            } else {
                pattern_matches = false;
                break;
            }
        }

        if (!pattern_matches) {
            ++unmatched_chains;
            continue;
        }

        std::size_t mismatched_groups_in_chain = 0;
        for (const auto& chunk : chunks) {
            const auto start = chunk.first;
            const auto length = chunk.second;
            const auto count = chain.packets[start].count;
            bool group_counts_match = true;
            for (std::size_t i = 1; i < length; ++i) {
                if (chain.packets[start + i].count != count) group_counts_match = false;
            }
            if (!group_counts_match) ++mismatched_groups_in_chain;
        }
        if (mismatched_groups_in_chain != 0) {
            count_mismatch_groups += mismatched_groups_in_chain;
            continue;
        }
        ++matched_chains;
        for (const auto& chunk : chunks) {
            if (chunk.second == 3) ++triplet_groups;
            else if (chunk.second == 4) ++quad_groups;
        }

        for (const auto& chunk : chunks) {
            const std::size_t start = chunk.first;
            const auto& first = chain.packets[start];
            const auto& second = chain.packets[start + 1U];
            const auto& uv = chain.packets[start + chunk.second - 1U];
            const std::uint64_t first_tail_offset = first.data_start + 12ULL;
            const std::uint64_t last_tail_offset = first.data_start +
                static_cast<std::uint64_t>(first.count - 1U) * 16ULL + 12ULL;
            const auto first_tail_word = u32_at(bytes, first_tail_offset);
            const auto last_tail_word = u32_at(bytes, last_tail_offset);
            if (first_tail_word && ((*first_tail_word & 0x4000U) != 0U)) ++stats.group_start_bit_4000;
            if (last_tail_word && ((*last_tail_word & 0x8000U) != 0U)) ++stats.group_end_bit_8000;
            if (last_tail_word && ((*last_tail_word & 0x2000U) != 0U)) ++stats.group_end_bit_2000;
            for (std::uint16_t i = 0; i < first.count; ++i) {
                const auto tail_word = u32_at(bytes, first.data_start + static_cast<std::uint64_t>(i) * 16ULL + 12ULL);
                if (tail_word) {
                    ++stats.tail_word_count;
                    ++stats.tail_word_values[*tail_word];
                    if ((*tail_word & 0x0008U) != 0U) ++stats.tail_bit_0008;
                    if ((*tail_word & 0x0010U) != 0U) ++stats.tail_bit_0010;
                    if ((*tail_word & 0x2000U) != 0U) ++stats.tail_bit_2000;
                    if ((*tail_word & 0x4000U) != 0U) ++stats.tail_bit_4000;
                    if ((*tail_word & 0x8000U) != 0U) ++stats.tail_bit_8000;
                }
                const std::uint64_t first_element = first.data_start + static_cast<std::uint64_t>(i) * 16ULL;
                const auto x = f32_le_at(bytes, first_element);
                const auto y = f32_le_at(bytes, first_element + 4ULL);
                const auto z = f32_le_at(bytes, first_element + 8ULL);
                if (x && y && z) {
                    const double length = std::sqrt(static_cast<double>(*x) * *x +
                                                    static_cast<double>(*y) * *y +
                                                    static_cast<double>(*z) * *z);
                    ++stats.first_finite_vectors;
                    stats.first_min_length = std::min(stats.first_min_length, length);
                    stats.first_max_length = std::max(stats.first_max_length, length);
                    if (std::abs(length - 1.0) <= 1.0e-6) ++stats.first_unit_vectors;
                }
                ++stats.first_vectors;

                const std::uint64_t second_element = second.data_start + static_cast<std::uint64_t>(i) * 16ULL;
                const auto sx = f32_le_at(bytes, second_element);
                const auto sy = f32_le_at(bytes, second_element + 4ULL);
                const auto sz = f32_le_at(bytes, second_element + 8ULL);
                if (sx && sy && sz) {
                    const double length = std::sqrt(static_cast<double>(*sx) * *sx +
                                                    static_cast<double>(*sy) * *sy +
                                                    static_cast<double>(*sz) * *sz);
                    stats.second_lengths.push_back(length);
                }

                const std::uint64_t uv_element = uv.data_start + static_cast<std::uint64_t>(i) * 8ULL;
                for (std::uint64_t component = 0; component < 2; ++component) {
                    const auto value = f32_le_at(bytes, uv_element + component * 4ULL);
                    ++stats.uv_scalar_count;
                    if (!value) {
                        ++stats.uv_non_finite;
                    } else if (*value >= 0.0F && *value <= 1.0F) {
                        ++stats.uv_in_unit_range;
                    }
                }
            }
        }
    }
}

void print_kfmd_header_report(const std::vector<std::uint8_t>& bytes,
                              const std::vector<HmdlInfo>& models,
                              std::ostream& output) {
    output << "\n=== KFMD payload-header comparison (raw field offsets) ===\n";
    output << "  Bounded top-level payloads with at least 0x50 bytes: ";
    std::size_t eligible = 0;
    std::map<std::uint32_t, std::size_t> word00_values;
    std::map<std::uint32_t, std::size_t> word08_values;
    std::size_t word04_is_50 = 0;
    for (const auto& model : models) {
        if (model.kfmd_payload_end < model.kfmd_payload_start ||
            model.kfmd_payload_end - model.kfmd_payload_start < 0x50ULL ||
            !range_fits(bytes.size(), model.kfmd_payload_start, 0x50ULL)) {
            continue;
        }
        ++eligible;
        ++word00_values[u32_at(bytes, model.kfmd_payload_start).value_or(0)];
        ++word08_values[u32_at(bytes, model.kfmd_payload_start + 0x08ULL).value_or(0)];
        if (u32_at(bytes, model.kfmd_payload_start + 0x04ULL).value_or(0) == 0x50U) {
            ++word04_is_50;
        }
    }
    output << eligible << '/' << models.size() << "; word +0x04 equals 0x50 in "
           << word04_is_50 << '/' << eligible << ".\n";
    output << "  word +0x00 values:";
    for (const auto& item : word00_values) output << ' ' << hex_value(item.first) << '=' << item.second;
    output << "; word +0x08 values:";
    for (const auto& item : word08_values) output << ' ' << hex_value(item.first) << '=' << item.second;
    output << "\n";
    output << "  Fields are printed raw; this comparison does not assign semantic names to unknown words.\n";

    for (std::size_t index = 0; index < models.size(); ++index) {
        const auto& model = models[index];
        if (model.kfmd_payload_end < model.kfmd_payload_start ||
            model.kfmd_payload_end - model.kfmd_payload_start < 0x50ULL ||
            !range_fits(bytes.size(), model.kfmd_payload_start, 0x50ULL)) {
            continue;
        }
        const auto value_at = [&](std::uint64_t relative) {
            return u32_at(bytes, model.kfmd_payload_start + relative).value_or(0);
        };
        output << "  model " << index << " HMDL=" << hex_value(model.start)
               << " payload-size=" << hex_value(model.kfmd_payload_end - model.kfmd_payload_start)
               << " words[00,04,08,0C,10,14,18,1C,44,48]="
               << hex_value(value_at(0x00)) << ',' << hex_value(value_at(0x04)) << ','
               << hex_value(value_at(0x08)) << ',' << hex_value(value_at(0x0c)) << ','
               << hex_value(value_at(0x10)) << ',' << hex_value(value_at(0x14)) << ','
               << hex_value(value_at(0x18)) << ',' << hex_value(value_at(0x1c)) << ','
               << hex_value(value_at(0x44)) << ',' << hex_value(value_at(0x48)) << '\n';

        const std::uint64_t payload_size = model.kfmd_payload_end - model.kfmd_payload_start;
        const std::uint64_t candidate_start = value_at(0x1c);
        const std::uint64_t candidate_length = value_at(0x08);
        if (candidate_start != 0 && candidate_start <= payload_size &&
            candidate_length == payload_size - candidate_start && candidate_length >= 4ULL) {
            const auto first = u32_at(bytes, model.kfmd_payload_start + candidate_start);
            std::uint64_t run = first ? 1ULL : 0ULL;
            std::uint32_t previous = first.value_or(0);
            for (std::uint64_t cursor = 4ULL; first && cursor + 4ULL <= candidate_length; cursor += 4ULL) {
                const auto current = u32_at(bytes, model.kfmd_payload_start + candidate_start + cursor);
                if (!current || previous == std::numeric_limits<std::uint32_t>::max() ||
                    *current != previous + 1U) break;
                previous = *current;
                ++run;
            }
            output << "    Candidate subrange: rel=" << hex_value(candidate_start)
                   << ", length=" << hex_value(candidate_length)
                   << " reaches payload end; word +0x18=" << hex_value(value_at(0x18))
                   << (value_at(0x18) == candidate_start + 4ULL ? " (= start+4)" : " (not start+4)")
                   << "; initial +1 integer run=" << run << " word(s)."
                   << " Range/sequence correlation is observed; semantic role is unknown.\n";
        }
    }
}

void print_vif_chain_report(const std::vector<std::uint8_t>& bytes,
                            const std::vector<HmdlInfo>& models,
                            std::ostream& output) {
    std::size_t raw_hits = 0;
    std::size_t candidate_starts = 0;
    const auto chains = scan_vif_chains(bytes, models, raw_hits, candidate_starts);
    std::size_t matched_chains = 0;
    std::size_t triplet_groups = 0;
    std::size_t quad_groups = 0;
    std::size_t unmatched_chains = 0;
    std::size_t count_mismatch_groups = 0;
    std::size_t packet_count = 0;
    std::map<std::uint8_t, std::size_t> opcode_counts;
    std::map<std::string, std::size_t> terminal_counts;
    VifAttributeStats stats;
    accumulate_vif_attribute_stats(bytes, chains, matched_chains, triplet_groups,
                                   quad_groups, unmatched_chains, count_mismatch_groups,
                                   packet_count, opcode_counts, terminal_counts, stats);

    std::sort(stats.second_lengths.begin(), stats.second_lengths.end());
    double second_median = 0.0;
    if (!stats.second_lengths.empty()) {
        const auto mid = stats.second_lengths.size() / 2U;
        if (stats.second_lengths.size() % 2U == 0U) {
            second_median = (stats.second_lengths[mid - 1U] + stats.second_lengths[mid]) / 2.0;
        } else {
            second_median = stats.second_lengths[mid];
        }
    }

    print_kfmd_header_report(bytes, models, output);
    output << "\n=== VIF chain evidence across top-level HMDL payloads ===\n";
    output << "  Bounded HMDL/KFMD payloads scanned: " << models.size() << '\n';
    output << "  Raw `00 00 00 17` byte-pattern hits: " << raw_hits
           << " (includes coincidences inside payload data; not used alone as a boundary).\n";
    output << "  Hits immediately followed by a supported VIF UNPACK command: " << candidate_starts << '\n';
    output << "  Non-overlapping chains with >=3 bounded packets ending on a recognized marker: "
           << chains.size() << '\n';
    output << "  Chain terminators:";
    for (const auto& entry : terminal_counts) {
        output << ' ' << entry.first << '=' << entry.second;
    }
    output << '\n' << "  Bounded packet commands: " << packet_count << "; opcode counts:";
    for (const auto& entry : opcode_counts) {
        output << ' ' << hex_value(entry.first) << '=' << entry.second;
    }
    output << '\n' << "  Repeating attribute-like groups: [6C,6C,64]=" << triplet_groups
           << ", [6C,6C,68,64]=" << quad_groups
           << "; fully matching chains=" << matched_chains << '/' << chains.size()
           << "; unmatched chains=" << unmatched_chains
           << "; group element-count mismatches=" << count_mismatch_groups << '\n';
    if (stats.first_vectors != 0) {
        output << "  First V4-32 lane XYZ length: " << stats.first_unit_vectors << '/'
               << stats.first_finite_vectors << " vectors are within 1e-6 of unit length; range="
               << std::setprecision(9) << stats.first_min_length << ".." << stats.first_max_length << '\n';
    }
    if (!stats.second_lengths.empty()) {
        output << "  Second V4-32 lane XYZ magnitude: vectors=" << stats.second_lengths.size()
               << ", min=" << stats.second_lengths.front() << ", median=" << second_median
               << ", max=" << stats.second_lengths.back() << '\n';
    }
    output << "  V2-32 lane: values in [0,1]=" << stats.uv_in_unit_range << '/'
           << stats.uv_scalar_count << ", non-finite=" << stats.uv_non_finite << '\n';
    output << "  First V4-32 trailing-word field: samples=" << stats.tail_word_count
           << ", distinct values=" << stats.tail_word_values.size()
           << ", bit 0x0008=" << stats.tail_bit_0008 << '/' << stats.tail_word_count
           << ", bit 0x0010=" << stats.tail_bit_0010 << '/' << stats.tail_word_count
           << ", bit 0x4000=" << stats.tail_bit_4000 << '/' << stats.tail_word_count
           << ", bit 0x8000=" << stats.tail_bit_8000 << '/' << stats.tail_word_count << '\n';
    output << "  Across matched pattern groups: first-element bit 0x4000 set="
           << stats.group_start_bit_4000 << '/' << (triplet_groups + quad_groups)
           << "; last-element bit 0x8000 set=" << stats.group_end_bit_8000 << '/' << (triplet_groups + quad_groups)
           << "; last-element bit 0x2000 set=" << stats.group_end_bit_2000 << '/' << (triplet_groups + quad_groups)
           << ". Boundary-marker interpretation remains a hypothesis.\n";
    output << "  Attribute hypotheses only: first V4-32 lane has unit-length XYZ values; second V4-32 lane has variable-magnitude XYZ values; "
              "V2-32 values fall in [0,1]. These statistics do not prove normals, positions, or UV semantics; V3-32 meaning remains unknown.\n";
}

std::vector<HmdlInfo> find_top_level_models(const std::vector<std::uint8_t>& bytes) {
    std::vector<HmdlInfo> models;
    std::string reason;
    const auto outer = parse_izca_index_directory(bytes, reason, 0);
    if (!outer || outer->groups.size() <= 2) return models;
    for (const auto target : outer->groups[2].targets) {
        if (!tag_is(bytes, target, "HMDL")) continue;
        const auto model = inspect_hmdl(bytes, target, std::nullopt);
        if (model) models.push_back(*model);
    }
    return models;
}

std::string terminal_hex(const std::string& terminal_kind) {
    std::string result;
    for (const unsigned char c : terminal_kind) {
        if (c != ' ') result.push_back(static_cast<char>(c));
    }
    return result;
}

} // namespace

void write_vif_chain_csv(const std::vector<std::uint8_t>& bytes, std::ostream& output) {
    output << "model_index,hmdl_offset,kfmd_payload_start,kfmd_payload_end,separator_offset,command_start,"
              "packet_count,chain_end,terminal_marker,opcode_sequence,count_sequence,attribute_pattern_sequence,"
              "pattern_matches,element_counts_match\n";
    const auto models = find_top_level_models(bytes);
    if (models.empty()) return;

    std::size_t raw_hits = 0;
    std::size_t candidate_starts = 0;
    const auto chains = scan_vif_chains(bytes, models, raw_hits, candidate_starts);
    for (const auto& chain : chains) {
        const auto& model = models[chain.model_index];
        output << chain.model_index << ',' << hex_value(model.start) << ','
               << hex_value(model.kfmd_payload_start) << ',' << hex_value(model.kfmd_payload_end) << ','
               << hex_value(chain.separator_offset) << ',' << hex_value(chain.command_start) << ','
               << chain.packets.size() << ',' << hex_value(chain.chain_end) << ','
               << terminal_hex(chain.terminal_kind) << ',';

        for (std::size_t i = 0; i < chain.packets.size(); ++i) {
            if (i) output << '|';
            output << hex_value(chain.packets[i].opcode);
        }
        output << ',';
        for (std::size_t i = 0; i < chain.packets.size(); ++i) {
            if (i) output << '|';
            output << chain.packets[i].count;
        }

        std::vector<std::pair<std::size_t, std::size_t>> groups;
        bool pattern_matches = true;
        std::size_t cursor = 0;
        while (cursor < chain.packets.size()) {
            const auto remaining = chain.packets.size() - cursor;
            if (remaining >= 4 && chain.packets[cursor].opcode == 0x6c &&
                chain.packets[cursor + 1].opcode == 0x6c &&
                chain.packets[cursor + 2].opcode == 0x68 &&
                chain.packets[cursor + 3].opcode == 0x64) {
                groups.emplace_back(cursor, 4);
                cursor += 4;
            } else if (remaining >= 3 && chain.packets[cursor].opcode == 0x6c &&
                       chain.packets[cursor + 1].opcode == 0x6c &&
                       chain.packets[cursor + 2].opcode == 0x64) {
                groups.emplace_back(cursor, 3);
                cursor += 3;
            } else {
                pattern_matches = false;
                break;
            }
        }
        bool counts_match = pattern_matches;
        if (pattern_matches) {
            for (const auto& group : groups) {
                const auto count = chain.packets[group.first].count;
                for (std::size_t i = 1; i < group.second; ++i) {
                    if (chain.packets[group.first + i].count != count) counts_match = false;
                }
            }
        } else {
            counts_match = false;
        }
        output << ',';
        if (!pattern_matches) {
            output << "unmatched";
        } else {
            for (std::size_t i = 0; i < groups.size(); ++i) {
                if (i) output << '|';
                output << (groups[i].second == 3 ? 'T' : 'Q');
            }
        }
        output << ',' << (pattern_matches ? "true" : "false")
               << ',' << (counts_match ? "true" : "false") << '\n';
    }
}

void write_vif_group_csv(const std::vector<std::uint8_t>& bytes, std::ostream& output) {
    output << "model_index,hmdl_offset,kfmd_payload_start,kfmd_payload_end,chain_separator,command_start,"
              "pattern_group_index,pattern,element_count,first_v4_payload_offset,second_v4_payload_offset,"
              "v3_payload_offset,v2_payload_offset,first_tail_word,last_tail_word,first_tail_bit_4000,"
              "last_tail_bit_8000,last_tail_bit_2000\n";
    const auto models = find_top_level_models(bytes);
    if (models.empty()) return;

    std::size_t raw_hits = 0;
    std::size_t candidate_starts = 0;
    const auto chains = scan_vif_chains(bytes, models, raw_hits, candidate_starts);
    for (const auto& chain : chains) {
        std::vector<std::pair<std::size_t, std::size_t>> groups;
        bool pattern_matches = true;
        std::size_t cursor = 0;
        while (cursor < chain.packets.size()) {
            const auto remaining = chain.packets.size() - cursor;
            if (remaining >= 4 && chain.packets[cursor].opcode == 0x6c &&
                chain.packets[cursor + 1].opcode == 0x6c &&
                chain.packets[cursor + 2].opcode == 0x68 &&
                chain.packets[cursor + 3].opcode == 0x64) {
                groups.emplace_back(cursor, 4);
                cursor += 4;
            } else if (remaining >= 3 && chain.packets[cursor].opcode == 0x6c &&
                       chain.packets[cursor + 1].opcode == 0x6c &&
                       chain.packets[cursor + 2].opcode == 0x64) {
                groups.emplace_back(cursor, 3);
                cursor += 3;
            } else {
                pattern_matches = false;
                break;
            }
        }
        if (!pattern_matches) continue;

        bool counts_match = true;
        for (const auto& group : groups) {
            const auto count = chain.packets[group.first].count;
            for (std::size_t i = 1; i < group.second; ++i) {
                if (chain.packets[group.first + i].count != count) counts_match = false;
            }
        }
        if (!counts_match) continue;

        const auto& model = models[chain.model_index];
        for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
            const auto [start, length] = groups[group_index];
            const auto& first = chain.packets[start];
            const auto& second = chain.packets[start + 1U];
            const auto& uv = chain.packets[start + length - 1U];
            const auto first_tail = u32_at(bytes, first.data_start + 12ULL).value_or(0);
            const auto last_tail = u32_at(bytes, first.data_start +
                static_cast<std::uint64_t>(first.count - 1U) * 16ULL + 12ULL).value_or(0);
            output << chain.model_index << ',' << hex_value(model.start) << ','
                   << hex_value(model.kfmd_payload_start) << ',' << hex_value(model.kfmd_payload_end) << ','
                   << hex_value(chain.separator_offset) << ',' << hex_value(chain.command_start) << ','
                   << group_index << ',' << (length == 3 ? 'T' : 'Q') << ',' << first.count << ','
                   << hex_value(first.data_start) << ',' << hex_value(second.data_start) << ',';
            if (length == 4) output << hex_value(chain.packets[start + 2U].data_start);
            output << ',' << hex_value(uv.data_start) << ',' << hex_value(first_tail) << ',' << hex_value(last_tail)
                   << ',' << ((first_tail & 0x4000U) != 0U ? "true" : "false")
                   << ',' << ((last_tail & 0x8000U) != 0U ? "true" : "false")
                   << ',' << ((last_tail & 0x2000U) != 0U ? "true" : "false") << '\n';
        }
    }
}

void write_kfmd_header_csv(const std::vector<std::uint8_t>& bytes, std::ostream& output) {
    output << "model_index,hmdl_offset,hmdl_size,kfmd_offset,kfmd_size,payload_start,payload_end";
    for (std::uint64_t offset = 0; offset < 0x50ULL; offset += 4ULL) {
        output << ",payload_word_" << std::uppercase << std::hex << std::setw(2)
               << std::setfill('0') << offset << std::dec << std::setfill(' ');
    }
    output << '\n';

    const auto models = find_top_level_models(bytes);
    for (std::size_t i = 0; i < models.size(); ++i) {
        const auto& model = models[i];
        if (model.kfmd_payload_end < model.kfmd_payload_start ||
            model.kfmd_payload_end - model.kfmd_payload_start < 0x50ULL ||
            !range_fits(bytes.size(), model.kfmd_payload_start, 0x50ULL)) {
            continue;
        }
        output << i << ',' << hex_value(model.start) << ',' << hex_value(model.hmdl_size) << ','
               << hex_value(model.kfmd_start) << ',' << hex_value(model.kfmd_size) << ','
               << hex_value(model.kfmd_payload_start) << ',' << hex_value(model.kfmd_payload_end);
        for (std::uint64_t offset = 0; offset < 0x50ULL; offset += 4ULL) {
            const auto value = u32_at(bytes, model.kfmd_payload_start + offset);
            output << ',' << (value ? hex_value(*value) : std::string("<out-of-bounds>"));
        }
        output << '\n';
    }
}

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

    print_material_link_report(bytes, nested->groups[1].targets, output);

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
    output << "  Exact 16-byte mesh-group header candidate(s) with first marker 03/04 and remaining bytes "
              "01 00 01 00 00 00 05 00 00 00 20 00 00 00 00: "
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
                   << " imm=" << hex_value(packet.immediate)
                   << " addr=" << hex_value(packet.address)
                   << " usn=" << (packet.usn ? 1 : 0)
                   << " flg=" << (packet.flg ? 1 : 0)
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

    print_vif_chain_report(bytes, top_models, output);

    output << "Interpretation limit: packet sizes and bounds are validated; index/strip topology, "
              "skin weights, and final triangle order are not inferred by this scan.\n";
}

} // namespace ps2probe
