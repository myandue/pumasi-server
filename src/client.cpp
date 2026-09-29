#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unordered_map>
#include <cctype> // console에서 작물 물 여부 표시(대문자)를 위함 
#include <vector>

#include <termios.h>

#include "./protocol.h"
#include "./farm.h"

struct Client {
    int x;
    int y;
};
std::unordered_map<int, Client> clients; // 현재 같은 방에 존재하는 client들 

Tile farm[FIELD_SIZE][FIELD_SIZE];
int my_id = -1;

int64_t server_offset = 0; // 서버와의 시간차

int face_dx = 0; // 바라보는 방향
int face_dy = 1; // 초기값 (0, 1)은 아래 방향을 바라보는 중

int my_coin = 0;
int my_seeds[CROP_COUNT] = {0};
int my_held[CROP_COUNT] = {0};

// 상점 창
bool shop_open = false;

// 유저 선택(방문 목적) 창
bool visit_open = false;

// 현재 농장 주인
int farm_owner = -1;

// 전체 유저 목록
std::vector<int> players;

// 나를 뺀 접속자 목록 (화면과 키 처리가 같은 순서를 써야 함)
std::vector<int> others() {
    std::vector<int> v;
    for (int id : players) if (id != my_id) v.push_back(id);
    return v;
}

int epfd = epoll_create1(0);

// 상점 창
void draw_shop() {
    printf("=== 상점 ===\n코인: %dG\n\n", my_coin);
    printf(" %-8s 씨앗값   판매가   보유(씨앗/수확물)\n", "작물");
    
    const char* name[] = {"?", "순무", "당근", "호박"};
    for (int c = 1 ; c < CROP_COUNT ; c++) {
        printf(" %-8s %4dG    %4dG          %2d / %2d\n", // 숫자: 칸수, -: 왼쪽 정렬 / 기본은 오른쪽 정렬
            name[c], CROPS[c].seed_price, CROPS[c].sell_price, my_seeds[c], my_held[c]
        );
    }

    printf("\n사기: 1 순무 / 2 당근 / 3 호박\n");
    printf("팔기: (shift) 1 순무 / 2 당근 / 3 호박\n");
    printf("닫기: b 또는 q\n");
} 

// 유저 선택(방문 목적) 창
void draw_visit() {
    printf("=== 방문 ===\n");
    std::vector<int> o = others();
    if (o.empty()) printf("(방문할 사람 없음)\n");
    for (size_t i = 0 ; i < o.size() ; i++) printf("%zu. %d번 농장\n", i + 1, o[i]);
    printf("\n번호: 방문 | h: 내 농장으로 | v/q: 닫기\n");
}

// 밭과 플레이어를 10*10 격자로 그린다
void draw() {
    uint64_t server_now = (uint64_t)((int64_t)now_ms() + server_offset);

    // 화면 지우고 커서 맨 위로
    printf("\033[2J\033[H"); // '\033': ESC, '[2J': 화면 클리어, '[H': 커서를 좌상단으로

    if (shop_open) { // 상점 창
        draw_shop();
    } else if (visit_open) { // 유저 리스트 창
        draw_visit();
    } else {
        for (int y = WORLD_MIN ; y <= WORLD_MAX ; y++) {
            for (int x = WORLD_MIN ; x <= WORLD_MAX ; x++) {
                char ch = '.'; // 밭 밖 바닥

                // 밭 체크
                bool in_field = (x >= FIELD_X0 && x < FIELD_X0 + FIELD_SIZE &&
                                    y >= FIELD_Y0 && y < FIELD_Y0 + FIELD_SIZE);

                if (in_field) {
                    Tile t = farm[y-FIELD_Y0][x-FIELD_X0]; // 복사본
                    refresh(t, server_now); // 표시용 단계 계산 
                    const char* c = "?tcp"; // crop 1~3 -> t/c/p
                    if (t.state == BARE) ch = '_';
                    else if (t.state == TILLED) ch = '=';
                    else if (t.state == GROWING) ch = t.watered_at ? toupper(c[t.crop]) : c[t.crop]; // toupper: 젖은 작물 표기용 대문자 변환
                    else  /* RIPE */ ch = '*';
                }

                for (auto& [id, p]: clients) { // 플레이어가 있으면 덮어씀. '나'일 경우에는 '@' / 다른 사람은 'P'
                    if (p.x == x && p.y == y) ch = (id == my_id) ? '@' : 'P';
                }
                putchar(ch); putchar(' ');
            }
            // 줄(y) 바꿈
            putchar('\n');
        }

        printf("\n_: 맨땅 | =: 갈린땅 | t/c/p: 작물(마름) | T/C/P: 작물(젖음) | *: 작물 다 자람 | @: 나 | P: 다른 플레이어\n");
        printf("wasd: 이동 | t: 갈기 | 1/2/3: 심기 | e: 물 | r: 수확 | q: 종료\n");

        // 방향 적용 내 위치 출력 
        if (clients.count(my_id)) {
            printf("대상 칸: (%d, %d)\n", clients[my_id].x + face_dx, clients[my_id].y + face_dy);
        }

        printf("상점 및 인벤토리 열기: b\n");
        printf("방문 가능한 플레이어 리스트 보기: v\n");
    }


    // 현재 버퍼 비우기
    fflush(stdout);
}

