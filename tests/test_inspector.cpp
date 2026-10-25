#include "akuijikiforge/inspector.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    using akuijikiforge::detect_signatures;
    using akuijikiforge::hex_preview;
    using akuijikiforge::shannon_entropy;

    const std::vector<std::uint8_t> empty;
    require(shannon_entropy(empty) == 0.0, "empty input entropy should be zero");
    const std::vector<std::uint8_t> repeated(128, 0xAA);
    require(std::abs(shannon_entropy(repeated)) < 1e-12, "constant input entropy should be zero");
    const std::vector<std::uint8_t> balanced{0, 1, 0, 1};
    require(std::abs(shannon_entropy(balanced) - 1.0) < 1e-12, "balanced binary entropy should be one bit");

    const std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00};
    const auto found = detect_signatures(png);
    require(found.size() == 1 && found.front().label == "PNG image", "PNG header should be recognized");
    require(hex_preview(png, 4) == "89 50 4E 47 …", "hex preview should be bounded and formatted");
    require(detect_signatures({0x00, 0x01, 0x02}).empty(), "unknown bytes should not match known signatures");
    require(detect_signatures({0x7F, 'E', 'L', 'F'}).front().label == "ELF executable", "ELF signature should be recognized");

    std::cout << "All AkuijikiForge inspector tests passed.\n";
    return 0;
}
