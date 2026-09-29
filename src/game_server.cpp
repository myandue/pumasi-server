#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h> // epoll_create1, epoll_ctl, epoll_wait
#include <sys/eventfd.h> // eventfd
#include <sys/timerfd.h> //timerfd
#include <cstdint> // uint64_t
#include <cstdlib>

#include <fcntl.h> // fcntl, O_NONBLOCK
#include <cerrno> // errno, EAGAIN

#include <csignal>
#include <unordered_map>
#include <string>
#include <queue>
#include <vector>
#include <mutex>

#include "./protocol.h"
#include "./farm.h"

const int MAX_STEP = 1; // 한 tick 최대 이동량 (±1)

struct Client {
    std::string send_buf;
    std::string recv_buf;
    
    //위치
    int x;
    int y;

    // 인벤토리·코인
    int coin = 100;
    int seeds[CROP_COUNT] = {0};
    int held[CROP_COUNT] = {0};

    // 지금 보고 있는 농장의 주인 id
    int current_farm = -1;
};
std::unordered_map<int, Client> clients;

struct Farm { Tile tiles[FIELD_SIZE][FIELD_SIZE]; };
std::unordered_map<int, Farm> farms; // 키 = 주인id(fd)

int epfd = epoll_create1(0); // 장부 개설

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0); // 현재 플래그 읽고
    fcntl(fd, F_SETFL, flags | O_NONBLOCK); // O_NONBLOCK 켜기, 현재 플래그 + 논블록 플래그 
} 
// fcntl: "이 fd의 속성을 바꿔줘" / 여기서는 소켓을 "읽을 게 없으면 잠들지 말고 즉시 EAGAIN 반환"모드로 바꾸는 것 

// 클라한테 해당 클라의 id(fd) 전송
void send_welcome(int fd) {
    std::string payload;
    put_u32(payload, fd);
    std::string pkt = make_packet(PKT_WELCOME, payload);
    ssize_t w = write(fd, pkt.data(), pkt.size());
    (void) w;
}

// 농장 입장할 때 마다: 입장한 농장 + 서버 현재 시각 + 밭 전체 스냅샷
void send_farm_snapshot(int fd, int farm_owner_id) { // farm_owner_id: 현재 농장의 owner
    std::string payload;

    // 0) owner_id
    put_u32(payload, farm_owner_id);

    // 1) server_now 8바이트
    uint64_t now = now_ms();
    put_u64(payload, now);

    // 2) farm. 보내기 직전에 refresh.
    Farm& f = farms[farm_owner_id];

    for (int i = 0 ; i < FIELD_SIZE ; i++) {
        for (int j = 0 ; j < FIELD_SIZE ; j++) {
            refresh(f.tiles[i][j], now);
            payload.push_back(f.tiles[i][j].state);
            payload.push_back(f.tiles[i][j].crop);
            payload.push_back(f.tiles[i][j].stage);
            put_u64(payload, f.tiles[i][j].watered_at);
        }
    }

    // 3) make_packet
    std::string pkt = make_packet(PKT_FARM_SNAPSHOT, payload);

    // 4) write
    ssize_t w = write(fd, pkt.data(), pkt.size());
    (void) w;
}

// 칸 하나의 현재 값을 담은 TILE_UPDATE 패킷을 만든다
std::string make_tile_update(int farm_owner_id, uint8_t x, uint8_t y) {
    std::string payload;
    Tile tile = farms[farm_owner_id].tiles[y-FIELD_Y0][x-FIELD_X0];

    payload.push_back(x);
    payload.push_back(y);
    payload.push_back(tile.state);
    payload.push_back(tile.crop);
    payload.push_back(tile.stage);
    put_u64(payload, tile.watered_at);

    return make_packet(PKT_TILE_UPDATE, payload);
}

// 그 방(농장)에 있는 사람 전원에게 pkt 전송
void send_to_room(int farm_owner_id, const std::string& pkt) {
    for (auto& [cfd, c]: clients) {
        if (c.current_farm != farm_owner_id) continue;
        ssize_t w = write(cfd, pkt.data(), pkt.size());
        (void) w;
    }
}

