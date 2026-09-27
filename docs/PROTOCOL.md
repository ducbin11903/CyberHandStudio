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
