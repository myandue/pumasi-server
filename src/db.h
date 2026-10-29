#pragma once

#include <string>
#include <cstdint>
#include <functional>

#include "./thread_pool.h"
#include "./farm.h"

struct UserData {
    std::string nick;
    int coin = 0;
    int seeds[CROP_COUNT] = {0};
    int held[CROP_COUNT] = {0};
    Farm farm;
};

// main에서도 쓰기 때문에 cpp에서 정의하기 전에 현재 헤더 파일에서 선언
extern ThreadPool* db_pool; // 메인 -> 워커 일감 넣는 곳
extern int db_event_fd;

// 메인 스레드
bool db_init(); // 라이브러리 초기화. 워커 스레드 만들기 전에 한 번. 성공 여부 반환.
void drain_db_results(); // db_event_fd가 울렸을 때 메인 루프에서 호출.

// 워커 스레드
void post_to_main(std::function<void()> fn);
bool db_connect();
uint8_t signup_db(const std::string& nick, const std::string& pw, uint32_t& out_id); // 반환값이 reason이기 때문에 또 다른 반환값인 생성 id는 매개변수로.
uint8_t login_db(const std::string& nick, const std::string& pw, uint32_t& out_id);
bool load_user_db(uint32_t user_id, UserData& out);