#pragma once

// Shared key=value file helpers for game/bot configs. Internal, but lives in
// include/ so every config parser uses identical rules (trimming, CRLF,
// strict integers, quoted strings). Tested indirectly via those parsers.
#include <cctype>
#include <stdexcept>
#include <string>

namespace cardengine::detail {

inline std::string trim(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size() &&
           std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

inline std::string lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Strict: the whole value must be an integer, nothing trailing.
inline int parse_int(const std::string& value, int line) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        if (used != value.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("line " + std::to_string(line) +
                                    ": bad integer '" + value + "'");
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("line " + std::to_string(line) +
                                    ": integer out of range '" + value + "'");
    }
}

inline std::string unquote(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

// Strict: the whole value must be a number, nothing trailing.
inline double parse_double(const std::string& value, int line) {
    try {
        std::size_t used = 0;
        const double parsed = std::stod(value, &used);
        if (used != value.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return parsed;
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("line " + std::to_string(line) +
                                    ": bad number '" + value + "'");
    } catch (const std::out_of_range&) {
        throw std::invalid_argument("line " + std::to_string(line) +
                                    ": number out of range '" + value + "'");
    }
}

}  // namespace cardengine::detail
