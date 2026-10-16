# Giao thức và định dạng file

## Serial (firmware bench H562, fw 0.5)

115200 8N1, mỗi lệnh kết thúc CR hoặc LF.

| Lệnh | Tác dụng | Nút trong app |
|---|---|---|
| `e` / `d` | Bật / tắt driver | Enable / Disable |
| `x <pct>` | Giới hạn \|duty\| | Speed Percentage → Update |
| `o <pct>`, `+`, `-`, `r`, `0` | Vòng hở | Duty vòng hở, −5 / 0 / +5 / Đảo |
| `g <độ>` | PID tới góc khớp | Góc khớp, quỹ đạo (giao thức "Bench 1 khớp") |
| `p <cnt>`, `t <cnt>` | PID theo count | Console |
| `v <độ/s>` | Tốc độ đều, `v 0` giữ | Tốc độ khớp → Chạy / Giữ |
| `k kp ki kd` | Gain PID | PID → Apply |
| `H [pct] [mA]` | Home | Home (tham số ở "Home duty / dòng") |
| `z` `c` `s` `l <hz>` `h` | Zero, xóa lỗi, 1 dòng log, tốc độ log, help | Zero, Clear fault, Status, Position Sample Rate, Help |
| `i` | Dòng `ID: CyberHand-H562 fw 0.5 joints=1 cnt90=7000` | App tự gửi khi kết nối |
| `j [cnt]` | Count tại 90° (RAM, mặc định `JOINT_RANGE_CNT`) | Calibration → Áp dụng; tự gửi khi kết nối nếu khác |
| `I [mA ms resp deg]` | Quá dòng: ngưỡng, thời gian giữ, phản ứng 0 trip / 1 lùi `deg` / 2 giữ / 3 chỉ báo | Safety → Ghi FW |
| `a <hz>` | `i_mA` trong log = trung bình 1/hz s (1..1000) | Power Sample Rate → Update |

Dòng log: `en:1,duty:0,out:42.0,i_mA:150,flt:0,diag:1,pos:700,tgt:729,vel:350,mode:1`.
Dòng chữ (help, `OK`, `ERR`, `HOME:`, `ID:`) không bắt đầu bằng số. `HOME: done` → Homed = Yes.
`ID: ... joints=N cnt90=7000`: app gửi `j <cnt/90° của profile>` nếu khác (tắt được trong Calibration, khi tắt thì app lấy giá trị của firmware).
`OVERI: retract i=412 mA to 5200 cnt` (trip / retract / hold / warn): firmware đã phản ứng quá dòng → Status hiện RETR / HOLD / TRIP / WARN trong 3 s.
Bảo vệ luôn dùng dòng 1 ms; `a` chỉ làm mượt số trong log.

## Firmware MCB G474 (fw 0.7)

Mỗi MCU điều khiển 3 khớp và có 1 cổng USB CDC riêng:

| MCU | motor 1 | motor 2 | motor 3 |
|---|---|---|---|
| U6 | I | T1 (Thumb Rotation) | T2 (Thumb Flexion) |
| U7 | M | R | P |

ID (lệnh `i`, firmware tự in khi mở cổng):
`ID: CyberHand-G474 fw 0.7-g474 joints=3 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2`
(`keys` = khớp của motor 1..3 trên MCU này).

| Lệnh | Tác dụng |
|---|---|
| `g <khớp 1-6\|tên> <độ>` | Số khớp toàn tay theo thứ tự app: 1 T2, 2 T1, 3 I, 4 M, 5 R, 6 P. Khớp của MCU kia: bỏ qua, không trả lời. Trả lời `OK pos <cnt> cnt <tên>` |
| `g <độ>` | Motor đang chọn |
| `m <tên\|1..3>` | Chọn motor cho các lệnh 1 motor: `o + - r 0 p t v k x H z j I a s`. Tên không thuộc MCU này → `ERR m ...` |
| `e` / `d` / `c` | Cả 3 motor của MCU; `e <tên>` / `d <tên>` / `c <tên>` = 1 motor |
| `D` | Tắt hết |

Log = 10 kênh cũ của motor đang chọn + `qN/tN/iN` (N = số khớp toàn tay) cho 3 khớp của MCU, ví dụ U6:
`en:1,duty:0,out:12.0,i_mA:150,flt:0,diag:1,pos:700,tgt:729,vel:350,mode:1,q3:700,t3:729,i3:150,q2:0,t2:0,i2:0,q1:0,t1:0,i1:0`

