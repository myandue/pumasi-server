#include <mysql/mysql.h>
#include <mysql/mysqld_error.h>
#include <sodium.h>
#include <queue>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "./db.h"
#include "./protocol.h"

static MYSQL* db = nullptr; // static을 붙임으로써 다른 파일에서 db에 접근하지 못하게 한다.

std::queue<std::function<void()>> db_results; // 워커 -> 메인으로 보낼 일 (db 워커가 결과 보내는 곳)
std::mutex db_results_mtx;
ThreadPool* db_pool = nullptr; // main 밖 함수에서 사용하므로 전역 선언 
int db_event_fd = -1;

bool db_init() {
    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        printf("mysql library init fail\n");
        return false;
    }
    if (sodium_init() < 0) {
        printf("sodium init fail\n");
        return false;
    }
    return true;
}

void drain_db_results() {
    uint64_t cnt;
    ssize_t r = read(db_event_fd, &cnt, sizeof(cnt));
    (void) r;

    std::queue<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lock(db_results_mtx);
        std::swap(local, db_results);
    }

    while(!local.empty()) {
        local.front()(); // 맨 앞의 함수(`local.front()`)를 받아서 실행(`()`)
        local.pop(); // 실행한 것 제거
    }
}

// 워커에서 부름: 메인 스레드가 실행할 함수를 넣고 초인종 울리기 (event_fd)
void post_to_main(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lock(db_results_mtx);
        db_results.push(std::move(fn));
    }
    uint64_t one = 1;
    ssize_t w = write(db_event_fd, &one, sizeof(one));
    (void) w;
}

bool db_connect() {
    const char* pw = std::getenv("PUMASI_DB_PW");
    db = mysql_init(nullptr);
    if (!mysql_real_connect(db, "localhost", "pumasi", pw, "pumasi", 0, nullptr, 0)) {
        printf("connect fail: %s\n", mysql_error(db));
        return false;
    } else {
        printf("db connected\n");
        return true;
    }
}

uint8_t signup_db(const std::string& nick, const std::string& pw, uint32_t& out_id) {
    // 1) 비밀번호 해시 (솔트 포함 문자열)
    char hash[crypto_pwhash_STRBYTES]; // 결과를 담을 글자 배열
    if (crypto_pwhash_str(hash, pw.data(), pw.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) { // (결과, 비밀번호 주소, 비밀번호 길이, 연산 강도, 메모리 강도)
        
        return AUTH_SERVER_ERROR;
    }

    if (mysql_query(db, "START TRANSACTION")) return AUTH_SERVER_ERROR; // 지금부터 `db`에 일어나는 것들을 하나의 트랜잭션으로 묶음. 실패시 0이 아닌 값.


    // 2) users INSERT: 사용자 입력(nick)이 들어가므로 prepared statement
    // SQL 인젝션 방지. "INSERT INTO users ... VALUES ('" + nick + "', ...)" 라고 쓰고, nick에 `); DROP TABLE users; --` 같은 것을 넣으면 SQL 명령으로 실행됨
    // 그래서 SQL 틀(`vALUES (?, ?))`과 값을 따로 보냄. `?`가 뭐건 SQL 명령으로 실행되지 X
    MYSQL_STMT* stmt = mysql_stmt_init(db); // prepared statement용 핸들 생성
    const char* sql = "INSERT INTO users (nickname, password_hash) VALUES (?, ?)";

    // 틀에 보낼 값 세팅
    MYSQL_BIND b[2]; // `MYSQL_BIND`: `?` 하나에 넣을 값을 설명하는 구조체. 해당 구조체에 대한 크기 2의 배열(값이 2개)
    memset(b, 0, sizeof(b)); // 구조체에 안 쓰는 필드가 많은데, 쓰레기값 제거를 위해 해당 필드들을 모두 0으로 초기화.

    b[0].buffer_type = MYSQL_TYPE_STRING;
    b[0].buffer = const_cast<char*>(nick.data()); // buffer가 받는 타입에 const가 없기때문에 const를 떼주는 캐스팅
    b[0].buffer_length = nick.size();

    b[1].buffer_type = MYSQL_TYPE_STRING;
    b[1].buffer = hash;
    b[1].buffer_length = strlen(hash);

    // INSERT 실행 - 아래 세개 중 하나라도 실패하면(0이 아니게 되면) if 조건 참 -> if 문 실행
    if (mysql_stmt_prepare(stmt, sql, strlen(sql))
        || mysql_stmt_bind_named_param(stmt, b, 2, nullptr) // nullptr: 이름 없는 `?`를 쓴다 의 의미
        || mysql_stmt_execute(stmt)) {
        
        unsigned int err = mysql_stmt_errno(stmt); // stmt의 마지막 동작에 대한 에러
        mysql_stmt_close(stmt);
        mysql_query(db, "ROLLBACK"); // 트랜잭션 롤백
        return err == ER_DUP_ENTRY ? AUTH_DUP_NICK : AUTH_SERVER_ERROR; // 유니크 제약에 걸렸을 경우와 그 외
    }

    // 성공했을 시 생성된 id 받기
    uint32_t id = (uint32_t) mysql_stmt_insert_id(stmt); // stmt의 마지막 insert에 대한 id 값
    mysql_stmt_close(stmt);

    // 인벤토리 3행, 농장 36칸 INSERT
    std::string inv = "INSERT INTO inventory (user_id, crop) VALUES";
    for (int crop = 1 ; crop <= 3 ; crop++) {
        if (crop > 1) inv += ",";
        inv += "(" + std::to_string(id) + ", " + std::to_string(crop) + ")";
    }

    std::string tiles = "INSERT INTO farm_tiles (user_id, x, y) VALUES";
    for (int y = 0 ; y < 6 ; y++) {
        for (int x = 0 ; x < 6 ; x++) {
            if (x || y) tiles += ", "; // '(id, 0, 0)' 앞에는 ', '를 붙이지 않음
            tiles += "(" + std::to_string(id) + ", " + std::to_string(x) + ", " + std::to_string(y) + ")";
        }
    }

    if (mysql_query(db, inv.c_str()) || mysql_query(db, tiles.c_str()) || mysql_query(db, "COMMIT")) {
        mysql_query(db, "ROLLBACK");
        return AUTH_SERVER_ERROR;
    }

    out_id = id;
    return AUTH_OK;
}