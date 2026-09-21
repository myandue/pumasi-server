#pragma once
#include <cstdint>
#include <chrono>

// 밭 위치 (월드 좌표 기준)
const int FIELD_X0 = 2;
const int FIELD_Y0 = 2;
const int FIELD_SIZE = 6;


enum TileState: uint8_t { BARE = 0, TILLED = 1, GROWING = 2, RIPE = 3 };
enum CropType : uint8_t {TURNIP = 0, CARROT = 1, PUMPKIN = 2 };
const int CROP_COUNT = 3;

struct CropDef { uint8_t stages; uint64_t stage_ms; };
const CropDef CROPS[CROP_COUNT] = {
    {2, 30 * 1000}, // TURNIP 순무
    {3, 60 * 1000}, // CARROT 당근
    {4, 150 * 1000}, // PUMPKIN 호박
};

struct Tile {
    uint8_t state = BARE;
    uint8_t crop = 0;
    uint8_t stage = 0;
    uint64_t watered_at = 0; // 0 = 마름
};

// 현재 시각 (에포크 ms, 1970-01-01부터 지난 ms)
inline uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// 갱신: 01 문서 "시간 계산"
inline void refresh(Tile& t, uint64_t now) {
    if (t.state == GROWING
        && t.watered_at > 0 
        && now >= CROPS[t.crop].stage_ms + t.watered_at) { // 현재 시각 - 물 준 시각 >= 필요 시간
        t.stage += 1;
        t.watered_at = 0;
        if(t.stage == CROPS[t.crop].stages) {
            t.state = RIPE;
        }
    }
}