App khi gặp ID có `keys=` (hoặc `CyberHand-G474`):
1. Lưu MCU và danh sách khớp của cổng; tự chọn giao thức "Từng khớp".
2. Gửi `m <khớp chính>`. Nếu khớp chính nằm ở MCU kia: không gửi `m`, console báo "Khớp X nằm ở U7 — cắm cổng U7",
   khóa các nút 1 khớp (Home, Zero, Status, vòng hở, tốc độ, PID, Speed/Power Update, Ghi FW, Calibration).
3. Một lần mỗi kết nối (nếu bật "Ghi cnt/90° xuống firmware khi kết nối"): với từng khớp của MCU `m <key>`, `j <cnt/90°>`
   (và `I ...` nếu Safety xử lý tại Firmware), xong `m <khớp chính>`.
4. Đổi "Khớp chính" → gửi `m <key>` (hoặc báo như bước 2).
5. Khớp của MCU kia hiện "chưa kết nối" (mờ) và không được gửi lệnh. Enable / Disable / Clear fault = `e` / `d` / `c` cả MCU;
   Manual Control có nút E / D từng khớp (`e <key>` / `d <key>`).

Mô phỏng: `python tools/fw_sim.py --g474 U6` (hoặc `U7`).

## 2 MCU: 1 cổng hoặc 2 cổng

Communication → **Kết nối**: `1 cổng` / `2 cổng · U6 + U7`. Connect mở cả hai cổng ở chế độ 2 cổng;
cổng 2 dùng chung Baud / Line ending với cổng 1. Mỗi cổng hiện 1 dòng trạng thái: MCU, RX, khớp, motor đang chọn, link.

### Chế độ A — 2 cổng (fw 0.7 trên cả hai MCU)

Mỗi cổng gửi ID riêng; app lưu MCU/khớp theo từng cổng và gửi lệnh tới cổng sở hữu khớp:

| Lệnh | Đi tới |
|---|---|
| `g <j> <độ>` (stream, Manual, pose, timeline) | cổng có khớp j |
| `m <key>`, `e/d/c <key>` | cổng có key |
| `e`, `d`, `c`, `D`, `i`, `l <hz>`, `G ...` | cả hai cổng |
| lệnh 1 motor (`o + - r 0 p t v k x H z j I a s`, `g <độ>`) | cổng của Khớp chính |
| `@U7 <dòng>` | gửi thẳng `<dòng>` tới cổng U7 |

Console: cột nguồn U6 / U7 cho mọi dòng (nhận và gửi), lọc **Nguồn**, chọn **đích** (Tự động / U6 / U7 / Cả hai).
Telemetry: cổng của Khớp chính "dẫn nhịp" — mỗi dòng log của nó được gộp với dòng mới nhất (< 1,5 s) của cổng kia
thành 1 mẫu 6 khớp: 10 kênh của motor chính, `qN/tN/iN` của cả hai, `flt` = OR hai MCU; 10 kênh của MCU kia
có tiền tố (`U7.out`, `U7.en` ...) trong Data Monitor / Live. Đồng bộ `j`/`I` khi kết nối chạy riêng từng cổng.

Mô phỏng: hai tiến trình `fw_sim.py --g474 U6` và `fw_sim.py --g474 U7`; app: `--port <U6> --port2 <U7>`.

### Chế độ B — 1 cổng qua link UART (fw 0.8-g474)

MCU có USB (master) chuyển lệnh của MCU kia qua UART4 và gộp log. App thấy 1 cổng 6 khớp:

- ID: `ID: CyberHand-G474 fw 0.8-g474 joints=6 cnt90=7000 mcu=U6 motor=1(I) keys=I,T1,T2,M,R,P link=U7`.
  Mất link: `keys=I,T1,T2 link=-` (app coi peer U7 đang mất).
- Dòng chữ của MCU kia có tiền tố `[U7] ` — app bỏ tiền tố, gắn nguồn U7 trong console, vẫn xử lý `HOME:`, `OVERI:`, `OK`.
  `[U7] ID: ...` không đổi bố cục cổng; dòng log có tiền tố (vd. sau `@U7 s`) chỉ hiện trong console.
- `LINK: up U7` / `LINK: lost U7` (firmware in ID mới trước đó). Log thêm `lk:1|0`.
- Mất link: khớp M, R, P mờ "chưa kết nối", lệnh tới chúng bị chặn, chip **LINK** đỏ, sự kiện LINK.
  Link lên lại: app gửi lại `m`/`j`/`I` cho các khớp vừa quay lại.
