#include "akuijikiforge/inspector.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
void usage() {
    std::cout << "AkuijikiForge CLI 1.0\n"
              << "Read-only file inventory for locally supplied assets.\n\n"
              << "Usage: akuijikiforge [--verbose] <file> [file ...]\n\n"
              << "Options:\n"
              << "  --verbose  show a longer hexadecimal header preview\n"
              << "  --help     show this help message\n\n"
              << "The inspector reports basic file metadata and common file signatures.\n"
              << "It does not modify input files or attempt to bypass software protections.\n";
}

void report_file(const std::filesystem::path& path, bool verbose) {
    const auto result = akuijikiforge::read_file(path);
    if (!result.ok) {
        std::cerr << "ERROR " << path.string() << ": " << result.error << '\n';
        return;
    }

    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    std::cout << "\n" << path.filename().string() << '\n'
              << std::string(path.filename().string().size(), '=') << '\n'
              << "Size: " << size << " bytes\n"
              << "Shannon entropy: " << std::fixed << std::setprecision(4)
              << akuijikiforge::shannon_entropy(result.bytes) << " bits/byte\n"
              << "Header (hex): " << akuijikiforge::hex_preview(result.bytes, verbose ? 64U : 16U) << '\n';

    const auto matches = akuijikiforge::detect_signatures(result.bytes);
    if (matches.empty()) {
        std::cout << "Recognized file signature: none of the common signatures checked\n";
    } else {
        std::cout << "Recognized file signature(s):\n";
        for (const auto& match : matches) std::cout << "  - " << match.label << '\n';
    }
    std::cout << "Input status: read-only inspection complete\n";
}
} // namespace

int main(int argc, char** argv) {
    bool verbose = false;
    std::vector<std::filesystem::path> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            usage();
            return 0;
        }
        if (argument == "--verbose" || argument == "-v") {
            verbose = true;
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "Unknown option: " << argument << "\n\n";
            usage();
            return 2;
        }
        paths.emplace_back(argument);
    }

    if (paths.empty()) {
        usage();
        return argc == 1 ? 0 : 2;
    }

    bool failed = false;
    for (const auto& path : paths) {
        if (!std::filesystem::is_regular_file(path)) {
            std::cerr << "ERROR " << path.string() << ": not a regular file\n";
            failed = true;
            continue;
        }
        report_file(path, verbose);
    }
    return failed ? 1 : 0;
}
