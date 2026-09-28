// mt-decode: read raw AIS NMEA lines on stdin, write one JSON object per
// decoded message on stdout, and print decoder statistics to stderr.
//
//   mt-decode < capture.nmea > messages.jsonl
//   mt-decode --stats-only < capture.nmea
//
// Each JSON object carries "line", the 1-based input line that completed it.

#include <exception>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

#include "maritime/ais/decoder.hpp"
#include "maritime/ais/json.hpp"

namespace {

using namespace maritime;

int run(int argc, char** argv) {
    bool stats_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--stats-only") {
            stats_only = true;
        } else {
            std::cerr << "usage: mt-decode [--stats-only] < input.nmea\n";
            return 2;
        }
    }

    std::ios::sync_with_stdio(false);
    ais::Decoder decoder;
    std::set<std::uint32_t> mmsis;
    std::string line;
    std::uint64_t line_no = 0;  // 1-based line of the sentence that completed the message
    while (std::getline(std::cin, line)) {
        ++line_no;
        if (auto dm = decoder.feed(line)) {
            mmsis.insert(ais::mmsi_of(dm->message));
            if (!stats_only) ais::write_json(std::cout, *dm, line_no);
        }
    }
    ais::write_stats(std::cerr, decoder.stats(), mmsis.size());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "mt-decode: " << e.what() << '\n';
        return 1;
    }
}