- `flt` 8 = LINK (MCU bị bridge tự tắt motor khi mất heartbeat 300 ms) → chip LINK, Clear fault để chạy lại.
- `@U7 <dòng>` gửi thô qua link (firmware xử lý). App bật DTR khi mở cổng và gửi `i` nếu 500 ms chưa có ID.

Mô phỏng: `fw_sim.py --g474 U6 --bridge`; trong console gõ `link off` / `link on` để thử mất link.

### Đề xuất cho 6 khớp (app đã hỗ trợ sẵn)

- Log: `q1..q6` (pos, count), `t1..t6` (tgt, count), `i1..i6` (mA), `en`, `flt`, `mode`.
- Lệnh: giao thức "Từng khớp" gửi `g <khớp 1-6> <độ>`; giao thức "1 dòng" gửi `G q1 q2 q3 q4 q5 q6`.
- Thứ tự khớp: T2, T1, I, M, R, P (như trên board MCB).

## Cảm biến bo mạch (fw 0.9)

SHT40 (nhiệt độ + độ ẩm, I2C) và cầu chia VBAT (đo tối đa ~10.3 V) chỉ nằm trên U7; U6 nhận giá trị qua link UART,
nên cổng nào (1 cổng hay 2 cổng) cũng có số liệu. Firmware tự bảo vệ: quá nhiệt hoặc pin yếu thì tắt cả 6 motor;
app chỉ hiển thị và cảnh báo.

**Log** — sau `qN/tN/iN`, trước `lk:`: `...,q6:..,t6:..,i6:..,temp:25.0,rh:45.0,vbat:7.99,lk:1`
- `temp` °C (1 số lẻ, có thể âm), `rh` %RH, `vbat` V (2 số lẻ).
- `temp`/`rh` chỉ có khi SHT40 đọc được; `vbat` chỉ có khi đã đo. Thiếu trường → hiển thị "—", không coi là 0
  (CSV để trống). VBAT < 3 V = không cắm pin.
- 2 cổng: cả hai log đều có `temp/rh/vbat` → app lấy một nguồn, ưu tiên cổng U7 (cổng kia chỉ dùng khi U7 im > 3 s),
  không vẽ trùng (không có `U7.temp`).

**Lệnh `sense`** — app gửi sau ID của fw ≥ 0.9 (tới cổng U7 nếu đang mở) và khi bấm làm mới:
```
OK sense U7 T=25.0 C RH=45.0 % VBAT=7.99 V | off at T>=70.0 C (on <60.0) VBAT<6.40 V (on >=6.80) | hot=0 low=0 i2c_err=0
```
- Có thể thêm ` (T invalid)` (T=n/a, RH=n/a) hoặc ` (VBAT not measured)`.
- Lỗi: `ERR no sensor data (SHT40 + VBAT are on U7, link to U7 is down)`.
- Ngưỡng (70.0 / 60.0 / 6.40 / 6.80) lấy từ dòng này (hoặc từ dòng sự kiện), không hard-code; chưa có thì không vẽ vạch.

**Sự kiện** (có thể có tiền tố `[U7] ` / `[U6] ` — app bỏ trước khi parse):
```
OTEMP: board 72.0 C >= 70.0 C (SHT40 on U7), all motors off (flt 16)
OTEMP: cleared (< 60.0 C), send e to run again
VBAT: battery 6.31 V < 6.40 V, all motors off (flt 32)
VBAT: ok (7.40 V), send e to run again
```
- Cùng sự kiện đến 2 lần trong ~1 s (dòng U6 + dòng `[U7]`, hoặc từ 2 cổng) → app gộp thành 1 (cửa sổ 1.5 s).
- Khi đang bị chặn, `e` trả về `ERR board over-temperature, motors stay off ('sense')` /
  `ERR board battery low, motors stay off ('sense')`.

**Bit `flt`**: 1 DIAG, 2 OVERI (quá dòng), 4 ESTOP, 8 LINK, 16 OTEMP (quá nhiệt), 32 VBAT (pin yếu).

**App**
- Status → ô **Board**: nhiệt độ, độ ẩm, VBAT, % pin (LiPo, số cell ở Settings → Board, mặc định 2S: 6.4 V = 0 %,
  8.4 V = 100 %, nội suy theo đường cong xả), "từ U7" + tuổi dữ liệu (> 3 s không có số mới → mờ),
  sparkline 10 phút cho °C và V kèm vạch ngưỡng. Màu: bình thường / cảnh báo (T ≥ ngưỡng tắt − 10 °C,
  VBAT < ngưỡng bật lại) / báo động (hot=1, low=1, flt 16/32). Chip **Quá nhiệt** / **Pin yếu** trong Faults.
