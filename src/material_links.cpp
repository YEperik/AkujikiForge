#include "ps2probe/material_links.hpp"

#include "ps2probe/probe.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ps2probe {
namespace {

bool range_fits(std::size_t file_size, std::uint64_t offset, std::uint64_t length) {
    return offset <= static_cast<std::uint64_t>(file_size) &&
           length <= static_cast<std::uint64_t>(file_size) - offset;
}

std::optional<std::uint32_t> u32_at(const std::vector<std::uint8_t>& bytes,
                                    std::uint64_t offset) {
    if (!range_fits(bytes.size(), offset, 4)) {
        return std::nullopt;
    }
    bool ok = false;
    const auto value = read_u32_le(bytes, static_cast<std::size_t>(offset), ok);
    if (!ok) return std::nullopt;
    return value;
}

std::string tag_at(const std::vector<std::uint8_t>& bytes, std::uint64_t offset) {
    if (!range_fits(bytes.size(), offset, 4)) return {};
    std::string tag;
    tag.reserve(4);
    for (std::uint64_t i = 0; i < 4; ++i) {
        const auto c = bytes[static_cast<std::size_t>(offset + i)];
        if (c < 0x20U || c > 0x7eU) return {};
        tag.push_back(static_cast<char>(c));
    }
    return tag;
}

bool valid_chunk(const std::vector<std::uint8_t>& bytes,
                 std::uint64_t start,
                 std::uint64_t parent_end,
                 const std::string& expected_tag,
                 std::uint32_t expected_header_size,
                 std::uint64_t* chunk_end_out = nullptr) {
    if (tag_at(bytes, start) != expected_tag || !range_fits(bytes.size(), start, 0x10)) {
        return false;
    }
    const auto size = u32_at(bytes, start + 4ULL);
    const auto header_size = u32_at(bytes, start + 8ULL);
    if (!size || !header_size || *header_size != expected_header_size) return false;
    const std::uint64_t end = start + static_cast<std::uint64_t>(*header_size) + *size;
    if (end < start || end > parent_end || end > bytes.size()) return false;
    if (chunk_end_out != nullptr) *chunk_end_out = end;
    return true;
}

bool filename_char(std::uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '\\';
}

std::string ascii_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::optional<std::uint64_t> bundle_end(const std::vector<std::uint8_t>& bytes,
                                        std::uint64_t base) {
    if (tag_at(bytes, base) != "IZCA") return std::nullopt;
    const auto size = u32_at(bytes, base + 4ULL);
    if (!size) return std::nullopt;
    const std::uint64_t end = base + 0x10ULL + *size;
    if (end < base || end > bytes.size()) return std::nullopt;
    return end;
}

bool validate_child_directory(const std::vector<std::uint8_t>& bytes,
                              std::uint64_t child,
                              std::uint64_t child_end,
                              const IzcaIndexDirectory& directory) {
    if (directory.field_08 != 0x10U || directory.group_count != 5U) return false;
    const std::uint64_t span = child_end - child;
    const std::uint64_t descriptor_end = 0x18ULL +
        static_cast<std::uint64_t>(directory.group_count) * 8ULL;
    if (descriptor_end > span) return false;
    for (const auto& group : directory.groups) {
        const std::uint64_t table_bytes = static_cast<std::uint64_t>(group.count) * 4ULL;
        const std::uint64_t table_end = static_cast<std::uint64_t>(group.table_offset) + table_bytes;
        if (group.table_offset < descriptor_end || table_end > span) return false;
        for (const auto target : group.targets) {
            if (target < child || static_cast<std::uint64_t>(target) + 4ULL > child_end) return false;
        }
    }
    return range_fits(bytes.size(), child, span);
}

std::string fixed_hex(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}

struct ChildLink {
    std::size_t index{};
    std::uint64_t child{};
    std::uint64_t child_end{};
    std::uint64_t mle0{};
    std::uint64_t metl{};
    std::uint64_t hmdl{};
    std::uint64_t metf{};
    std::vector<std::uint64_t> htsf;
    std::uint32_t mle0_value{};
    std::vector<std::string> paths;
    std::optional<MetfTable> metf_table;
    std::uint32_t hmdl_payload_size{};
    std::uint32_t kfmd_payload_size{};
    bool slots_valid{};
    bool chunks_valid{};
    bool hmdl_pair_valid{};
    bool counts_match{};
    bool ordinals_match{};
};

} // namespace