// 앞 칸을 대상으로 농사 패킷 전송
void send_action(int sock, char key) {
    uint16_t type;
    uint8_t crop = 0;

    std::string payload;

    if (shop_open) {
        if (key == '1') { type = PKT_BUY; crop = 1; }
        else if (key == '2') { type = PKT_BUY; crop = 2; }
        else if (key == '3') { type = PKT_BUY; crop = 3; }
        else if (key == '!') { type = PKT_SELL; crop = 1; }
        else if (key == '@') { type = PKT_SELL; crop = 2; }
        else if (key == '#') { type = PKT_SELL; crop = 3; }
        else return;

        payload.push_back(crop);
        payload.push_back((uint8_t)1); // TODO console 플레이에서는 1로 고정
    } else {
        if (key == 't') type = PKT_TILL;
        else if (key == '1') { type = PKT_PLANT; crop = 1; }
        else if (key == '2') { type = PKT_PLANT; crop = 2; }
        else if (key == '3') { type = PKT_PLANT; crop = 3; }
        else if (key == 'e') type = PKT_WATER;
        else if (key == 'r') type = PKT_HARVEST;
        else return;

        if (!clients.count(my_id)) return;
        int tx = clients[my_id].x + face_dx;
        int ty = clients[my_id].y + face_dy;

        payload.push_back((uint8_t)tx);
        payload.push_back((uint8_t)ty);
        if (type == PKT_PLANT) payload.push_back(crop);
    }


    std::string pkt = make_packet(type, payload);

    ssize_t w = write(sock, pkt.data(), pkt.size());
    (void) w;
}

// 다른 사람 농장 방문
void send_visit(int sock, int owner) {
    std::string payload;
    put_u32(payload, owner);
    std::string pkt = make_packet(PKT_VISIT, payload);
    ssize_t w = write(sock, pkt.data(), pkt.size());
    (void) w;
}