// 땅 업데이트 시 요청자 한 명에게만 전송
void send_tile_update(int fd, int farm_owner_id, uint8_t x, uint8_t y) {
    std::string pkt = make_tile_update(farm_owner_id, x, y);
    ssize_t w = write(fd, pkt.data(), pkt.size());
    (void) w;
}

void send_wallet(int fd, Client& c) {
    std::string payload;
    
    put_u32(payload, c.coin);
    
    for (int i = 1 ; i < CROP_COUNT ; i++) {
        payload.push_back(c.seeds[i]);
        payload.push_back(c.held[i]);
    }

    std::string pkt = make_packet(PKT_WALLET, payload);

    ssize_t w = write(fd, pkt.data(), pkt.size());
    (void) w;
}

// move
void handle_move(Client& c, const std::string& payload) {
    if (payload.size() != 2) return;

    int8_t dx = (int8_t)payload[0];
    int8_t dy = (int8_t)payload[1];

    // 이상값 거부
    if (dx < -MAX_STEP || dx > MAX_STEP || dy < -MAX_STEP || dy > MAX_STEP) return;

    int nx = c.x + dx;
    int ny = c.y + dy;
    
    // 세계보다 크거나 작을 경우 세계 사이즈 적용
    if (nx > WORLD_MAX) nx = WORLD_MAX;
    if (nx < WORLD_MIN) nx = WORLD_MIN;
    if (ny > WORLD_MAX) ny = WORLD_MAX;
    if (ny < WORLD_MIN) ny = WORLD_MIN;

    c.x = nx;
    c.y = ny;
}

// 갈기/심기/물/수확 공통 처리. 01 문서 "서버 검증" 순서 그대로
void handle_farm_action(int fd, Client& c, uint16_t type, const std::string& payload) {
    // 1. 크기가 맞지 않으면 무시 (심기 3, 나머지 2)
    size_t need = (type == PKT_PLANT) ? 3 : 2;
    if (payload.size() != need) return;

    uint8_t x = (uint8_t)payload[0];
    uint8_t y = (uint8_t)payload[1];
    uint8_t crop = CROP_NONE;
    if (type == PKT_PLANT) {
        crop = payload[2];

        // crop이 1~3을 벗어나면 무시
        if (crop < 1 || crop > 3) return;
    }

    // 2. 밭 영역 밖이면 무시
    if (x < FIELD_X0 || x >= FIELD_X0 + FIELD_SIZE ||
    y < FIELD_Y0 || y >= FIELD_Y0 + FIELD_SIZE) return;

    // 3. 내 위치에서 가로/세로 각각 1칸 이내가 아니면 거부
    // 참고로 1, 2의 경우 정상 클라 코드라면 만들 수 없는 값이기 때문에 바로 return을 주며 무시를,
    // 3, 5의 경우 클라가 요청할 수 있는 값이기 때문에 `send_tile_update`로 응답을 주며 거부한다.
    if (abs(c.x - x) > 1 || abs(c.y - y) > 1) {
        send_tile_update(fd, c.current_farm, x, y); // 현재 '틀렸다'라는 응답을 받지는 않는다. 그저 응답.
        return;
    }

    // '수확'의 경우, 수행 유저와 농장주가 다르다면 거부
    if (type == PKT_HARVEST && fd != c.current_farm) {
        send_tile_update(fd, c.current_farm, x, y);
        return;
    }

    // 4. 갱신
    Tile& t = farms[c.current_farm].tiles[y-FIELD_Y0][x-FIELD_X0];
    uint64_t now = now_ms();
    refresh(t, now);

    // 씨앗을 갖고있지 않다면 거부
    if (type == PKT_PLANT && c.seeds[crop] < 1) {
        send_tile_update(fd, c.current_farm, x, y);
        return;
    }

    // 5~6. 행동. 필요한 상태가 아니면 함수가 false를 돌려주고 칸은 그대로.
    // false 시 요청자만 받고 true 시 방 전체 사람들이 받는다.
    bool ok = false;
    switch (type) {
        case PKT_TILL:
            ok = till(t); break;
        case PKT_PLANT: {
            ok = plant(t, crop);
            if (ok) c.seeds[crop] -= 1;
            send_wallet(fd, c);
            break;
        }
        case PKT_WATER:
            ok = water(t, now); break;
        case PKT_HARVEST: {
            uint8_t crop = t.crop; // harvest로 비우기 전에 crop 읽기.
            ok = harvest(t);
            if (ok) c.held[crop] += 1;
            send_wallet(fd, c);
            break;
        }
    }

    if (ok) {
        send_to_room(c.current_farm, make_tile_update(c.current_farm, x, y)); // 성공 시 방 전원에게
    } else {
        send_tile_update(fd, c.current_farm, x, y); // 실패 시 요청자(수행자)에게만
    }
}

