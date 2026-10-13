#include <mysql/mysql.h>
#include <cstdio>
#include <cstdlib>

int main() {
    const char* pw = std::getenv("PUMASI_DB_PW");
    MYSQL* conn = mysql_init(nullptr);
    if (!mysql_real_connect(conn, "localhost", "pumasi", pw, "pumasi", 0, nullptr, 0)) {
        printf("connect fail: %s\n", mysql_error(conn));
        return 1;
    }
    printf("connected\n");

    // 1. INSERT
    if (mysql_query(conn,
            "INSERT INTO users (nickname, password_hash) VALUES ('tester', 'dummy')")) {
                printf("insert fail: %s\n", mysql_error(conn)); // mysql_query는 성공하면 0, 실패하면 1
            } else {
                printf("inserted id = %llu\n", (unsigned long long) mysql_insert_id(conn));
            }

    // 2. SELECT
    if (mysql_query(conn,
            "SELECT id, nickname, coin FROM users WHERE nickname = 'tester'")) {
                printf("select fail: %s\n", mysql_error(conn));
            } else {
                MYSQL_RES* res = mysql_store_result(conn); // 결과 행들을 전부 받아 메모리에 담은 묶음을 만들고 그 주소를 res에 받는다.
                MYSQL_ROW row; // 실제타입: `char**`, 문자열 주소들의 배열
                while((row = mysql_fetch_row(res))) { // 묶음에서 행을 하나씩 꺼내 `row`에 넣는다. 
                    printf("id=%s nickname=%s coin=%s\n", row[0], row[1], row[2]);
                }
                mysql_free_result(res); // 다 쓴 묶음을 해제. 안하면 메모리 누수.
            }

    mysql_close(conn);
}