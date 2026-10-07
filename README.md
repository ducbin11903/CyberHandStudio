# CyberHand Studio

App PC điều khiển và mô phỏng bàn tay robot CyberHand. Giao diện theo tông app Gesture HW1.
C++17 + Qt 6 (QML, Quick 3D, SerialPort), chạy Windows / Linux.

## Tải bản chạy sẵn (Windows)

GitHub → Releases → **Latest build (main)** → tải `CyberHandStudio-win64.zip`, giải nén, chạy `CyberHandStudio.exe`.

## Chức năng

| Khu | Nội dung |
|---|---|
| Bảng trái | Communication (COM, baud, line ending, giao thức), Control (send rate, speed %, log rate, soft start/stop, giới hạn dòng), Motor (Enable/Disable/Home/Zero/Clear, góc khớp, duty, tốc độ, PID), Poses, Joints (cnt/90°, min/max), Macros, Visual |
| 3D | Bàn tay 6 khớp T2 T1 I M R P, khối đặc = đo được, bóng xanh = đích |
| Status | Driver, mode, homed, vị trí/đích/sai lệch, duty, tốc độ, lỗi DIAG/Quá dòng/KEY, current monitor (actual/peak/ngưỡng) |
| Text | Nạp CSV quỹ đạo, vẽ theo khớp, phát (Once/Loop/Bounce, tốc độ), AI tạo quỹ đạo từ mô tả |
| Timeline | Mỗi khớp một track, clip = đi tới góc đích với easing; kéo/dời/đổi độ dài; lưu JSON, xuất CSV |
| Controller | Map tay cầm (SDL2) → Motion / Position / Pose / Command; thử bằng chuột khi không có tay cầm |
| Stream | Nhận khung góc khớp qua UDP (JSON/CSV), hiệu chuẩn min/max, Mirror tới tay, dạng Bars/Graph |
| Live | Đồ thị log firmware theo thời gian thực |
| Console | Dòng chữ firmware, lệnh đã gửi, ô gửi lệnh có lịch sử |

Mọi thao tác quy về lệnh CLI hiện có của firmware (`e d x o g p t v k H z c s l h`). Xem `docs/PROTOCOL.md`.

## Thử không cần phần cứng

- Linux: `python3 tools/fw_sim.py` in ra `/dev/pts/N` → nhập vào ô COM Port → Connect.
- Windows: cài com0com (cặp COM ảo), `python tools/fw_sim.py --port COM21` (cần `pip install pyserial`), app mở COM20.
- Board MCB 2 MCU: `--g474 U6` và `--g474 U7` (2 tiến trình → app chế độ "2 cổng"), hoặc `--g474 U6 --bridge`
  (fw 0.8, 1 cổng điều khiển cả 6 khớp qua link UART). Xem docs/PROTOCOL.md.
- Stream: `python tools/stream_demo.py` rồi vào Stream → Start.

## Build

Windows (MSYS2 UCRT64):

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,qt6-base,qt6-declarative,qt6-quick3d,qt6-shadertools,qt6-serialport,qt6-svg,SDL2}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/CyberHandStudio.exe
```

Linux (Ubuntu 24.04): `qt6-base-dev qt6-declarative-dev qt6-quick3d-dev qt6-serialport-dev qt6-svg-dev libsdl2-dev` + các gói `qml6-module-*` tương ứng.

Tham số dòng lệnh: `--port <tên>`, `--baud`, `--mode text|timeline|controller|stream|live|console`, `--play <csv>`, `--run-macro "e;H;@waitfor HOME: 20000;g 45"`, `--config-dir`, `--screenshot <png>`.

## Cấu trúc

```
src/core/   backend C++: SerialLink, LineParser, Telemetry, HandController, Trajectory, Player,
            TimelineModel, StreamServer, GamepadInput, PoseLibrary, MacroRunner, LlmClient, ConfigStore
qml/        giao diện: Theme.qml (bảng màu), Icons.qml, components/, panels/, views/ (3D), modes/
tools/      fw_sim.py (firmware giả lập), stream_demo.py
tests/      unit test lõi (QtTest)
```

Font: Noto Sans (SIL OFL, xem `assets/fonts/OFL.txt`).
