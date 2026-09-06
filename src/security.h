#pragma once
#include <string>
namespace ag {
bool valid_token(const std::string& token);
bool constant_time_equal(const std::string& left, const std::string& right);
std::string read_token_file(const std::string& path);
void create_token_file(const std::string& path);
}
