#pragma once

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace ps2probe {

// Prints evidence-led analysis of the observed PLAYER00.MLD layout.
// The routine is read-only and reports candidate VIF streams without exporting geometry.
void print_deep_mld_report(const std::vector<std::uint8_t>& bytes, std::ostream& output);

} // namespace ps2probe
