# 05. DB 저장·가입/로그인·워커풀

## 범위
- MySQL에 계정·코인·인벤토리·농장 칸 저장. 서버 재시작 후에도 유지.
- 가입·로그인. 플레이어 구분을 fd -> 계정 id로.
- DB 호출은 워커풀에서 (게임 루프 안 막기).
- 오프라인 농장 방문(로드/언로드): 06

## 테이블
- users: id(AUTO_INCREMENT), nickname(UNIQUE), password_hash, coin
- inventory: user_id, corp, seeds, held (UNIQUE: user_id + crop)
- farm_tiles: user_id, x, y, state, crop, stage, watered_at (UNIQUE: user_id + x + y)
    - watered_at은 에포크 ms라 BIGINT
- 타입·제약 세부는 CREATE TABLE 쓸 때 확정

## 데이터 (구조 변경)
- 현재: clients·farms 키 = fd. 접속마다 바뀌는 번호라 저장 기준으로 못 씀. 휘발.
- 변경: 계정 상태(코인·인벤토리)와 farms의 키 = 계정 id (user_id). 연결(fd)은 "어느 계정인지"만 가리킴.
    - 로그인 전 연결: 계정 없음 상태. 가입·로그인 패킷만 받고 나머지 무시.
- dirty 표시 (메모리 값이 DB와 달라졌다는 표시)
    - 농장: 칸 단위. 바뀐 칸만 UPDATE.
    - 계정: 계정 단위 하나. 코인·인벤토리 중 하나라도 바뀌면 계정 전체를 한 트랜잭션으로.
        (구매처럼 코인·씨앗이 같이 바뀌는 걸 따로 쓰면 둘이 어긋날 수 있음)

## 흐름
- 가입: 닉네임 중복이면 실패. 성공 시 users·inventory·farm_tiles 기본값 생성.
- 로그인: 닉네임·비밀번호 확인 -> DB에서 계정·농장 로드 -> 자기 농장 방 입장 (03과 동일)
- 중복 로그인: 기존 연결 끊고 새 연결 받기.
    - 새 연결은 DB 다시 읽지 않고 메모리 상태를 그대로 넘겨받음 (아직 안 쓴 변경 보존)
- 로그아웃(접속 끊김): dirty 저장 후 언로드

## 저장 시점
- 모아서 쓰기: 주기(예: 30초) + 로그아웃 + 정상 종료(SIGTERM·SIGINT 받으면 전부 쓰고 종료)
    - `SIGTERM`: 종료 요청 신호. `kill` 명령의 기본값. 
    - `SIGINT`: 터미널에서 `Ctrl+C` 누르면 가는 신호
- 크래시·kill -9는 처리 불가 -> 주기가 잃는 양의 상한 (30s 잃을 수 있음)
    - `SIGKILL`: `kill -9`
- 즉시 쓰기는 안 함 (현금 결제·유저 간 거래 없음). 실제 게임의 경우 이렇게 잃어서는 안되는 것들만 즉시 저장.

## 워커풀
- chat-server 방식: 메인 루프 -> 작업 큐 -> 워커가 DB 호출 -> 결과 큐 + eventfd로 메인 루프 깨움
- 로그인 응답·로드 결과는 메인 루프에서 적용 (게임 상태는 메인 스레드만 만짐)

## 패킷
- 70번대: 클라 -> 서버, 계정
- 80번대: 서버 -> 클라, 계정 결과
- 클라 -> 서버
    - PKT_SIGNUP (70), payload: [nick_len:1][nick][pw_len:1][pw]
    - PKT_LOGIN (71), payload: 70과 동일
- 서버 -> 클라
    - PKT_AUTH_RESULT (80), payload: [reason:1][account_id:4]
        - reason: 닉네임 중복 / 닉네임·비번 틀림 / 형식 오류 등
- 바뀌는 것
    - PKT_WELCOME (4): 접속 직후 fd 전송 -> 없앰. 내 id는 AUTH_RESULT로.
    - 다른 패킷의 id(owner_id·sender_id 등)는 전부 계정 id.

## 열어둔 것
- 비밀번호 해시 방식 (라이브러리 선택)
- 닉네임 길이·허용 문자
- 평문 비밀번호가 TCP로 감
- 오프라인 농장 방문·로드/언로드: 06