# Mô hình 3D từ CAD

App hiện bàn tay từ file CAD thay cho khối đơn giản (Visual → Mô hình 3D).
Mặc định: `models/opengrasp_lite` (từ `mechanics-main/HAND_Assembly_with_screws.STEP`).

## Tạo lại từ STEP

```
pip install cadquery-ocp numpy
python tools/cad/step_to_rig.py HAND_Assembly_with_screws.STEP out_dir
```

Trong app: Visual → Thư mục model → Mở… → chọn `out_dir`.

- `rig.json`: danh sách khâu, màu, khớp; `hand.bin`: lưới tam giác (mm, toạ độ CAD).
- Bỏ: vít, insert, PCB, motor trong lòng bàn tay, `rigid_link` (bản thay lò xo).
- Khớp lấy từ trục các chốt, dời chốt trong CAD thì khớp dời theo.

## Động học

- Ngón I/M/R/P: cơ cấu 4 khâu (paper 2408.02293, hình 2b). A = chốt MCP (motor), B = chốt PIP,
  C = chốt trên đầu ngón, D = chốt trên palm_connector. Góc khớp trong app = góc đốt 1 quanh A;
  đầu ngón (đốt 2+3, DIP cố định) tự tính để C luôn cách D một đoạn cố định.
- Ngón cái: T2 quay thumb_base quanh chốt của nó (dương = áp vào lòng bàn tay);
  T1 là cơ cấu 4 khâu tương tự trên thumb_base (dương = gập về phía các ngón).
- Góc 0 = tư thế trong file CAD. Góc ngoài tầm với của cơ cấu thì dừng ở vị trí xa nhất còn khép kín.