int main() {
    signal(SIGPIPE, SIG_IGN);

    // 1) 소켓 만들고 서버에 connect (서버는 bind/listen/accept)
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    // sockaddr_in vs. sockaddr
    // sockaddr_in: IPv4 전용. 필드 읽기가 좋아서 구조체 채울 때 사용.
    // sockaddr: 옛날 범용 구조체. bind/connect/accept 함수가 인자타입으로 요구하는 구조체.
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr); // 서버 주소 
    // inet_pton 함수를 통해 4바이트 이진 주소로 변환되어 addr.sin_addr.s_addr가 채워짐.

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }
    printf("서버 접속 완료\n");

    // 터미널 설정 변경
    // 기본 - canotical 모드: 입력을 한 줄씩 모아서 엔터를 쳐야 프로그램에 넘겨줌 + 키를 화면에 echo
    // 변경: 키 하나 누르면 엔터 없이 즉시 넘김 + echo 끄기
    // 1) 원래 설정 저장
    struct termios orig; 
    tcgetattr(0, &orig); // fd 0 = stdin: 지금 터미널(fd 0)의 현재 설정을 읽어서 orig에 담기
    // 2) 복사본 만들어서 플래그 끄기
    struct termios raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO); // 두 비트만 끔
    // 3) 즉시 적용
    tcsetattr(0, TCSANOW, &raw); // raw 세팅값을 지금 터미널(fd 0)에 즉시 적용(TCSANOW)
    
    // fd 장부 등록
    // a) 소켓
    struct epoll_event s_ev;
    s_ev.events = EPOLLIN;
    s_ev.data.fd = sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &s_ev);

    // b) 키보드 입력 / 0 = stdin(표준 입력, 키보드)
    struct epoll_event keyboard_ev;
    keyboard_ev.events = EPOLLIN;
    keyboard_ev.data.fd = 0;
    epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &keyboard_ev);

    struct epoll_event events[2];

    std::string recv_buf;
    char buf[4096];

    // 받기 루프 
    while (true) {
        int n = epoll_wait(epfd, events, 2, -1);
        if (n < 0) { perror("epoll_wait"); break; }

        bool closed = false;

        for (int i = 0 ; i < n ; i++) {
            int fd = events[i].data.fd;

            if (fd == sock) {
                ssize_t cnt = read(sock, buf, sizeof(buf));

                if (cnt <= 0) { closed = true; break; } // 0: 서버 끊김, <0: 에러
                recv_buf.append(buf, cnt);

                // 프레임 추출
                while (true) {
                    if (recv_buf.size() < 4) break;
                    uint16_t length = get_u16(recv_buf, 0);
                    if (recv_buf.size() < (4 + length)) break;
                    uint16_t type = get_u16(recv_buf, 2);
                    std::string payload = recv_buf.substr(4, length);
                    recv_buf.erase(0, 4 + length);

                    // 처리
                    if (type == PKT_SNAPSHOT) {
                        // 모든 유저의 위치값 받기
                        clients.clear();
                        int client_cnt = get_u16(payload, 0);

                        for (int i = 0 ; i < client_cnt ; i++) {
                            int j = 3*i;
                            int id = get_u32(payload, 4*j + 2);
                            int x = get_u32(payload, 4*(j+1) + 2);
                            int y = get_u32(payload, 4*(j+2) + 2);

                            clients[id] = Client{.x = x, .y = y};
                        }
                    } else if (type == PKT_WELCOME) {
                        // 서버에서의 내 id 값 받기
                        my_id = get_u32(payload, 0);
                    } else if (type == PKT_FARM_SNAPSHOT) {
                        farm_owner = get_u32(payload, 0); // 누구 농장인지 
                        server_offset = (int64_t)get_u64(payload, 4) - (int64_t)now_ms(); // 부호가 필요한 계산에서는 int로 캐스팅을 해준다

                        for (int i = 0 ; i < FIELD_SIZE ; i++) {
                            for (int j = 0 ; j < FIELD_SIZE ; j++) {
                                int offset = 12 + (i*FIELD_SIZE + j)*11;

                                farm[i][j].state = payload[offset];
                                farm[i][j].crop = payload[offset+1];
                                farm[i][j].stage = payload[offset+2];
                                farm[i][j].watered_at = get_u64(payload, offset+3);
                            }
                        }
                    } else if (type == PKT_TILE_UPDATE) {
                        int x = payload[0];
                        int y = payload[1];

                        farm[y-FIELD_Y0][x-FIELD_X0].state = payload[2];
                        farm[y-FIELD_Y0][x-FIELD_X0].crop = payload[3];
                        farm[y-FIELD_Y0][x-FIELD_X0].stage = payload[4];
                        farm[y-FIELD_Y0][x-FIELD_X0].watered_at = get_u64(payload, 5);
                    } else if (type == PKT_WALLET) {
                        my_coin = get_u32(payload, 0);

                        int offset = 4;
                        for (int i = 1 ; i < CROP_COUNT ; i++) {
                            my_seeds[i] = payload[offset];
                            my_held[i] = payload[offset+1];
                            offset += 2;
                        }
                    } else if (type == PKT_PLAYER_LIST) {
                        players.clear();
                        int player_cnt = get_u16(payload, 0);

                        for (int i = 0 ; i < player_cnt ; i++) {
                            players.push_back(get_u32(payload, i*4 + 2));
                        }
                    }

                    // 출력
                    draw();
                }
                
                continue;
            }

            if (fd == 0) { // 키보드 입력
                char key;
                ssize_t cnt = read(0, &key, 1);

                if (cnt <= 0) { closed = true; break; } // 0: 입력 닫힘, <0: 에러
                
                if (shop_open) {
                    if (key == 'q' || key == 'b') {
                        shop_open = false;
                    } else {
                        send_action(sock, key);
                    }
                } else if (visit_open) {
                    std::vector<int> o = others();
                    if (key >= '1' && key <= '9' && (size_t)(key - '1') < o.size()) {
                        send_visit(sock, o[key-'1']);
                        visit_open = false;
                    } else if (key == 'h') { // 내 방으로
                        send_visit(sock, my_id);
                        visit_open = false;
                    } else if (key == 'v' || key == 'q') {
                        visit_open = false;
                    }
                } else {
                    if (key == 'w' || key == 's' || key == 'a' || key == 'd') {
                        int dx = 0;
                        int dy = 0;
                        if (key == 'w') dy = -1;
                        if (key == 's') dy = +1;
                        if (key == 'a') dx = -1;
                        if (key == 'd') dx = +1;
                        
                        // 방향 기억
                        face_dx = dx;
                        face_dy = dy;

                        std::string payload;
                        payload.push_back((int8_t)dx); // 1바이트인 int8_t로 캐스팅해서 넣기
                        payload.push_back((int8_t)dy);
                        
                        std::string pkt = make_packet(PKT_MOVE, payload);

                        ssize_t w = write(sock, pkt.data(), pkt.size());
                        (void) w;
                    } else if (key == 'q') { // 종료 키
                        closed = true;
                    } else if (key == 'b')  { // 상점 열기
                        shop_open = true;
                    } else if (key == 'v') {
                        visit_open = true;
                    } else {
                        send_action(sock, key);
                    }
                }

                continue;
            }
        }    

        if (closed) break;
    }
    
    close(sock);

    // 터미널 원복
    tcsetattr(0, TCSANOW, &orig);

    return 0;
}