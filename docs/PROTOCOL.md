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
| U6 | I | T1 | T2 |
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
- `flt` bit 8 (0x100) = LINK (MCU bị bridge tự tắt motor khi mất heartbeat 300 ms) → chip LINK, Clear fault để chạy lại.
- `@U7 <dòng>` gửi thô qua link (firmware xử lý). App bật DTR khi mở cổng và gửi `i` nếu 500 ms chưa có ID.

Mô phỏng: `fw_sim.py --g474 U6 --bridge`; trong console gõ `link off` / `link on` để thử mất link.

### Đề xuất cho 6 khớp (app đã hỗ trợ sẵn)

- Log: `q1..q6` (pos, count), `t1..t6` (tgt, count), `i1..i6` (mA), `en`, `flt`, `mode`.
- Lệnh: giao thức "Từng khớp" gửi `g <khớp 1-6> <độ>`; giao thức "1 dòng" gửi `G q1 q2 q3 q4 q5 q6`.
- Thứ tự khớp: T2, T1, I, M, R, P (như trên board MCB).

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