- OTEMP / VBAT (hoặc flt 16/32): banner đỏ cố định "Quá nhiệt 72.0 °C — đã tắt cả 6 motor" /
  "Pin yếu 6.31 V — đã tắt cả 6 motor"; nút Enable (Motor, Manual Control E, Calibration Start) bị khóa, tooltip ghi lý do.
  Dòng `cleared` / `ok` → banner "Đã an toàn — bấm Enable để chạy lại" (app không tự gửi `e`); gửi `e` → banner tắt.
  Sự kiện ghi vào Console và Event log (Data Monitor) kèm giờ.
- Live: `temp` / `rh` / `vbat` là kênh riêng trên trục Y phụ bên phải (°C · %, V), `rh` mặc định tắt.
- Live → **Ghi CSV**: `Documents/CyberHandStudio/recordings/rec_<ngày>_<giờ>.csv`, cột
  `t, T2..P (độ), T2_tgt.., T2_mA.., en, flt, mode, temp, rh, vbat` (mở lại được trong Text mode).

Mô phỏng (`fw_sim.py --g474 ...`): log có `temp/rh/vbat`, `sense`, sự kiện OTEMP/VBAT, flt 16/32, từ chối `e`;
lệnh thử `simtemp <C>`, `simvbat <V>`, `simsht off|on`; tham số `--temp`, `--vbat`. Ở chế độ 2 tiến trình
(`--g474 U6` + `--g474 U7`) mỗi sim tự giả lập cảm biến (U6 không nhận được giá trị của U7), nên `simtemp` gửi cho từng cổng.

## CAN v1 (fw 0.11-g474, Waveshare USB-CAN-FD)

Bus: CAN FD ISO + BRS, trọng tài 1 Mbit/s, dữ liệu 2 Mbit/s, ID 11 bit, little-endian.
Node 1 = U6 (I, T1, T2), node 2 = U7 (M, R, P). Số khớp: 1 T2, 2 T1, 3 I, 4 M, 5 R, 6 P.
Đấu J11: 1 GND, 2 CANL, 3 CANH; một điện trở đầu cuối 120 Ω.

| ID | Hướng | Len | Nội dung |
|---|---|---|---|
| 0x000 | host → tất cả | 0–1 | E-STOP: tắt mọi motor ngay, flt 4 |
| 0x080 | host → tất cả | 1 | HOST_HB mỗi 100 ms, byte0 = seq. Mất > 300 ms → MCB tắt motor, flt 8 |
| 0x100 | host → tất cả | 12 | POS_ALL: int16 q[6] (0.01°), khớp 1..6, 0x7FFF = giữ nguyên; không trả lời |
| 0x200 | host → cả 2 MCU | ≤64 | CMD_TEXT: 1 dòng CLI, không CR/LF |
| 0x200+n | host → MCU n | ≤64 | CMD_TEXT riêng MCU n |
| 0x280+n | MCU n → host | ≤64 | TEXT: luồng ký tự, dòng kết thúc `\n`, 1 dòng có thể qua nhiều khung |
| 0x300+n | MCU n → host | 64 | STATUS, tần số `canlog <hz>` (mặc định 50) |
| 0x700+n / 0x780+n | | | ping / pong (app bỏ qua) |

**STATUS** (64 byte)
- `[0]` version = 1, `[1]` node, `[2..3]` seq u16
- 3 khối × 14 byte từ `[4]`: `+0` số khớp, `+1` bit (b0 enabled, b1 homed, b2..3 mode 0 open / 1 PID / 2 home,
  b4 DIAG thấp), `+2` flt, `+3` duty int8 %, `+4` pos int32, `+8` target int32 (= pos khi không PID), `+12` dòng int16 mA
- `[46]` temp int16 0.1 °C (0x7FFF = không có), `[48]` RH u16 0.1 % (0xFFFF = không có), `[50]` VBAT u16 mV (0 = không có)
- `[52]` bit: b0 quá nhiệt, b1 pin yếu, b2 link UART U6–U7 sống; `[56..59]` uptime u32 ms
- Góc: deg = count × 90 / cnt90 (cnt90 từ dòng ID).

Khung FD chỉ có độ dài 0–8, 12, 16, 20, 24, 32, 48, 64: phần thiếu đệm 0x00 (TEXT: bên nhận bỏ NUL và `\r`).

