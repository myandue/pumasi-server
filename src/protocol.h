#pragma once
#include <string>
#include <cstdint>

enum PacketType : uint16_t {
    PKT_TICK = 1, // 서버 -> 클라: 지금 몇 tick 째인지
    PKT_MOVE = 2, // 클라 -> 서버: client의 이동 수신
    PKT_SNAPSHOT = 3, // 서버 -> 클라: 모든 플레이어의 좌표
    PKT_WELCOME = 4, // 서버 -> 클라: 해당 클라의 fd 알려주기

    PKT_TILL = 10, // 클라 -> 서버: [x:1][y:1]
    PKT_PLANT = 11, // 클라 -> 서버: [x:1][y:1][crop:1]
    PKT_WATER = 12, // 클라 -> 서버: [x:1][y:1]
    PKT_HARVEST = 13, // 클라 -> 서버: [x:1][y:1]
    
    PKT_TILE_UPDATE = 20, // 서버 -> 클라: [x:1][y:1][state:1][crop:1][stage:1][watered_at:8]
    PKT_FARM_SNAPSHOT = 21, // 서버 -> 클라 [server_now:8] + 36칸*[state:1][crop:1][stage:1][watered_at:8]

    PKT_BUY = 30, // 클라 -> 서버: [crop:1][count:1]
    PKT_SELL = 31, // 클라 -> 서버: [crop:1][count:1]

    PKT_WALLET = 40, // 서버 -> 클라: [coin:4] + crop 1~3 각각 [seeds:1][held:1]
};

// uint16을 빅엔디안 2바이트로 buf 끝에 붙이기
inline void put_u16(std::string& buf, uint16_t v) {
    buf.push_back((v >> 8) & 0xFF); // 상위 바이트 먼저
    buf.push_back(v & 0xFF); // 하위 바이트
}

// uint32를 빅엔디안 4바이트로
inline void put_u32(std::string& buf, uint32_t v) {
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back(v & 0xFF);
}

// uint64를 빅엔디안 8바이트로
inline void put_u64(std::string& buf, uint64_t v) {
    buf.push_back((v >> 56) & 0xFF);
    buf.push_back((v >> 48) & 0xFF);
    buf.push_back((v >> 40) & 0xFF);
    buf.push_back((v >> 32) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back(v & 0xFF);
}

// [lenth(2)][type(2)][payload] 완성된 패킷 바이트 만들기
inline std::string make_packet(uint16_t type, const std::string& payload) {
    std::string pkt;
    put_u16(pkt, payload.size()); // length = payload 바이트 수 (헤더 제외)
    put_u16(pkt, type);
    pkt += payload; // 페이로드 이어붙이기 
    return pkt;
}

// offset 위치 바이트와 offset+1 위치 바이트를 합쳐서 하나의 숫자로 만드는 작업
// char 는 부호가 있는 타입이라 값이 오염될 수 있기 때문에 'unsigned char'로 캐스팅 후 연산 진행 
inline uint16_t get_u16(std::string& buf, int offset) {
    return (((unsigned char)buf[offset] << 8) | (unsigned char)buf[offset+1]);
}

inline uint32_t get_u32(std::string& buf, int offset) {
    uint32_t payload = ((unsigned char)buf[offset] << 24);
    payload |= ((unsigned char)buf[offset+1] << 16);
    payload |= ((unsigned char)buf[offset+2] << 8);
    payload |= (unsigned char)buf[offset+3];
    return payload;
}

inline uint64_t get_u64(std::string& buf, int offset) {
    // unsigned char는 계산에 들어가는 순간 32비트 int로 바뀐다.
    // 32비트 값을 32칸 이상 밀면 결과가 망가지기 때문에 64비트로 먼저 바꿔줘야한다.
    uint64_t payload = ((uint64_t)(unsigned char)buf[offset] << 56);
    payload |= ((uint64_t)(unsigned char)buf[offset+1] << 48);
    payload |= ((uint64_t)(unsigned char)buf[offset+2] << 40);
    payload |= ((uint64_t)(unsigned char)buf[offset+3] << 32);
    payload |= ((uint64_t)(unsigned char)buf[offset+4] << 24);
    payload |= ((uint64_t)(unsigned char)buf[offset+5] << 16);
    payload |= ((uint64_t)(unsigned char)buf[offset+6] << 8);
    payload |= (uint64_t)(unsigned char)buf[offset+7];
    return payload;
}