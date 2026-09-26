# Giao thức và định dạng file

## Serial (firmware bench H562, FW_H5 V1.7.0)

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

Dòng log: `en:1,duty:0,out:42.0,i_mA:150,flt:0,diag:1,pos:700,tgt:729,vel:350,mode:1`.
Dòng chữ (help, `OK`, `ERR`, `HOME:`, `ID:`) không bắt đầu bằng số. `HOME: done` → Homed = Yes.
`ID: ... joints=N cnt90=937` (nếu firmware có) → app tự đặt cnt/90° cho khớp bench.

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
{"format":"cyberhand-timeline-1","length":6,
 "clips":[{"track":"I","start":0.8,"duration":1.2,"target":55,"easing":"smooth"}]}
```

easing: `linear`, `smooth`, `easein`, `easeout`, `step`. Clip đi từ giá trị hiện có của khớp tới `target`.

## Stream (UDP, mặc định cổng 7400)

Mỗi gói là một khung: `{"q":[0,0,45,90,90,90]}`, `{"I":45,"M":90}`, `0,0,45,90,90,90` hoặc `I:45,M:90`.
Đơn vị chọn trong app: độ, % hành trình, hoặc thô + hiệu chuẩn (Start → cử động hết biên → Save).

## Macro

Mỗi dòng là một lệnh CLI, hoặc: `@wait <ms>`, `@waitfor <chuỗi> [ms]`, `@repeat <N>` … `@end`, `@pose <tên>`, `@log <chữ>`, `# comment`.