**App** — Communication → Kết nối → **CAN**
- Adapter "Waveshare USB-CAN-FD": thiết bị (mặc định 0), kênh CAN1/CAN2, bitrate trọng tài / dữ liệu,
  "Điện trở 120 Ω — bật trên adapter" (`ZCAN_SetResistanceEnable`, mặc định bật: cùng 120 Ω phía MCB thành 60 Ω cho bus 2 đầu;
  tắt nếu bus đã có 2 điện trở).
  `ControlCANFD.dll` (x64, gói *USB-CAN-FD Library* của Waveshare) có sẵn trong bản build, cạnh `CyberHandStudio.exe`
  (nguồn trong repo: `third_party/waveshare/`).
- Khi Connect: gửi `i` và `canlog 50` tới từng node, chờ ID của cả hai (2 dòng trạng thái U6 / U7 như chế độ 2 cổng).
- STATUS → dòng log kiểu USB (`en,duty,out,i_mA,flt,diag,pos,tgt,mode` của khớp chính, `qN,tN,iN`, `temp,rh,vbat`)
  → cùng đường Telemetry / Live / Board như USB. Log rate (Settings) gửi `canlog <hz>`.
- Stream, thanh trượt, Text/Timeline, pose → POS_ALL theo Motion Send Rate (tối đa 100 Hz, gộp giá trị mới nhất);
  1 khớp = POS_ALL với 5 khớp còn lại 0x7FFF.
- Enable/Disable/Home/Zero/PID/`x o v j I a` → CMD_TEXT tới MCU của khớp chọn, trước đó `m <khớp>`.
  Enable/Disable cả tay, Disconnect (`D`) → 0x200.
- Console: đích Tự động / U6 / U7 / Cả hai (0x200); dòng nhận gắn [U6] / [U7].
- HOST_HB chạy trong luồng CAN riêng (không phụ thuộc UI). Nút ■ cạnh Connect và phím **Esc** = E-STOP (0x000).
- E-STOP chốt trong firmware (flt 4, `ESTOP: …`; `e` trả `ERR E-STOP latched (flt 4), send c first`): app hiện thanh đỏ
  "E-STOP" với nút **Xóa E-STOP** (`c` tới mọi MCU), khóa các nút Enable cho tới khi flt 4 hết.
- Thống kê: khung/s RX/TX, STATUS mất theo seq từng node, trạng thái lỗi bus (TEC/REC, error passive, bus-off;
  đọc bằng `ZCAN_ReadChannelErrInfo` của DLL, có trong DLL nhưng không có trong header).
- Mode **CAN monitor**: khung thô (thời gian, Rx/Tx, ID, len, FD/BRS, hex, giải mã), lọc ID (`281 282 300-3FF !080`),
  tạm dừng, gửi khung tay (ID hex, data hex, FD, BRS).

**Bus ảo (không cần adapter)**: `python tools/fw_sim.py --can-udp 7470` giả lập 2 node, app chọn adapter "Ảo (UDP)"
127.0.0.1:7470 (hoặc `CyberHandStudio --can udp:7470`). Mỗi khung = 1 datagram:
`"CF"` u8 flags (b0 FD, b1 BRS, b2 EXT) u8 len u32 id (LE) data[len]. Sim học địa chỉ app từ datagram đầu tiên;
lệnh thử `simdrop <n>` làm STATUS kế tiếp nhảy seq n (kiểm tra bộ đếm mất khung).

## WiFi (MainBoard, ESP32-S2 bridge)

ESP32-S2-WROOM-1 trên MainBoard là cầu nối trong suốt TCP ↔ UART (921600) tới MCU3: byte nào đi byte đó.
SoftAP `CyberHand-XXXX`, mật khẩu `cyberhand`, module `192.168.4.1`, TCP port `8888`, 1 client (kết nối mới thay kết nối cũ),
~90 KB/s mỗi chiều. PC vào WiFi này thì card WiFi đó mất internet.

**App** — Communication → Kết nối → **WiFi** (hoặc `CyberHandStudio --wifi 192.168.4.1:8888`)
- TCP client, `TCP_NODELAY` + keep-alive, dùng như 1 cổng COM: cùng dòng lệnh / log như USB (hiện là CLI text, kết thúc `\n`).
- TCP là luồng byte: app gom byte vào buffer rồi tách dòng (1 dòng qua nhiều lần recv, nhiều dòng trong 1 lần đều được).
- Tự kết nối lại mỗi 1 s khi rớt (timeout kết nối 3 s); Disconnect thì dừng hẳn.
- Keep-alive: đã kết nối mà 1 s chưa gửi lệnh nào thì app gửi `ping\n` (lệnh khác cũng tính là "đã gửi").
  ESP chế độ echo trả `ping`, MainBoard sau này trả `pong`. Ping/pong ẩn khỏi Console (tick "Hiện ping" để xem), đo RTT.