std::optional<MetfTable> parse_metf_table(const std::vector<std::uint8_t>& bytes,
                                         std::uint64_t chunk_offset,
                                         std::uint64_t parent_end,
                                         std::string& rejection_reason) {
    rejection_reason.clear();
    if (tag_at(bytes, chunk_offset) != "METF") {
        rejection_reason = "chunk tag is not METF";
        return std::nullopt;
    }
    if (!range_fits(bytes.size(), chunk_offset, 0x10)) {
        rejection_reason = "METF header is truncated";
        return std::nullopt;
    }
    const auto payload_size = u32_at(bytes, chunk_offset + 4ULL);
    const auto header_size = u32_at(bytes, chunk_offset + 8ULL);
    if (!payload_size || !header_size || *header_size != 0x10U) {
        rejection_reason = "METF header-size field is not the observed 0x10";
        return std::nullopt;
    }
    const std::uint64_t chunk_end = chunk_offset + static_cast<std::uint64_t>(*header_size) + *payload_size;
    if (chunk_end < chunk_offset || chunk_end > parent_end || chunk_end > bytes.size()) {
        rejection_reason = "METF chunk exceeds its parent or file boundary";
        return std::nullopt;
    }
    if (*payload_size < 4U) {
        rejection_reason = "METF payload is too small for its record count";
        return std::nullopt;
    }
    const auto count_opt = u32_at(bytes, chunk_offset + 0x10ULL);
    if (!count_opt || *count_opt == 0U || *count_opt > 4096U) {
        rejection_reason = "METF record count is zero, unreadable, or above the safe limit";
        return std::nullopt;
    }
    const std::uint64_t expected_size = 4ULL + static_cast<std::uint64_t>(*count_opt) * 12ULL;
    if (expected_size != *payload_size) {
        rejection_reason = "METF payload size does not equal 4 + count * 12";
        return std::nullopt;
    }

    MetfTable table;
    table.record_count = *count_opt;
    table.payload_size = *payload_size;
    table.records.reserve(table.record_count);
    for (std::uint32_t i = 0; i < table.record_count; ++i) {
        const std::uint64_t record_offset = chunk_offset + 0x14ULL +
                                            static_cast<std::uint64_t>(i) * 12ULL;
        const auto field0 = u32_at(bytes, record_offset);
        const auto field1 = u32_at(bytes, record_offset + 4ULL);
        const auto ordinal = u32_at(bytes, record_offset + 8ULL);
        if (!field0 || !field1 || !ordinal) {
            rejection_reason = "METF record is truncated";
            return std::nullopt;
        }
        table.records.push_back(MetfRecord{*field0, *field1, *ordinal});
    }
    return table;
}

