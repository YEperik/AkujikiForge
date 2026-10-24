#include "ps2probe/probe.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void write_u32_le(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value & 0xffU);
    bytes.at(offset + 1) = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes.at(offset + 2) = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes.at(offset + 3) = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

bool test_valid_index_directory() {
    std::vector<std::uint8_t> bytes(0x90, 0);
    bytes[0] = 'I'; bytes[1] = 'Z'; bytes[2] = 'C'; bytes[3] = 'A';
    write_u32_le(bytes, 0x04, 0x70);
    write_u32_le(bytes, 0x08, 0x10);
    write_u32_le(bytes, 0x10, 1);
    write_u32_le(bytes, 0x18, 0x20);
    write_u32_le(bytes, 0x1c, 2);
    write_u32_le(bytes, 0x20, 0x40);
    write_u32_le(bytes, 0x24, 0x80);
    bytes[0x40] = 'M'; bytes[0x41] = 'L'; bytes[0x42] = 'X'; bytes[0x43] = '0';
    bytes[0x80] = 'H'; bytes[0x81] = 'M'; bytes[0x82] = 'D'; bytes[0x83] = 'L';

    std::string reason;
    const auto directory = ps2probe::parse_izca_index_directory(bytes, reason);
    return directory.has_value() && directory->group_count == 1 &&
           directory->groups[0].count == 2 && directory->groups[0].targets[0] == 0x40 &&
           ps2probe::printable_tag_at(bytes, directory->groups[0].targets[1]) == "HMDL";
}

bool test_rejects_out_of_bounds_target() {
    std::vector<std::uint8_t> bytes(0x40, 0);
    bytes[0] = 'I'; bytes[1] = 'Z'; bytes[2] = 'C'; bytes[3] = 'A';
    write_u32_le(bytes, 0x10, 1);
    write_u32_le(bytes, 0x18, 0x20);
    write_u32_le(bytes, 0x1c, 1);
    write_u32_le(bytes, 0x20, 0xFFFFFFF0U);
    std::string reason;
    return !ps2probe::parse_izca_index_directory(bytes, reason).has_value() && !reason.empty();
}



bool test_relative_nested_izca_directory() {
    std::vector<std::uint8_t> bytes(0x120, 0);
    constexpr std::size_t base = 0x20;
    bytes[base] = 'I'; bytes[base + 1] = 'Z'; bytes[base + 2] = 'C'; bytes[base + 3] = 'A';
    write_u32_le(bytes, base + 0x04, 0xE0);
    write_u32_le(bytes, base + 0x08, 0x10);
    write_u32_le(bytes, base + 0x10, 1);
    write_u32_le(bytes, base + 0x18, 0x20);
    write_u32_le(bytes, base + 0x1c, 1);
    write_u32_le(bytes, base + 0x20, 0x80);
    bytes[base + 0x80] = 'I'; bytes[base + 0x81] = 'Z'; bytes[base + 0x82] = 'C'; bytes[base + 0x83] = 'A';
    std::string reason;
    const auto directory = ps2probe::parse_izca_index_directory(bytes, reason, base);
    return directory.has_value() && directory->groups.size() == 1 &&
           directory->groups[0].targets[0] == base + 0x80 &&
           ps2probe::printable_tag_at(bytes, directory->groups[0].targets[0]) == "IZCA";
}

bool test_valid_bda_directory() {
    std::vector<std::uint8_t> bytes(0x100, 0);
    write_u32_le(bytes, 0x00, 2);
    const char marker[] = "B Data";
    for (std::size_t i = 0; i < sizeof(marker) - 1; ++i) {
        bytes[0x10 + i] = static_cast<std::uint8_t>(marker[i]);
    }
    const char name0[] = "one.ada";
    const char name1[] = "two.ada";
    for (std::size_t i = 0; i < sizeof(name0) - 1; ++i) bytes[0x20 + i] = static_cast<std::uint8_t>(name0[i]);
    write_u32_le(bytes, 0x30, 0x60);
    write_u32_le(bytes, 0x38, 0x80000000U);
    write_u32_le(bytes, 0x3c, 0x10);
    for (std::size_t i = 0; i < sizeof(name1) - 1; ++i) bytes[0x40 + i] = static_cast<std::uint8_t>(name1[i]);
    write_u32_le(bytes, 0x50, 0x80);
    write_u32_le(bytes, 0x58, 0x80000000U);
    write_u32_le(bytes, 0x5c, 0x10);
    std::string reason;
    const auto directory = ps2probe::parse_bda_directory(bytes, reason);
    return directory.has_value() && directory->entry_count == 2 &&
           directory->entries[0].name == "one.ada" &&
           directory->entries[1].data_offset == 0x80 &&
           directory->entries[1].data_size == 0x10;
}

bool test_read_u32_bounds() {
    const std::vector<std::uint8_t> bytes{0x78, 0x56, 0x34, 0x12};
    bool ok = false;
    const auto value = ps2probe::read_u32_le(bytes, 0, ok);
    if (!ok || value != 0x12345678U) {
        return false;
    }
    (void)ps2probe::read_u32_le(bytes, 1, ok);
    return !ok;
}

} // namespace

int main() {
    const bool tests[] = {test_valid_index_directory(), test_rejects_out_of_bounds_target(), test_relative_nested_izca_directory(), test_valid_bda_directory(), test_read_u32_bounds()};
    const char* names[] = {"valid indexed directory", "out-of-bounds rejection", "relative nested IZCA directory", "valid BDA directory", "little-endian bounds"};
    bool success = true;
    for (std::size_t i = 0; i < 5; ++i) {
        std::cout << (tests[i] ? "PASS " : "FAIL ") << names[i] << '\n';
        success = success && tests[i];
    }
    return success ? 0 : 1;
}