- Mất liên lạc: không nhận byte nào (kể cả ping/pong) trong 3 s → ngắt, kết nối lại. Tắt được bằng "Timeout 3 s"
  khi test với phía kia im lặng. `ping`/`pong` là tạm, đổi khi chốt frame nhị phân.
- 2 app (2 máy) cùng nối 1 module sẽ giành nhau vì module chỉ nhận 1 client.
- Chưa làm: frame nhị phân (header, độ dài, CRC) dùng chung cho mọi link, UDP stream lực/trạng thái — chờ chốt.
- fw_sim trả `pong` cho `ping`.

**Thử không cần phần cứng**: `python tools/fw_sim.py --g474 U6 --bridge --tcp 8888` rồi app WiFi `127.0.0.1:8888`
(fw_sim nhận 1 client như module). Module thật chưa có MainBoard: nối tắt IO17–IO18 → app nhận lại đúng dòng nó gửi.

## CSV quỹ đạo (chế độ Text)

```
# comment; thêm "# unit=percent" nếu giá trị là % hành trình
t,T2,T1,I,M,R,P
0,0,0,0,0,0,0
1.5,60,45,90,90,90,90
```

- `t` giây, tăng dần. Cột khớp theo tên (T2, T1, I, M, R, P / q1..q6 / tên hiển thị). Ô trống = không điều khiển khớp đó.
- Phân cách `,` `;` hoặc tab. Giữa hai dòng app nội suy tuyến tính, gửi theo Motion Send Rate.

## Timeline (.json)

```json
{"format":"cyberhand-timeline-2","length":6,
 "clips":[
  {"track":"I","start":0.8,"duration":1.2,"kind":"move","target":55,"easing":"smooth"},
  {"track":"M","start":0,"duration":4,"kind":"sine","trough":0,"crest":90,"period":2,"shift":0},
  {"track":"R","start":1,"duration":1,"kind":"ramp","from":90,"to":0},
  {"track":"P","start":0,"duration":2,"kind":"hold","value":30},
  {"track":"T1","start":2,"duration":1,"kind":"power","from":0,"to":60,"exponent":2,"shape":"inout"}]}
```

| kind | Tham số | Giá trị (t tính từ đầu clip, D = duration) |
|---|---|---|
| `move` | `target`, `easing` (linear/smooth/easein/easeout/step) | từ giá trị trước clip tới `target` |
| `hold` | `value` | hằng số |
| `ramp` | `from`, `to` | tuyến tính, slope = (to − from)/D |
| `sine` | `trough`, `crest`, `period`, `shift` | mid − amp·cos(2π(t − shift)/period), bắt đầu ở đáy |
| `square` / `triangle` / `sawtooth` | như sine | vuông (nửa đầu = đáy), tam giác, răng cưa |
| `power` | `from`, `to`, `exponent`, `shape` (in/out/inout/outin) | from + (to − from)·f(t/D) |

Sau clip, khớp giữ giá trị cuối. File `cyberhand-timeline-1` (chỉ `move`) vẫn mở được.

## Stream (UDP, mặc định cổng 7400)

Mỗi gói là một khung: `{"q":[0,0,45,90,90,90]}`, `{"I":45,"M":90}`, `0,0,45,90,90,90` hoặc `I:45,M:90`.
Đơn vị chọn trong app: độ, % hành trình, hoặc thô + hiệu chuẩn (Start → cử động hết biên → Save).

Bên tay: JSON `"hand":"left"|"right"` hoặc tiền tố `L ` / `R ` (`L 0,0,45,90,90,90`). Handed = Trái/Phải thì bỏ khung của tay kia.

Input Processing, theo thứ tự: trung bình N khung → lọc thông thấp bậc 1 (Hz) → giới hạn tốc độ (°/s) và gia tốc (°/s², không vượt đích), chạy ở Sampling Rate. 0 = tắt.

## Macro

Mỗi dòng là một lệnh CLI, hoặc: `@wait <ms>`, `@waitfor <chuỗi> [ms]`, `@repeat <N>` … `@end`, `@pose <tên>`, `@log <chữ>`, `# comment`.
