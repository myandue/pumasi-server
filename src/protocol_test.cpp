#include <cassert>
#include "./protocol.h"

int main() {
    std::string buf;
    put_u64(buf, 1789689600000ULL);
    assert(buf.size() == 8);
    assert(get_u64(buf, 0) == 1789689600000ULL);
}