#pragma once

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace ps2probe {

// Prints evidence-led analysis of the observed PLAYER00.MLD layout.
// The routine is read-only and reports candidate VIF streams without exporting geometry.
void print_deep_mld_report(const std::vector<std::uint8_t>& bytes, std::ostream& output);

// Exports bounded, non-overlapping candidate VIF chains as CSV; no asset data is modified.
// Input that does not contain the observed top-level IZCA/HMDL layout yields a header-only CSV.
void write_vif_chain_csv(const std::vector<std::uint8_t>& bytes, std::ostream& output);

// Exports each fully matched recurring VIF attribute-like group and the first lane's trailing word.
// Group boundaries are inferred from the command pattern and are not asserted to be mesh boundaries.
void write_vif_group_csv(const std::vector<std::uint8_t>& bytes, std::ostream& output);

// Exports the first 0x50 payload bytes of each bounded top-level KFMD as raw little-endian words.
// Fields are deliberately unlabeled semantically; this is for comparative header analysis.
void write_kfmd_header_csv(const std::vector<std::uint8_t>& bytes, std::ostream& output);

} // namespace ps2probe
