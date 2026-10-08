# Trạng thái phát triển

Cập nhật: 2026-09-27

## Đã xong

- Khung app Qt6 Quick, theme theo Gesture HW1 (`qml/Theme.qml`), font Noto Sans.
- Serial + parser log/dòng chữ + console + bảng Status + current monitor.
- Bảng điều khiển (Enable/Disable/Home/Zero/Clear, góc, duty, tốc độ, PID, log rate, duty limit).
- 3D bàn tay 6 khớp bằng khối, bóng đích, xoay camera.
- Quỹ đạo: CSV, Player (Once/Loop/Bounce, tốc độ, soft start/stop), Text mode, Timeline editor.
- Stream UDP (JSON/CSV, hiệu chuẩn, mirror, Bars/Graph), Controller (SDL2 + mô phỏng chuột), AI tạo quỹ đạo.
- Cấu hình dạng profile JSON, poses, macros.
- Firmware giả lập `tools/fw_sim.py`, unit test lõi, CI Windows (GitHub Actions → release "latest").
- Đã thử với fw_sim: home → PID → đồ thị; phát CSV bench; console lỗi/OK; 6 khớp + Stream mirror (`G q1..q6`); tay cầm mô phỏng; AI qua API giả.
- Bảng Status theo "Khớp chính" cả khi log 6 khớp.

### 0.2.0 (tham khảo Gesture HW1)

- Timeline: thư viện clip (Linear: Hold / Ramp up / Ramp down / Move, Wave: Sine / Square / Triangle / Sawtooth, Power: In / Out / In-Out / Out-In, Custom), kéo thả vào làn; Clip Details (Type, Axis, Start/End/Duration, tham số đường cong, công thức, đồ thị); thanh tổng quan; nhân bản (Ctrl+D).
- Manual Control: 6 slider dọc, góc lệnh / đo, dòng từng khớp, Total Current Draw.
- Safety: ngưỡng dòng từng khớp, phản ứng Cảnh báo / Tắt driver / Lùi (RETR) / Giữ, xử lý tại App hoặc Firmware (`I`).
- Calibration: quét 2 chặn cơ khí (H → o +duty tới khi kẹt) hoặc ghi điểm tay, fit tuyến tính + R², Áp dụng → cnt/90° + `j`.
- Stream Input Processing: Handed, Sampling Rate, Filter Frequency, Averaging, Max Velocity, Max Acceleration.
- Đèn trạng thái OFFLINE / IDLE / READY / RUN / FAULT, Data Monitor (mọi field + min/max, bảng khớp), Visual Refresh Rate, Power Sample Rate (`a`).
- Mô hình 3D từ STEP (OpenGrasp-Lite): 33 mesh, ~264k tam giác, động học 4 khâu cho 4 ngón và ngón cái (docs/MODEL3D.md).
- Firmware H562 fw 0.5: `i`, `j`, `I`, `a`, dòng `OVERI:`; đã thử logic trên máy (stub HAL) và fw_sim.

### 0.3.0

- Firmware MCB G474 (fw 0.7-g474): đọc `mcu=` / `keys=` trong ID, tự chọn "Từng khớp", `m <khớp chính>`,
  đẩy `j` (và `I`) cho từng motor của MCU, khóa lệnh 1 khớp khi khớp chính ở MCU kia, khớp MCU kia hiện "chưa kết nối".
  fw_sim `--g474 U6|U7`.

### 0.4.0 — 2 MCU

- Chế độ 2 cổng (U6 + U7 cùng lúc): định tuyến lệnh theo khớp, gộp log thành 6 khớp, console gắn nguồn + lọc + chọn đích,
  `@U7 <lệnh>`, đồng bộ `j`/`I` từng cổng, cổng 2 lưu trong profile, tùy chọn dòng lệnh `--port2`.
- Chế độ 1 cổng qua link UART (fw 0.8-g474): ID `keys=` 6 khớp + `link=U7`, bỏ tiền tố `[U7] `, `LINK: up/lost`, `lk`,
  flt 8 = LINK (chip LINK), khớp MCU kia khóa khi mất link và được đồng bộ lại khi link lên.
- fw_sim `--g474 U6 --bridge` (lệnh `link on|off`). Macro cho phép dòng `@U7 ...`.

### 0.5.0 — cảm biến bo mạch (fw 0.9)

- Ô Board (nhiệt độ, độ ẩm, VBAT, % pin LiPo, tuổi dữ liệu, sparkline 10 phút + vạch ngưỡng), màu cảnh báo / báo động.
- Banner OTEMP / VBAT, khóa Enable kèm lý do, "Đã an toàn — bấm Enable để chạy lại"; `sense` sau ID và nút làm mới.
- Bit flt mới: 4 ESTOP, 8 LINK, 16 OTEMP, 32 VBAT. Event log (Data Monitor). Trục Y phụ trong Live. Ghi CSV.
- fw_sim: temp/rh/vbat, `sense`, OTEMP/VBAT, `simtemp/simvbat/simsht`, `--temp/--vbat`.

### 0.5.1

- T1 = Thumb Rotation, T2 = Thumb Flexion (tên khớp, mô hình 3D, mẫu timeline/CSV, pose Pinch, tay cầm, AI); profile cũ tự đổi tên.
- Disconnect (và thoát app): dừng phát, gửi `D` (G474) hoặc `0` + `d` (bench) tới mọi cổng trước khi đóng.
- Mô phỏng khi đang kết nối: Output tắt (Timeline/Text/Manual/Controller) hoặc Stream không Mirror → 3D chạy theo lệnh,
  bóng xanh = tay thật.

## Giả định cần Duc xác nhận

- (đã xác nhận) T1 = xoay ngón cái (Thumb Rotation, opposition), T2 = gập ngón cái (Thumb Flexion).
- Bench H562 map vào khớp Index.
- Hệ số khớp PIP/DIP của mô hình 3D (0.8 / 0.4 so với MCP) chỉ để hiển thị.

## Việc tiếp theo

- Duc build + nạp firmware fw 0.5, thử trên bench H562 thật (COM8): Calibration Start cho Index, Safety = Firmware + Lùi.
- Pin %, LED demo của Gesture: chưa áp dụng (bench không có pin / LED RGB).

- Mesh bàn tay thật từ CAD (glTF) thay khối.
- Firmware MCB: log `q1..q6`, lệnh `g <j> <độ>` / `G ...`, USB CDC.
- Bảng tra count → góc nhiều điểm nếu quan hệ không tuyến tính.