std::vector<std::string> extract_hts_paths(const std::vector<std::uint8_t>& bytes,
                                           std::uint64_t range_start,
                                           std::uint64_t range_end) {
    std::vector<std::string> paths;
    if (range_end < range_start || range_end > bytes.size()) return paths;
    std::uint64_t cursor = range_start;
    while (cursor + 4ULL <= range_end) {
        if (bytes[static_cast<std::size_t>(cursor)] != '.') {
            ++cursor;
            continue;
        }
        const auto c1 = static_cast<char>(std::tolower(bytes[static_cast<std::size_t>(cursor + 1ULL)]));
        const auto c2 = static_cast<char>(std::tolower(bytes[static_cast<std::size_t>(cursor + 2ULL)]));
        const auto c3 = static_cast<char>(std::tolower(bytes[static_cast<std::size_t>(cursor + 3ULL)]));
        if (c1 != 'h' || c2 != 't' || c3 != 's' || cursor + 4ULL >= range_end ||
            bytes[static_cast<std::size_t>(cursor + 4ULL)] != 0U) {
            ++cursor;
            continue;
        }
        std::uint64_t start = cursor;
        while (start > range_start && filename_char(bytes[static_cast<std::size_t>(start - 1ULL)])) {
            --start;
        }
        if (start < cursor) {
            std::string path;
            path.reserve(static_cast<std::size_t>(cursor + 4ULL - start));
            for (std::uint64_t p = start; p < cursor + 4ULL; ++p) {
                path.push_back(static_cast<char>(bytes[static_cast<std::size_t>(p)]));
            }
            const auto lowered = ascii_lower(path);
            if (lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".hts") == 0 &&
                (start == range_start || bytes[static_cast<std::size_t>(start - 1ULL)] == 0U)) {
                paths.push_back(std::move(path));
            }
        }
        cursor += 4ULL;
    }
    return paths;
}

