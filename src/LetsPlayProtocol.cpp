#include "LetsPlayProtocol.h"

std::string LetsPlayProtocol::encode(const std::vector<std::string>& chunks) {
    std::ostringstream oss;
    for (const auto& chunk : chunks) {
        oss << chunk.size();
        oss << '.';
        oss << chunk;
        oss << ',';
    }

    std::string out = oss.str();
    out.back() = ';';
    return out;
}

std::vector<std::string> LetsPlayProtocol::decode(const std::string& input) {
    std::vector<std::string> output;

    if (input.back() != ';') return output;

    std::istringstream iss{input};
    while (iss) {
        unsigned long long length{0};
        // if length is greater than -1ull then length will just be equal to
        // -1ull, no overflows here
        iss >> length;

        // TODO: Make the max received size equal to maxMessageSize (config value) multiplied by len("\u{1AAAA}") + len('4.chat,') + len(';'). Pass as a parameter to decode for keeping it static.
        if (!iss || length >= 1'000) {
            return std::vector<std::string>();
        }

        if (iss.peek() != '.') return std::vector<std::string>();

        iss.get();  // remove the period
//
        std::vector<char> content(length + 1, '\0');
        iss.read(content.data(), static_cast<std::streamsize>(length));
        output.push_back(std::string(content.data()));

        const char& separator = iss.peek();
        if (separator != ',') {
            if (separator == ';') return output;

            return std::vector<std::string>();
        }

        iss.get();
    }
    return std::vector<std::string>();
}