void handle_wallet(int fd, Client& c, uint16_t type, const std::string payload) {
    // 1. 크기가 맞지 않으면 무시
    if (payload.size() != 2) return;

    uint8_t crop = (uint8_t)payload[0];
    uint8_t count = (uint8_t)payload[1];

    // 2. crop이 1~3을 벗어나면 무시
    if (crop < 1 || crop > 3) return;

    if (type == PKT_BUY) {
        // 3. 코인 < 씨앗값*count 면 거부
        if (c.coin < CROPS[crop].seed_price * count) {
            send_wallet(fd, c);
            return;
        }

        // 4. 처리
        c.coin -= CROPS[crop].seed_price * count;
        c.seeds[crop] += count;
        send_wallet(fd, c);

    } else if (type == PKT_SELL) {
        // 3. held[crop] < count 면 거부
        if (c.held[crop] < count) {
            send_wallet(fd, c);
            return;
        }

        // 4. 처리
        c.held[crop] -= count;
        c.coin += CROPS[crop].sell_price * count;
        send_wallet(fd, c);
    }
}

void on_tick() {    
    // 초기화는 이 줄에 도달했을 때 딱 한 번
    static uint32_t tick = 0;
    
    for (auto& [owner, farm]: farms) { // 방(농장)마다
        std::string body;
        uint16_t client_cnt = 0;
        for (auto& [cfd, c]: clients) {
            if (c.current_farm != owner) continue;
            put_u32(body, cfd);
            put_u32(body, c.x);
            put_u32(body, c.y);
            client_cnt ++;
        }
        if (client_cnt == 0) continue; // 아무도 없는 방은 건너뜀

        std::string payload;
        put_u16(payload, client_cnt);
        payload += body;
        send_to_room(owner, make_packet(PKT_SNAPSHOT, payload));
    }
    
    tick++;
}

void handle_packet(int fd, Client& c, uint16_t type, const std::string& payload) {
    switch (type) {
        case PKT_MOVE:
            handle_move(c, payload); break;
        case PKT_TILL: case PKT_PLANT: case PKT_WATER: case PKT_HARVEST:
            handle_farm_action(fd, c, type, payload); break;
        case PKT_BUY: case PKT_SELL:
            handle_wallet(fd, c, type, payload); break;
        default:
            break; // 모르는 type, 서버 -> 클라 전용 type: 무시
    }
}

