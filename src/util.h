#pragma once

#include <string>

// UTF-8 문자열의 글자 수를 세는 함수 (바이트 수x)
inline int utf8_len(const std::string& s) {
    int n = 0;
    for (unsigned char uc : s) {
        if ((uc & 0xC0) != 0x80) n++; // 이어지는 바이트(10xxxxxx)가 아니면 새로운 글자의 첫 바이트
    }
    return n;
}