void print_material_link_report(const std::vector<std::uint8_t>& bytes,
                                const std::vector<std::uint32_t>& child_offsets,
                                std::ostream& output) {
    output << "\n=== METL / MLE0 / METF / HTSF cross-correlation ===\n";
    std::vector<ChildLink> links;
    links.reserve(child_offsets.size());
    std::size_t directory_ok = 0;
    std::size_t schema_ok = 0;
    std::size_t all_chunks_ok = 0;
    std::size_t hmdl_pairs_ok = 0;
    std::size_t metf_tables_ok = 0;
    std::size_t counts_match = 0;
    std::size_t ordinal_bundle_match = 0;
    std::size_t mles_with_zero_p14 = 0;
    std::size_t mles_with_ffff_p20 = 0;
    std::size_t total_paths = 0;
    std::size_t total_records = 0;
    std::size_t total_htsf = 0;
    std::map<std::uint32_t, std::size_t> mle0_value_counts;
    std::map<std::uint32_t, std::size_t> metf_field0_counts;

    for (std::size_t index = 0; index < child_offsets.size(); ++index) {
        ChildLink item;
        item.index = index;
        item.child = child_offsets[index];
        const auto end = bundle_end(bytes, item.child);
        if (!end) continue;
        item.child_end = *end;
        std::string reason;
        const auto directory = parse_izca_index_directory(
            bytes, reason, static_cast<std::size_t>(item.child));
        if (!directory || !validate_child_directory(bytes, item.child, item.child_end, *directory)) {
            links.push_back(std::move(item));
            continue;
        }
        ++directory_ok;
        if (directory->groups[0].count != 1U || directory->groups[1].count != 1U ||
            directory->groups[2].count != 1U || directory->groups[3].count != 1U ||
            directory->groups[4].count == 0U) {
            links.push_back(std::move(item));
            continue;
        }
        item.mle0 = directory->groups[0].targets[0];
        item.metl = directory->groups[1].targets[0];
        item.hmdl = directory->groups[2].targets[0];
        item.metf = directory->groups[3].targets[0];
        for (const auto target : directory->groups[4].targets) item.htsf.push_back(target);

        item.slots_valid = tag_at(bytes, item.mle0) == "MLE0" && tag_at(bytes, item.metl) == "METL" &&
                           tag_at(bytes, item.hmdl) == "HMDL" && tag_at(bytes, item.metf) == "METF";
        for (const auto target : item.htsf) item.slots_valid = item.slots_valid && tag_at(bytes, target) == "HTSF";
        if (item.slots_valid) ++schema_ok;

        std::uint64_t mle_end = 0;
        std::uint64_t metl_end = 0;
        std::uint64_t hmdl_end = 0;
        std::uint64_t metf_end = 0;
        bool chunks_valid = item.slots_valid &&
            valid_chunk(bytes, item.mle0, item.child_end, "MLE0", 0x10U, &mle_end) &&
            valid_chunk(bytes, item.metl, item.child_end, "METL", 0x10U, &metl_end) &&
            valid_chunk(bytes, item.hmdl, item.child_end, "HMDL", 0x20U, &hmdl_end) &&
            valid_chunk(bytes, item.metf, item.child_end, "METF", 0x10U, &metf_end);
        for (const auto target : item.htsf) {
            chunks_valid = chunks_valid && valid_chunk(bytes, target, item.child_end, "HTSF", 0x10U);
        }
        item.chunks_valid = chunks_valid;
        if (chunks_valid) ++all_chunks_ok;
        if (!chunks_valid) {
            links.push_back(std::move(item));
            continue;
        }

        const auto mle0_value = u32_at(bytes, item.mle0 + 0x10ULL);
        const auto mle0_field14 = u32_at(bytes, item.mle0 + 0x14ULL);
        const auto mle0_field20 = u32_at(bytes, item.mle0 + 0x20ULL);
        if (mle0_value) {
            item.mle0_value = *mle0_value;
            ++mle0_value_counts[*mle0_value];
        }
        if (mle0_field14 && *mle0_field14 == 0U) ++mles_with_zero_p14;
        if (mle0_field20 && *mle0_field20 == 0xffffffffU) ++mles_with_ffff_p20;

        const auto metl_size = u32_at(bytes, item.metl + 4ULL).value_or(0U);
        item.paths = extract_hts_paths(bytes, item.metl + 0x10ULL,
                                       item.metl + 0x10ULL + metl_size);
        std::string metf_reason;
        item.metf_table = parse_metf_table(bytes, item.metf, item.child_end, metf_reason);
        if (item.metf_table) {
            ++metf_tables_ok;
            total_records += item.metf_table->records.size();
            bool ordinals_valid = true;
            std::optional<std::uint32_t> first_field0;
            for (std::size_t ri = 0; ri < item.metf_table->records.size(); ++ri) {
                const auto& record = item.metf_table->records[ri];
                ++metf_field0_counts[record.field0];
                if (!first_field0) first_field0 = record.field0;
                if (record.field0 != *first_field0 || record.ordinal != ri) ordinals_valid = false;
            }
            item.ordinals_match = ordinals_valid;
            if (ordinals_valid) ++ordinal_bundle_match;
        }
        total_paths += item.paths.size();
        total_htsf += item.htsf.size();
        item.counts_match = item.metf_table.has_value() &&
                            item.paths.size() == item.metf_table->records.size() &&
                            item.paths.size() == item.htsf.size();
        if (item.counts_match) ++counts_match;

        const auto hmdl_size = u32_at(bytes, item.hmdl + 4ULL);
        if (hmdl_size) item.hmdl_payload_size = *hmdl_size;
        const std::uint64_t kfmd = item.hmdl + 0x20ULL;
        const auto kfmd_size = u32_at(bytes, kfmd + 4ULL);
        const auto kfmd_header = u32_at(bytes, kfmd + 8ULL);
        if (tag_at(bytes, kfmd) == "KFMD" && kfmd_size && kfmd_header && *kfmd_header == 0x10U) {
            const std::uint64_t kfmd_end = kfmd + static_cast<std::uint64_t>(*kfmd_header) + *kfmd_size;
            if (kfmd_end >= kfmd && kfmd_end <= hmdl_end && kfmd_end <= item.child_end) {
                item.kfmd_payload_size = *kfmd_size;
                item.hmdl_pair_valid = true;
            }
        }
        if (item.hmdl_pair_valid) ++hmdl_pairs_ok;
        links.push_back(std::move(item));
    }

    output << "Observed child bundles: " << child_offsets.size() << '\n';
    output << "  child IZCA directory/span validation: " << directory_ok << '/' << child_offsets.size() << '\n';
    output << "  slot layout [MLE0, METL, HMDL, METF, HTSF*]: " << schema_ok << '/' << child_offsets.size() << '\n';
    output << "  indexed chunks fit parent boundaries: " << all_chunks_ok << '/' << child_offsets.size() << '\n';
    output << "  HMDL contains bounded KFMD payload: " << hmdl_pairs_ok << '/' << child_offsets.size() << '\n';
    output << "  METF count/record-size validation: " << metf_tables_ok << '/' << child_offsets.size() << '\n';
    output << "  METL path count = METF record count = HTSF index count: "
           << counts_match << '/' << child_offsets.size() << '\n';
    output << "  METF ordinal equals zero-based record index and field0 stays constant per bundle: "
           << ordinal_bundle_match << '/' << child_offsets.size() << '\n';
    output << "  Total extracted .hts paths / METF records / HTSF entries: "
           << total_paths << " / " << total_records << " / " << total_htsf << '\n';
    output << "  MLE0 field +0x14 is zero: " << mles_with_zero_p14 << '/' << all_chunks_ok
           << "; field +0x20 is 0xFFFFFFFF: " << mles_with_ffff_p20 << '/' << all_chunks_ok << '\n';
    output << "  MLE0 field +0x10 unique observed values: " << mle0_value_counts.size()
           << " (field meaning unconfirmed)\n";
    output << "  METF record field0 values/counts:";
    for (const auto& entry : metf_field0_counts) {
        output << ' ' << fixed_hex(entry.first) << '=' << entry.second;
    }
    output << " (semantics unconfirmed)\n";

    output << "\nPer-bundle candidate mapping (array ordinal joins METL path, METF record, and HTSF indexed entry):\n";
    for (const auto& item : links) {
        if (!item.slots_valid || !item.chunks_valid) {
            output << "  [" << std::setw(2) << std::setfill('0') << item.index << std::setfill(' ')
                   << "] child " << fixed_hex(item.child) << ": rejected/partial bundle validation\n";
            continue;
        }
        output << "  [" << std::setw(2) << std::setfill('0') << item.index << std::setfill(' ')
               << "] child=" << fixed_hex(item.child)
               << " MLE0+0x10=" << fixed_hex(item.mle0_value)
               << " METL=" << fixed_hex(item.metl)
               << " HMDL=" << fixed_hex(item.hmdl)
               << " HMDL-size=" << fixed_hex(item.hmdl_payload_size)
               << " KFMD-payload-size=" << fixed_hex(item.kfmd_payload_size)
               << " paths=" << item.paths.size()
               << " HTSF=" << item.htsf.size()
               << " match=" << (item.counts_match ? "yes" : "no") << '\n';
        if (!item.metf_table) {
            output << "      METF rejected; detailed record mapping omitted\n";
            continue;
        }
        const std::size_t record_count = item.metf_table->records.size();
        const std::size_t display_count = std::min({record_count, item.paths.size(), item.htsf.size()});
        for (std::size_t ri = 0; ri < display_count; ++ri) {
            const auto& rec = item.metf_table->records[ri];
            const auto tex_off = item.htsf[ri];
            const auto tex_size = u32_at(bytes, tex_off + 4ULL).value_or(0U);
            output << "      [" << ri << "] path=\"" << item.paths[ri] << "\""
                   << " METF=(" << fixed_hex(rec.field0) << ", " << fixed_hex(rec.field1)
                   << ", ordinal=" << rec.ordinal << ")"
                   << " HTSF@" << fixed_hex(tex_off) << " size=" << fixed_hex(tex_size) << '\n';
        }
        if (item.paths.size() != record_count || item.paths.size() != item.htsf.size()) {
            output << "      WARNING: path/record/HTSF cardinalities differ; no positional join asserted for missing entries\n";
        }
    }
    output << "Interpretation limit: METF field0/field1 and MLE0 field+0x10 are reported raw. The equal counts and ordinal pattern support a same-index association, but do not prove the semantic meaning of each record or filename.\n";
}

} // namespace ps2probe