int main() {
    signal(SIGPIPE, SIG_IGN); // SIGPIPE 무시 (프로세스 안 죽음)

    // 1) 듣기 소켓 준비
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {perror("socket"); return 1;}

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // timer_fd 만들어서 준비
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    // CLOCK_MONOTONIC: 시스템 시간 바뀌어도(NTP 보정 등) 영향 안 받는 단조 증가 시계

    struct itimerspec ts{};
    ts.it_value.tv_sec = 0;
    ts.it_value.tv_nsec = 50 * 1000000; // 첫 발사까지 50ms (tv_sec + tv_nsec)
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 50 * 1000000; // 그 뒤 반복 간격 50mc (tv_sec + tv_nsec)
    timerfd_settime(timer_fd, 0, &ts, nullptr);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(9000);

    if(bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {perror("bind"); return 1;}
    if(listen(listen_fd, 16) < 0) {perror("listen"); return 1;}
    printf("서버 대기 중... 포트 9000\n");

    // 2) epoll 장부 만들고 listen_fd 등록
    // <-> select의 fd_set master; FD_SET(listen_fd, &master); 자리
    struct epoll_event l_ev; 
    l_ev.events = EPOLLIN; // "읽기 가능해지면 알려줘" 
    l_ev.data.fd = listen_fd; // 누구인지 메모
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &l_ev); // 등록 (딱 한 번)

    // 만들어둔 timer_fd를 장부에 등록
    struct epoll_event t_ev;
    t_ev.events = EPOLLIN;
    t_ev.data.fd = timer_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &t_ev);

    struct epoll_event events[64]; // 준비된 것 받아올 배열 

    // 3) 이벤트 루프
    while (true) {
        // <-> select 자리. 준비된 것만 events[]에 담고 개수 n 리턴
        int n = epoll_wait(epfd, events, 64, -1); // -1 = 무한 대기
        if(n < 0) { perror("epoll_wait"); break;}

        // <-> 준비된 n개만 순회
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd; // 이 이벤트 주인 fd

            if (fd == timer_fd) {
                uint64_t expirations;
                ssize_t n = read(timer_fd, &expirations, sizeof(expirations));
                (void) n; // n으로 안받아도 되는데, read는 반환값을 받지 않고, 처리하지 않으면 경고를 발생시킴.
                // read의 두번째 인자는 주소값이어야하는데, expirations의 경우 값 하나짜리(uint64_t)라서 주소형태('&')로 받는다.
                
                on_tick();
                continue;
            }

            if (events[i].events & EPOLLIN) {
                if (fd == listen_fd) {
                    int client_fd = accept(listen_fd, NULL, NULL);
                    if (client_fd < 0) { perror("accept"); continue;}

                    set_nonblocking(client_fd); // 논블로킹

                    struct epoll_event cev;
                    cev.events = EPOLLIN | EPOLLET; // ET 켜기 (EPOLLET: ET 키는 옵션 / 도착 순간 한 번만 알림 / 안 키면 기본값(LT: 남아있으면 계속 알림))
                    cev.data.fd = client_fd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev); // <-> FD_SET

                    clients[client_fd] = Client{};
                    clients[client_fd].current_farm = client_fd; // 자기 농장에 입장
                    farms[client_fd] = Farm{};

                    send_welcome(client_fd);
                    send_farm_snapshot(client_fd, client_fd); // (받는 사람, 어느 농장)
                    send_wallet(client_fd, clients[client_fd]);

                    continue;
                } else {
                    // fd == client_fd
                    bool closed = false;
                    while (true) { // 수신버퍼를 다 비울 때까지 recv_buf에 받기
                        char buf[1024];
                        ssize_t cnt = read(fd, buf, sizeof(buf));

                        if (cnt > 0) {
                            if (clients.count(fd)) {
                                Client& c= clients[fd];
                                c.recv_buf.append(buf, cnt);
                            }
                        } else if (cnt == 0) { // 끊김
                            closed = true;
                            break;
                        } else { // 'cnt < 0': 에러
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 에러X. 다 읽었음 표시. -> 루프 종료

                            // 그 외 -> 실제 에러. client와 연결 회수 필요.
                            closed = true;
                            break;
                        }
                    }

                    // recv_buf에 받은 것들을 처리
                    if (clients.count(fd)) {
                        Client& c = clients[fd];

                        while (true) { // recv_buf에서 한 패킷씩 잘라서 처리
                            if (c.recv_buf.size() < 4) break; // 헤더가 온전히 들어오지 않아 루프 종료

                            // 패킷의 length: 패킷의 첫번째 두번째 바이트
                            uint16_t length = get_u16(c.recv_buf, 0);
                            
                            // 패킷이 온전히 도착했는지 체크. 안왔으면 루프 종료
                            if (c.recv_buf.size() < 4 + length) break;

                            // type: 패킷의 두번째 세번째 바이트
                            uint16_t type = get_u16(c.recv_buf, 2);

                            // payload: 네번째 바이트부터 length 길이 만큼
                            std::string payload = c.recv_buf.substr(4, length);

                            // recv_buf에서 제거
                            c.recv_buf.erase(0, 4 + length);

                            // 처리
                            handle_packet(fd, c, type, payload);
                        }
                    }
                    
                    if (closed) {
                        if (clients.count(fd)) {
                            clients.erase(fd);
                            if (farms.count(fd)) {
                                farms.erase(fd);
                            }
                        }
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                    }
                }
            }
        }
    }
    return 0;
}