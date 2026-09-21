#include <cassert>
#include "./farm.h"

int main() {
    // 1. 순무, 0단계, 시각 1000에 물을 준 칸
    Tile t;
    t.state = GROWING;
    t.crop = TURNIP;
    t.stage = 0;
    t.watered_at = 1000;

    // 2. 30초가 되기 1ms 전: 아무것도 안 바뀜
    refresh(t, 1000 + 29999);
    assert(t.stage == 0);
    assert(t.watered_at == 1000);
    assert(t.state == GROWING);

    // 3. 정확히 30초: 한 단계 오르고 마름
    refresh(t, 1000 + 30000);
    assert(t.stage == 1);
    assert(t.watered_at == 0);
    assert(t.state == GROWING);

    // 4. 마른 채로는 시간이 아무리 지나도 그대로
    refresh(t, 1000 + 30000 + 30000);
    assert(t.stage == 1);
    assert(t.watered_at == 0);
    assert(t.state == GROWING);

    // 5. 다시 물을 주고 30초 뒤: stage 2, RIPE
    uint64_t watered_at = now_ms();
    t.watered_at = watered_at;
    refresh(t, watered_at + 30000);
    assert(t.stage == 2);
    assert(t.watered_at == 0);
    assert(t.state == RIPE);

    return 0;
}