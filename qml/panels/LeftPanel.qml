import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import CyberHand
import CyberHand.Core

// Left settings column: profile bar + Communication / Control / Motor / Joints / Macros / Visual.
Card {
    id: root
    property var hand: Core.hand
    readonly property var bench: hand.joints[hand.benchJoint]

    TextPrompt { id: prompt }
    FolderDialog {
        id: cadDialog
        title: qsTr("Thư mục mô hình 3D (rig.json)")
        onAccepted: {
            if (Core.loadCad(selectedFolder.toString())) {
                Core.setUiValue("cadFolder", selectedFolder.toString())
                Core.setUiValue("model3d", "cad")
            }
        }
    }
    ConfirmDialog { id: confirm }
    MacroEditor { id: macroEditor }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- profile bar -------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 2
            IconButton { iconPath: Icons.folder; tip: qsTr("Mở thư mục cấu hình"); onClicked: Core.config.openFolder() }
            FlatCombo {
                id: profileCombo
                Layout.fillWidth: true
                Layout.minimumWidth: 90
                model: Core.config.profiles
                currentIndex: Core.config.profiles.indexOf(Core.config.current)
                onActivated: (i) => Core.config.load(textAt(i))
            }
            IconButton { iconPath: Icons.save; tip: qsTr("Lưu cấu hình"); onClicked: Core.config.save() }
            IconButton {
                iconPath: Icons.copy
                tip: qsTr("Lưu thành cấu hình mới")
                onClicked: {
                    prompt.accepted.disconnect(root.saveAsNew)
                    prompt.accepted.connect(root.saveAsNew)
                    prompt.ask(qsTr("Tên cấu hình mới"), Core.config.current + "_copy")
                }
            }
            IconButton {
                iconPath: Icons.trash
                tip: qsTr("Xóa cấu hình")
                enabled: Core.config.profiles.length > 1
                onClicked: confirm.ask(qsTr("Xóa cấu hình \"%1\"?").arg(Core.config.current),
                                       function() { Core.config.remove(Core.config.current); if (Core.config.profiles.length) Core.config.load(Core.config.profiles[0]) },
                                       qsTr("Xóa"))
            }
            Item { Layout.preferredWidth: 4 }
            FlatButton {
                Layout.preferredWidth: 104
                accent: !Core.link.connected
                text: Core.link.connected ? qsTr("Disconnect") : qsTr("Connect")
                iconPath: Icons.plug
                onClicked: Core.link.toggle()
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: col.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: FlatScrollBar {}

            ColumnLayout {
                id: col
                width: flick.width
                spacing: 1

                // ---- Communication ----------------------------------------
                Section {
                    title: qsTr("Communication")
                    FormRow {
                        label: qsTr("COM Port")
                        FlatCombo {
                            id: portCombo
                            Layout.fillWidth: true
                            editable: true
                            model: Core.link.ports
                            enabled: !Core.link.connected
                            Component.onCompleted: editText = Core.link.portName
                            Connections {
                                target: Core.link
                                function onPortNameChanged() { if (portCombo.editText !== Core.link.portName) portCombo.editText = Core.link.portName }
                            }
                            onActivated: (i) => Core.link.portName = textAt(i)
                            onAccepted: Core.link.portName = editText
                            onEditTextChanged: if (!Core.link.connected && editText.length) Core.link.portName = editText
                        }
                        IconButton { iconPath: Icons.refresh; tip: qsTr("Quét lại cổng"); onClicked: Core.link.refreshPorts() }
                    }
                    FormRow {
                        label: qsTr("Baud")
                        FlatCombo {
                            id: baudCombo
                            Layout.fillWidth: true
                            editable: true
                            model: ["9600", "57600", "115200", "230400", "460800", "921600"]
                            Component.onCompleted: editText = "" + Core.link.baudRate
                            onActivated: (i) => Core.link.baudRate = parseInt(textAt(i))
                            onAccepted: Core.link.baudRate = parseInt(editText)
                        }
                    }
                    FormRow {
                        label: qsTr("Line ending")
                        FlatCombo {
                            Layout.fillWidth: true
                            model: ["LF  \\n", "CR  \\r", "CR+LF"]
                            currentIndex: Core.link.lineEnding
                            onActivated: (i) => Core.link.lineEnding = i
                        }
                    }
                    FormRow {
                        label: qsTr("Protocol")
                        FlatCombo {
                            Layout.fillWidth: true
                            model: Core.ctrl.protocolNames
                            currentIndex: Core.ctrl.protocol
                            onActivated: (i) => Core.ctrl.protocol = i
                        }
                    }
                    FormRow {
                        label: qsTr("Khớp chính")
                        FlatCombo {
                            id: benchCombo
                            Layout.fillWidth: true
                            // G474: joints of the other MCU are marked
                            model: root.hand.names.map(function(n, i) {
                                return root.hand.joints[i].offPort ? n + qsTr("  — chưa kết nối") : n
                            })
                            currentIndex: root.hand.benchJoint
                            // a new model resets the index: bind it to the main joint again
                            onModelChanged: Qt.callLater(function() {
                                benchCombo.currentIndex = Qt.binding(function() { return root.hand.benchJoint })
                            })
                            onActivated: (i) => root.hand.benchJoint = i
                        }
                    }
                    TextLabel {
                        Layout.fillWidth: true
                        visible: Core.ctrl.portKeys.length > 0
                        font.pixelSize: Theme.fontSmall
                        secondary: true
                        text: qsTr("Cổng %1 · khớp %2 · motor đang chọn: %3")
                              .arg(Core.ctrl.portMcu.length ? Core.ctrl.portMcu : "MCU")
                              .arg(Core.ctrl.portKeys.join(", "))
                              .arg(Core.ctrl.selectedKey.length ? Core.ctrl.selectedKey : "—")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Led { on: Core.link.connected && Core.link.lastLineAgeMs >= 0 && Core.link.lastLineAgeMs < 1500 }
                        TextLabel {
                            Layout.fillWidth: true
                            secondary: true
                            font.pixelSize: Theme.fontSmall
                            color: Core.link.errorString.length && !Core.link.connected ? Theme.liveRed : Theme.textSecondary
                            text: {
                                if (!Core.link.connected)
                                    return Core.link.errorString.length ? Core.link.errorString : qsTr("Chưa kết nối")
                                var age = Core.link.lastLineAgeMs
                                return qsTr("RX %1 B/s · %2 dòng/s · dòng cuối %3")
                                    .arg(Math.round(Core.link.rxBytesPerSec))
                                    .arg(Core.link.linesPerSec.toFixed(1))
                                    .arg(age < 0 ? "—" : (age > 9999 ? ">10 s" : age + " ms"))
                            }
                        }
                    }
                }

                // ---- Control ------------------------------------------------
                Section {
                    title: qsTr("Control")
                    FormRow {
                        label: qsTr("Motion Send Rate")
                        NumberField {
                            Layout.preferredWidth: 90
                            value: Core.player.sendRate; decimals: 0; from: 1; to: 200; unit: "Hz"
                            onCommitted: (v) => { Core.player.sendRate = v; Core.stream.sendRate = v; Core.gamepad.sendRate = v }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Speed Percentage")
                        NumberField {
                            Layout.preferredWidth: 90
                            value: Core.ctrl.dutyLimit; decimals: 0; from: 0; to: 100; unit: "%"
                            onCommitted: (v) => Core.ctrl.dutyLimit = v
                        }
                        FlatButton { text: qsTr("Update"); tip: qsTr("Gửi  x <pct>"); enabled: Core.ctrl.benchOnPort; onClicked: Core.ctrl.applyDutyLimit() }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Position Sample Rate")
                        NumberField {
                            Layout.preferredWidth: 90
                            value: Core.ctrl.logRate; decimals: 0; from: 0; to: 100; unit: "Hz"
                            onCommitted: (v) => Core.ctrl.logRate = v
                        }
                        FlatButton { text: qsTr("Update"); tip: qsTr("Gửi  l <hz>"); onClicked: Core.ctrl.applyLogRate() }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Soft Start")
                        FlatCheck { checked: Core.player.softStart; onToggled: Core.player.softStart = checked }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Soft Stop")
                        FlatCheck { checked: Core.player.softStop; onToggled: Core.player.softStop = checked }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Power Sample Rate")
                        NumberField {
                            Layout.preferredWidth: 90
                            value: Core.ctrl.powerRate; decimals: 0; from: 1; to: 1000; unit: "Hz"
                            onCommitted: (v) => Core.ctrl.powerRate = v
                        }
                        FlatButton { text: qsTr("Update"); tip: qsTr("Gửi  a <hz>: firmware lấy trung bình dòng trong 1/hz giây"); enabled: Core.ctrl.benchOnPort; onClicked: Core.ctrl.applyPowerRate() }
                        Item { Layout.fillWidth: true }
                    }
                }

                // ---- Safety (over-current) --------------------------------
                Section {
                    title: qsTr("Safety")
                    FormRow {
                        label: qsTr("Current Limit") + (root.bench ? " · " + root.bench.key : "")
                        NumberField {
                            Layout.preferredWidth: 90
                            value: root.bench ? root.bench.currentLimitMa : 300; decimals: 0; from: 1; to: 5000; unit: "mA"
                            onCommitted: (v) => { if (root.bench) root.bench.currentLimitMa = v }
                        }
                        TextLabel { text: qsTr("khớp khác: bảng Joints"); muted: true; font.pixelSize: Theme.fontTiny; Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Khi quá dòng")
                        FlatCombo {
                            Layout.fillWidth: true
                            model: Core.telemetry.responseNames
                            currentIndex: Core.telemetry.overResponse
                            onActivated: (i) => Core.telemetry.overResponse = i
                        }
                    }
                    FormRow {
                        label: qsTr("Lùi / giữ ngưỡng")
                        NumberField {
                            Layout.preferredWidth: 80
                            value: Core.telemetry.retractDeg; decimals: 1; from: 0.5; to: 90; unit: "°"
                            onCommitted: (v) => Core.telemetry.retractDeg = v
                        }
                        NumberField {
                            Layout.preferredWidth: 80
                            value: Core.telemetry.tripMs; decimals: 0; from: 1; to: 5000; unit: "ms"
                            onCommitted: (v) => Core.telemetry.tripMs = v
                        }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Xử lý tại")
                        SegmentToggle {
                            options: [qsTr("App"), qsTr("Firmware")]
                            currentIndex: Core.telemetry.overSource
                            onActivated: (i) => Core.telemetry.overSource = i
                        }
                        FlatButton {
                            text: qsTr("Ghi FW")
                            enabled: Core.ctrl.benchOnPort
                            tip: qsTr("Gửi  I <mA> <ms> <kiểu> <độ>  (firmware ≥ 0.5)")
                            onClicked: Core.pushProtection()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    TextLabel {
                        Layout.fillWidth: true
                        font.pixelSize: Theme.fontTiny
                        muted: true
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                        text: Core.telemetry.overSource === 0
                              ? qsTr("App: phản ứng qua serial (~50 Hz), dừng cả phát / stream / controller.")
                              : qsTr("Firmware: phản ứng ở 1 kHz sau %1 ms quá ngưỡng, báo về dòng OVERI:. Tự ghi khi kết nối.").arg(Math.round(Core.telemetry.tripMs))
                    }
                }

                // ---- Motor (bench) ----------------------------------------
                Section {
                    title: qsTr("Motor")
                    TextLabel {
                        Layout.fillWidth: true
                        visible: Core.ctrl.benchHint.length > 0
                        text: Core.ctrl.benchHint + qsTr(" · các lệnh 1 khớp bị khóa")
                        color: Theme.liveRed
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 4
                        rowSpacing: 4
                        readonly property string allTip: Core.ctrl.portKeys.length ? qsTr(" (cả %1)").arg(Core.ctrl.portKeys.join(", ")) : ""
                        FlatButton { Layout.fillWidth: true; text: qsTr("Enable"); iconPath: Icons.power; tip: "e" + parent.allTip; onClicked: Core.ctrl.enable() }
                        FlatButton { Layout.fillWidth: true; text: qsTr("Disable"); danger: true; tip: "d" + parent.allTip; onClicked: Core.ctrl.disable() }
                        FlatButton { Layout.fillWidth: true; text: qsTr("Home"); iconPath: Icons.home; tip: "H [pct] [mA]"; enabled: Core.ctrl.benchOnPort; onClicked: Core.ctrl.home() }
                        FlatButton { Layout.fillWidth: true; text: qsTr("Zero"); tip: "z"; enabled: Core.ctrl.benchOnPort; onClicked: Core.ctrl.zero() }
                        FlatButton { Layout.fillWidth: true; text: qsTr("Clear fault"); tip: "c" + parent.allTip; onClicked: Core.ctrl.clearFault() }
                        FlatButton { Layout.fillWidth: true; text: qsTr("Status"); tip: "s"; enabled: Core.ctrl.benchOnPort; onClicked: Core.ctrl.status() }
                    }
                    FormRow {
                        enabled: Core.ctrl.benchOnPort
                        label: qsTr("Home duty / dòng")
                        NumberField {
                            Layout.preferredWidth: 70
                            value: Core.ctrl.homePct; decimals: 0; from: 0; to: 100; unit: "%"
                            onCommitted: (v) => Core.ctrl.homePct = v
                        }
                        NumberField {
                            Layout.preferredWidth: 80
                            value: Core.ctrl.homeMa; decimals: 0; from: 0; to: 5000; unit: "mA"
                            onCommitted: (v) => Core.ctrl.homeMa = v
                        }
                        Item { Layout.fillWidth: true }
                    }
                    // joint angle (PID)
                    FormRow {
                        label: qsTr("Góc khớp") + (root.bench ? " · " + root.bench.key : "")
                        FlatSlider {
                            id: angleSlider
                            Layout.fillWidth: true
                            from: root.bench ? root.bench.minDeg : 0
                            to: root.bench ? root.bench.maxDeg : 90
                            stepSize: 0.5
                            fillColor: Theme.jointColor(root.hand.benchJoint)
                            value: root.bench ? root.bench.commandDeg : 0
                            onMoved: if (liveCheck.checked) liveTimer.restart()
                            onPressedChanged: if (!pressed) Core.ctrl.moveJoint(root.hand.benchJoint, value)
                            Timer { id: liveTimer; interval: 60; onTriggered: Core.ctrl.moveJoint(root.hand.benchJoint, angleSlider.value) }
                        }
                        NumberField {
                            Layout.preferredWidth: 64
                            value: angleSlider.value; decimals: 1; from: 0; to: 180; unit: "°"
                            onCommitted: (v) => Core.ctrl.moveJoint(root.hand.benchJoint, v)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.labelWidth + 6
                        FlatCheck { id: liveCheck; text: qsTr("Gửi khi kéo"); checked: false }
                        Item { Layout.fillWidth: true }
                        FlatButton { text: "0°"; onClicked: Core.ctrl.moveJoint(root.hand.benchJoint, 0) }
                        FlatButton { text: "45°"; onClicked: Core.ctrl.moveJoint(root.hand.benchJoint, 45) }
                        FlatButton { text: "90°"; onClicked: Core.ctrl.moveJoint(root.hand.benchJoint, 90) }
                    }
                    // open loop duty
                    FormRow {
                        enabled: Core.ctrl.benchOnPort
                        label: qsTr("Duty vòng hở")
                        FlatSlider {
                            id: dutySlider
                            Layout.fillWidth: true
                            from: -100; to: 100; stepSize: 1
                            centered: true
                            value: Core.telemetry.hasData ? Core.telemetry.duty : 0
                            onPressedChanged: if (!pressed) Core.ctrl.setDuty(value)
                        }
                        ValueBox { Layout.preferredWidth: 50; text: Math.round(dutySlider.value) + "%" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.labelWidth + 6
                        spacing: 4
                        enabled: Core.ctrl.benchOnPort
                        FlatButton { text: "−5"; tip: "-"; onClicked: Core.ctrl.dutyDown() }
                        FlatButton { text: "0"; tip: "0"; onClicked: Core.ctrl.stopOpenLoop() }
                        FlatButton { text: "+5"; tip: "+"; onClicked: Core.ctrl.dutyUp() }
                        FlatButton { text: qsTr("Đảo"); tip: "r"; onClicked: Core.ctrl.reverse() }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        enabled: Core.ctrl.benchOnPort
                        label: qsTr("Tốc độ khớp")
                        NumberField {
                            id: velField
                            Layout.preferredWidth: 90
                            value: Core.ctrl.velocity; decimals: 1; from: -500; to: 500; unit: "°/s"
                            onCommitted: (v) => Core.ctrl.velocity = v
                        }
                        FlatButton { text: qsTr("Chạy"); tip: "v <deg/s>"; onClicked: Core.ctrl.runVelocity(Core.ctrl.velocity) }
                        FlatButton { text: qsTr("Giữ"); tip: "v 0"; onClicked: Core.ctrl.hold() }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        enabled: Core.ctrl.benchOnPort
                        label: qsTr("PID kp / ki / kd")
                        NumberField { Layout.fillWidth: true; value: Core.ctrl.kp; decimals: 6; from: 0; to: 100; onCommitted: (v) => Core.ctrl.kp = v }
                        NumberField { Layout.fillWidth: true; value: Core.ctrl.ki; decimals: 6; from: 0; to: 100; onCommitted: (v) => Core.ctrl.ki = v }
                        NumberField { Layout.fillWidth: true; value: Core.ctrl.kd; decimals: 7; from: 0; to: 100; onCommitted: (v) => Core.ctrl.kd = v }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.labelWidth + 6
                        FlatButton { text: qsTr("Apply PID"); tip: "k kp ki kd"; enabled: Core.ctrl.benchOnPort; onClicked: Core.ctrl.applyPid() }
                        Item { Layout.fillWidth: true }
                        FlatButton { text: qsTr("Help"); tip: "h"; onClicked: Core.ctrl.help() }
                    }
                }

                // ---- Poses --------------------------------------------------
                Section {
                    title: qsTr("Poses")
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: Core.poses
                            FlatButton { text: model.name; onClicked: Core.poses.apply(index) }
                        }
                        FlatButton {
                            iconPath: Icons.plus
                            tip: qsTr("Lưu tư thế hiện tại thành pose")
                            onClicked: {
                                prompt.accepted.disconnect(root.savePose)
                                prompt.accepted.connect(root.savePose)
                                prompt.ask(qsTr("Tên pose"), "Pose " + (Core.poses.count + 1))
                            }
                        }
                    }
                }

                // ---- Joints ---------------------------------------------------
                Section {
                    title: qsTr("Joints")
                    expanded: false
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        TextLabel { text: ""; Layout.preferredWidth: 26 }
                        TextLabel { text: qsTr("Tên"); secondary: true; Layout.fillWidth: true; font.pixelSize: Theme.fontSmall }
                        TextLabel { text: qsTr("cnt/90°"); secondary: true; Layout.preferredWidth: 58; font.pixelSize: Theme.fontSmall }
                        TextLabel { text: qsTr("cnt/vòng"); secondary: true; Layout.preferredWidth: 62; font.pixelSize: Theme.fontSmall }
                        TextLabel { text: qsTr("min°"); secondary: true; Layout.preferredWidth: 44; font.pixelSize: Theme.fontSmall }
                        TextLabel { text: qsTr("max°"); secondary: true; Layout.preferredWidth: 44; font.pixelSize: Theme.fontSmall }
                        TextLabel { text: qsTr("mA"); secondary: true; Layout.preferredWidth: 48; font.pixelSize: Theme.fontSmall }
                    }
                    Repeater {
                        model: root.hand.joints
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Rectangle { width: 8; height: 8; radius: 4; color: Theme.jointColor(index) }
                            TextLabel { text: modelData.key; Layout.preferredWidth: 14; font.pixelSize: Theme.fontSmall; secondary: true }
                            FlatField { Layout.fillWidth: true; text: modelData.name; onEditingFinished: modelData.name = text }
                            NumberField { Layout.preferredWidth: 58; value: modelData.countsPer90; decimals: 1; from: -100000; to: 100000; onCommitted: (v) => modelData.countsPer90 = v }
                            NumberField { Layout.preferredWidth: 62; value: modelData.countsPerRev; decimals: 0; from: 1; to: 10000000; onCommitted: (v) => modelData.countsPerRev = v }
                            NumberField { Layout.preferredWidth: 44; value: modelData.minDeg; decimals: 0; from: -180; to: 180; onCommitted: (v) => modelData.minDeg = v }
                            NumberField { Layout.preferredWidth: 44; value: modelData.maxDeg; decimals: 0; from: -180; to: 180; onCommitted: (v) => modelData.maxDeg = v }
                            NumberField { Layout.preferredWidth: 48; value: modelData.currentLimitMa; decimals: 0; from: 1; to: 5000; onCommitted: (v) => modelData.currentLimitMa = v }
                        }
                    }
                }

                // ---- Macros ---------------------------------------------------
                Section {
                    title: qsTr("Macros")
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        FlatCombo {
                            id: macroCombo
                            Layout.fillWidth: true
                            model: Core.macros
                            textRole: "name"
                        }
                        IconButton { iconPath: Icons.pencil; tip: qsTr("Sửa"); onClicked: macroEditor.edit(macroCombo.currentIndex) }
                        IconButton { iconPath: Icons.plus; tip: qsTr("Macro mới"); onClicked: macroEditor.edit(-1) }
                        IconButton {
                            iconPath: Icons.trash; tip: qsTr("Xóa")
                            enabled: macroCombo.currentIndex >= 0
                            onClicked: confirm.ask(qsTr("Xóa macro \"%1\"?").arg(macroCombo.currentText), function() { Core.macros.remove(macroCombo.currentIndex) }, qsTr("Xóa"))
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        FlatButton {
                            text: Core.macros.running ? qsTr("Stop") : qsTr("Run")
                            iconPath: Core.macros.running ? Icons.stop : Icons.play
                            iconFilled: true
                            accent: !Core.macros.running
                            onClicked: Core.macros.running ? Core.macros.stop() : Core.macros.run(macroCombo.currentIndex)
                        }
                        TextLabel {
                            Layout.fillWidth: true
                            secondary: true
                            font.pixelSize: Theme.fontSmall
                            text: Core.macros.running ? Core.macros.runningName + " · " + qsTr("dòng %1").arg(Core.macros.currentLine) + " · " + Core.macros.status : Core.macros.status
                        }
                    }
                }

                // ---- Visual ---------------------------------------------------
                Section {
                    title: qsTr("Visual")
                    FormRow {
                        label: qsTr("Bóng mờ đích (tgt)")
                        FlatCheck { checked: Core.ui.showGhost !== false; onToggled: Core.setUiValue("showGhost", checked) }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Lưới sàn")
                        FlatCheck { checked: Core.ui.showFloor !== false; onToggled: Core.setUiValue("showFloor", checked) }
                        Item { Layout.fillWidth: true }
                    }
                    FormRow {
                        label: qsTr("Mô hình 3D")
                        FlatCombo {
                            Layout.fillWidth: true
                            model: [Core.cad.loaded ? "CAD · " + Core.cad.name : qsTr("CAD (chưa nạp)"), qsTr("Khối đơn giản")]
                            currentIndex: Core.ui.model3d === "blocks" || !Core.cad.loaded ? 1 : 0
                            onActivated: (i) => Core.setUiValue("model3d", i === 1 ? "blocks" : "cad")
                        }
                    }
                    FormRow {
                        label: qsTr("Thư mục model")
                        FlatButton {
                            text: qsTr("Mở…")
                            iconPath: Icons.folder
                            tip: qsTr("Thư mục có rig.json + hand.bin (tools/cad/step_to_rig.py)")
                            onClicked: cadDialog.open()
                        }
                        FlatButton {
                            text: qsTr("Mặc định")
                            enabled: (Core.ui.cadFolder || "").length > 0
                            onClicked: { Core.setUiValue("cadFolder", ""); Core.loadCad(""); Core.setUiValue("model3d", "cad") }
                        }
                        TextLabel {
                            Layout.fillWidth: true
                            muted: true
                            font.pixelSize: Theme.fontTiny
                            text: Core.cad.loaded ? Math.round(Core.cad.triangles / 1000) + "k tam giác" : Core.cad.lastError
                        }
                    }
                    FormRow {
                        label: qsTr("Visual Refresh Rate")
                        NumberField {
                            Layout.preferredWidth: 90
                            value: Core.refreshHz; decimals: 0; from: 1; to: 144; unit: "Hz"
                            onCommitted: (v) => Core.refreshHz = v
                        }
                        TextLabel { text: qsTr("đồ thị + 3D"); muted: true; font.pixelSize: Theme.fontTiny; Layout.fillWidth: true }
                    }
                }
                Item { Layout.preferredHeight: 8 }
            }
        }
    }

    function saveAsNew(name) { Core.config.saveAs(name) }
    function savePose(name) { Core.poses.capture(name); Core.saveNow() }
}
