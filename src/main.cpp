#include "ps2probe/probe.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout << "PS2 Asset Inspector 0.1.0\n"
              << "Usage: ps2probe [--verbose] <file> [file ...]\n"
              << "Read-only inventory, signature scan, entropy and validated IZCA index-table probe.\n";
}

void print_file_report(const std::filesystem::path& path, bool verbose) {
    const auto read = ps2probe::read_file(path);
    if (!read.ok) {
        std::cerr << "ERROR " << path.string() << ": " << read.error << '\n';
        return;
    }

    const auto& bytes = read.bytes;
    std::cout << "\n=== " << path.filename().string() << " ===\n"
              << "size: " << bytes.size() << " bytes (" << ps2probe::hex_value(bytes.size()) << ")\n"
              << "Shannon entropy: " << std::fixed << std::setprecision(4)
              << ps2probe::shannon_entropy(bytes) << " bits/byte\n"
              << "first 16 bytes: ";
    for (std::size_t i = 0; i < std::min<std::size_t>(16, bytes.size()); ++i) {
        std::cout << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(bytes[i]) << (i + 1 == std::min<std::size_t>(16, bytes.size()) ? "" : " ");
    }
    std::cout << std::dec << std::setfill(' ') << '\n';

    const auto hits = ps2probe::scan_signatures(bytes);
    std::map<std::string, std::vector<std::size_t>> by_signature;
    for (const auto& hit : hits) {
        by_signature[hit.signature].push_back(hit.offset);
    }
    if (by_signature.empty()) {
        std::cout << "known signatures: none found\n";
    } else {
        std::cout << "known signatures:\n";
        for (const auto& entry : by_signature) {
            std::cout << "  " << entry.first << ": " << entry.second.size() << " hit(s); offsets";
            const std::size_t max_offsets = verbose ? entry.second.size() : std::min<std::size_t>(8, entry.second.size());
            for (std::size_t i = 0; i < max_offsets; ++i) {
                std::cout << ' ' << ps2probe::hex_value(entry.second[i]);
            }
            if (max_offsets < entry.second.size()) {
                std::cout << " ...";
            }
            std::cout << '\n';
        }
    }

    std::string reason;
    const auto directory = ps2probe::parse_izca_index_directory(bytes, reason);
    if (directory) {
        std::cout << "IZCA indexed-directory candidate: accepted by bounds/overlap checks\n"
                  << "  declared-size-field: " << ps2probe::hex_value(directory->declared_size) << '\n'
                  << "  field +0x08 (unknown): " << ps2probe::hex_value(directory->field_08) << '\n'
                  << "  group count @ +0x10: " << directory->group_count << '\n';
        std::vector<std::uint32_t> all_targets;
        for (std::size_t i = 0; i < directory->groups.size(); ++i) {
            const auto& group = directory->groups[i];
            std::map<std::string, std::size_t> tag_counts;
            for (const auto target : group.targets) {
                ++tag_counts[ps2probe::printable_tag_at(bytes, target)];
                all_targets.push_back(target);
            }
            std::cout << "  group " << i << ": table=" << ps2probe::hex_value(group.table_offset)
                      << ", count=" << group.count << ", target tags=";
            bool first = true;
            for (const auto& tag : tag_counts) {
                std::cout << (first ? "" : ", ") << tag.first << ':' << tag.second;
                first = false;
            }
            std::cout << '\n';
            if (verbose) {
                for (std::size_t j = 0; j < group.targets.size(); ++j) {
                    const auto target = group.targets[j];
                    bool ok_chunk_size = false;
                    const auto chunk_size = ps2probe::read_u32_le(bytes, static_cast<std::size_t>(target) + 4, ok_chunk_size);
                    std::cout << "    [" << j << "] " << ps2probe::hex_value(target)
                              << " -> " << ps2probe::printable_tag_at(bytes, target);
                    if (ok_chunk_size) {
                        std::cout << ", size@+4=" << ps2probe::hex_value(chunk_size);
                    }
                    std::cout << '\n';
                }
            }
        }
        std::sort(all_targets.begin(), all_targets.end());
        std::optional<std::uint64_t> footer_offset;
        for (const auto& hit : hits) {
            if (hit.signature == "EOFC" && !all_targets.empty() && hit.offset > all_targets.back()) {
                footer_offset = hit.offset;
                break;
            }
        }
        std::size_t extent_matches = 0;
        for (std::size_t i = 0; i < all_targets.size(); ++i) {
            const std::uint64_t target = all_targets[i];
            bool ok_chunk_size = false;
            const auto chunk_size = ps2probe::read_u32_le(bytes, static_cast<std::size_t>(target) + 4, ok_chunk_size);
            if (!ok_chunk_size) {
                continue;
            }
            const std::uint64_t raw_end = target + 0x20ULL + chunk_size;
            if (raw_end > bytes.size() || raw_end > std::numeric_limits<std::uint64_t>::max() - 0x3fULL) {
                continue;
            }
            const std::uint64_t aligned_end = (raw_end + 0x3fULL) & ~0x3fULL;
            std::optional<std::uint64_t> expected_next;
            if (i + 1 < all_targets.size()) {
                expected_next = all_targets[i + 1];
            } else if (footer_offset) {
                expected_next = *footer_offset;
            }
            if (expected_next && aligned_end == *expected_next) {
                ++extent_matches;
            }
        }
        std::cout << "  chunk extent candidate (+0x20 header, size @ +0x04, align to 0x40): "
                  << extent_matches << '/' << all_targets.size() << " indexed chunks align to the next target/footer\n";

        for (const auto target : all_targets) {
            if (ps2probe::printable_tag_at(bytes, target) != "PECK" ||
                ps2probe::printable_tag_at(bytes, target + 0x10U) != "IZCA") {
                continue;
            }
            const std::size_t nested_base = static_cast<std::size_t>(target) + 0x10U;
            std::string nested_reason;
            const auto nested = ps2probe::parse_izca_index_directory(bytes, nested_reason, nested_base);
            if (!nested) {
                std::cout << "  embedded IZCA at " << ps2probe::hex_value(nested_base)
                          << " was not accepted as this directory variant: " << nested_reason << '\n';
                continue;
            }
            std::size_t nested_target_count = 0;
            std::size_t nested_izca_count = 0;
            std::cout << "  embedded IZCA directory @ " << ps2probe::hex_value(nested_base)
                      << ": " << nested->group_count << " groups\n";
            for (std::size_t g = 0; g < nested->groups.size(); ++g) {
                const auto& group = nested->groups[g];
                std::size_t izca_tags = 0;
                for (const auto child : group.targets) {
                    ++nested_target_count;
                    if (ps2probe::printable_tag_at(bytes, child) == "IZCA") {
                        ++izca_tags;
                        ++nested_izca_count;
                    }
                }
                std::cout << "    group " << g << ": relative table=" << ps2probe::hex_value(group.table_offset)
                          << ", count=" << group.count << ", target tags IZCA=" << izca_tags << '/' << group.count << '\n';
            }
            std::cout << "    target validation: " << nested_izca_count << '/' << nested_target_count
                      << " offsets point to IZCA signatures\n";
        }
    } else if (bytes.size() >= 4 && bytes[0] == 'I' && bytes[1] == 'Z' && bytes[2] == 'C' && bytes[3] == 'A') {
        std::cout << "IZCA signature found, but this file does not match the implemented indexed-directory variant.\n"
                  << "  reason: " << reason << '\n';
    }

    const auto bda = ps2probe::parse_bda_directory(bytes, reason);
    if (bda) {
        std::cout << "BDA directory candidate: accepted by bounds/overlap checks\n"
                  << "  entry count @ 0x00: " << bda->entry_count << "\n";
        for (const auto& entry : bda->entries) {
            std::cout << "  " << entry.name << ": offset=" << ps2probe::hex_value(entry.data_offset)
                      << ", size=" << ps2probe::hex_value(entry.data_size)
                      << ", field+0x14=" << ps2probe::hex_value(entry.field_14)
                      << ", field+0x18=" << ps2probe::hex_value(entry.field_18) << '\n';
        }
    }

    if (verbose) {
        std::cout << "printable ASCII strings (minimum length 4):\n";
        std::size_t i = 0;
        std::size_t emitted = 0;
        while (i < bytes.size() && emitted < 100) {
            while (i < bytes.size() && (bytes[i] < 0x20 || bytes[i] > 0x7e)) {
                ++i;
            }
            const std::size_t start = i;
            while (i < bytes.size() && bytes[i] >= 0x20 && bytes[i] <= 0x7e) {
                ++i;
            }
            if (i - start >= 4) {
                std::cout << "  " << ps2probe::hex_value(start) << ": ";
                for (std::size_t j = start; j < i; ++j) {
                    std::cout << static_cast<char>(bytes[j]);
                }
                std::cout << '\n';
                ++emitted;
            }
        }
        if (emitted == 100) {
            std::cout << "  ... string output capped at 100 entries\n";
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    std::vector<std::filesystem::path> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
            continue;
        }
        paths.emplace_back(arg);
    }
    if (paths.empty()) {
        print_usage();
        return 2;
    }
    for (const auto& path : paths) {
        print_file_report(path, verbose);
    }
    return 0;
}
