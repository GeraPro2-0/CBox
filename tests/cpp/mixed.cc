#include <string>

extern "C" int cbox_cpp_value(void) {
    const std::string value = "42";
    return std::stoi(value);
}