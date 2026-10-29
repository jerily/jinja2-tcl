// Copied helpers from jerily-webserver/core/string-utils.
#include "string_utils.h"
#include <cctype>
#include <cstdio>

namespace jinja2_tcl::string_utils {

char lowercase_ascii_char(char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

std::string trim_ascii_copy(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

std::string lowercase_ascii_copy(const std::string &value) {
    std::string lowered = value;
    for (char &character : lowered) {
        character = lowercase_ascii_char(character);
    }
    return lowered;
}

std::string percent_encode_uri_component(const std::string &value) {
    static const char kHexDigits[] = "0123456789ABCDEF";

    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char byte : value) {
        const bool unreserved = (byte >= 'A' && byte <= 'Z') ||
                                (byte >= 'a' && byte <= 'z') ||
                                (byte >= '0' && byte <= '9') ||
                                byte == '-' ||
                                byte == '.' ||
                                byte == '_' ||
                                byte == '~';
        if (unreserved) {
            encoded.push_back(static_cast<char>(byte));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
        encoded.push_back(kHexDigits[byte & 0x0FU]);
    }
    return encoded;
}

std::string html_escape(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char character : value) {
        switch (character) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&#39;";
                break;
            default:
                escaped.push_back(character);
                break;
        }
    }
    return escaped;
}

std::string json_escape(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char byte : value) {
        switch (byte) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (byte < 0x20) {
                    char buffer[7];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(byte));
                    escaped += buffer;
                } else {
                    escaped.push_back(static_cast<char>(byte));
                }
                break;
        }
    }
    return escaped;
}

std::string html_safe_json_escape(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char byte : value) {
        switch (byte) {
            case '<':
                escaped += "\\u003c";
                break;
            case '>':
                escaped += "\\u003e";
                break;
            case '&':
                escaped += "\\u0026";
                break;
            default:
                escaped += json_escape(std::string(1, static_cast<char>(byte)));
                break;
        }
    }
    return escaped;
}

}
