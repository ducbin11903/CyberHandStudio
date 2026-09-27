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

## Giả định cần Duc xác nhận

- T2 = xoay ngón cái (opposition), T1 = gập ngón cái.
- Bench H562 map vào khớp Index.
- Hệ số khớp PIP/DIP của mô hình 3D (0.8 / 0.4 so với MCP) chỉ để hiển thị.

## Việc tiếp theo

- Duc thử trên bench H562 thật (COM8), báo lỗi/ảnh chụp.

- Mesh bàn tay thật từ CAD (glTF) thay khối.
- Firmware MCB: log `q1..q6`, lệnh `g <j> <độ>` / `G ...`, USB CDC.
- Bảng tra count → góc nhiều điểm nếu quan hệ không tuyến tính.
