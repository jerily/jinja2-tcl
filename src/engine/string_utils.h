#pragma once
#include <string>

namespace jinja2_tcl::string_utils {
char lowercase_ascii_char(char value);
std::string trim_ascii_copy(const std::string &value);
std::string lowercase_ascii_copy(const std::string &value);
std::string percent_encode_uri_component(const std::string &value);
std::string html_escape(const std::string &value);
std::string json_escape(const std::string &value);
std::string html_safe_json_escape(const std::string &value);
}
