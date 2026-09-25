# 02. 상점·인벤토리·코인

## 범위
- 혼자, 농장 1개, 메모리 상태. 
- DB·로그인·방문·채팅 없음.
- 추가: 코인, 인벤토리(씨앗·수확물), 상점(사기/팔기). - 01에서의 씨앗 무제한 가정 제거.
- 상점은 위치와 무관한 메뉴 (찾아가는것이 아님)

## 데이터
전역으로 서버가 들고 있는다. 인덱스는 01처럼 crop 번호 (0 없음 / 1 순무 / 2 당근 / 3 호박).

```
int coin = 100; // 시작 100G
int seeds[CROP_COUNT] = {0}; // 씨앗 개수 (index = crop 번호)
int held[CROP_COUNT] = {0}; // 수확물 개수
```

작물 가격은 CropDef에 추가한다.
```
struct CropDef { uint8_t stages; uint64_t stage_ms; int seed_price; int sell_price; }
const CropDef CROPS[CROP_COUNT] = {
    {0, 0, 0, 0}, // CROP_NONE
    {2, 30*1000, 10, 20}, // TURNIP 순무
    {3, 60*1000, 30, 75}, // CARROT 당근
    {4, 150*1000, 100, 300}, // PUMPKIN 호박
}
```

## 패킷
- 1~4: 일반
- 10~13: 클라->서버, 농사 액션
- 20~21: 서버->클라, 밭 정보
- 30번대: 클라->서버, 경제 요청 (이번에 추가)
- 40번대: 서버->클라, 경제 알림 (이번에 추가)

- 클라 -> 서버
    - PKT_BUY (30), payload 2byte: [crop: uint8][count: uint8] // 씨앗 count개 구매
    - PKT_SELL (31), payload 2byte: [crop: uint8][count: uint8] // 수확물 count개 판매
- 서버 -> 클라
    - PKT_WALLET (40), payload 10byte: [coin: uint32] + crop 1~3 각각 [seeds: uint8][held: uint8];
        - 코인·인벤 전체를 한 번에. 바뀔 때마다 & 접속 직후 1회

## 서버 검증
- BUY:
    1. payload 크기가 2가 아니면 무시
    2. crop이 1~3 밖이면 무시
    3. 코인 < 씨앗값 * count 면 거부
    4. 통과: 코인 -= 씨앗값 * count, seeds[crop] += count, PKT_WALLET 전송

- SELL:
    1. payload 크기가 2가 아니면 무시
    2. crop이 1~3 밖이면 무시
    3. held[crop] < count 면 거부
    4. 통과: held[crop] -= count, 코인 += 판매가 * count, PKT_WALLET 전송

- 거부 = 바뀐 것 없이 현재 지갑 상태를 PKT_WALLET 으로 돌려줌.

## 01과 연결
- PKT_PLANT 검증에 추가: seeds[crop] < 1 이면 거부. 통과하면 심으면서 seeds[crop] -= 1, PKT_WALLET 전송.
- PKT_HARVEST 성공 시: 수확물 적재. 단, harvest()가 칸을 초기화(t = Tile{})하므로 **crop 번호를 harvest 부르기 전에 읽어둬야 한다.** 그 crop으로 held[crop] += 1, PKT_WALLET 전송.

## 열어둔 것
- 로그인·계정·DB 저장: 05
- 방문자 물주기 보상(+2G): 03
- 인벤/씨앗 개수 상한: 없음 (현재 무한)
- 씨앗을 산 만큼만 심을 수 있으니, 처음엔 씨앗 몇 개 쥐고 시작할지 정할 것 (또는 코인으로 사서 시작)