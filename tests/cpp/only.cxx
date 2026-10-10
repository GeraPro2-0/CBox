#include <vector>

int main() {
    const std::vector<int> values = {42};
    return values.front() == 42 ? 0 : 1;
}