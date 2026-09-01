# USB HID 键盘链路实现方案

| 项目 | 内容 |
| --- | --- |
| 文档版本 | V1.0 |
| 编制日期 | 2026-09-01 |
| 状态 | 待确认（仅方案，未写代码） |
| 当前工程 | `F:\ble_key_workspace\01_code` |
| 参考实现 | `F:\ble_key_workspace\keyboard`（usb_hid_transport.c / hid_scheduler.c / hid_report_maps.h） |

---

## 1. 需求

1. USB 枚举为**复合设备**：keyboard HID + consumer HID + CDC ACM；
2. 键盘支持 **Boot Protocol** 与 **NKRO Report Protocol**；
3. Consumer report 支持 **音量加减、静音**；
4. 主机 **LED output report** 转换为 `hid_led_event`；
5. 检查 descriptor、report size 与 `keyboard_core` 一致性；
6. **发送调度机制**：抑制按键连发/旋钮快速旋转产生的 HID 报告尖峰，交给 USB 输入层最终发送。

---

## 2. 总体架构（事件流）

```
按键/旋钮输入
   │  button_event / encoder_event
   ▼
keyboard_core.c        ← 已有：构建 HID 报告（NKRO/Boot），提交 hid_key_event / hid_consumer_event
   │  hid_key_event / hid_consumer_event
   ▼
hid_scheduler.c        ← 新增：发送调度层（FIFO + last-wins + 背压），抑制尖峰
   │  hid_report_to_send_event
   ▼
usb_hid_transport.c    ← 新增：USB 复合设备驱动（kbd + consumer + CDC ACM）
   │  usbd (USB Device Next Stack)
   ▼
USB 主机
```

反向路径（主机 → 键盘）：

```
USB 主机
   │  OUT report（LED）
   ▼
usb_hid_transport.c    ← kbd_set_report / kbd_output_report
   │  hid_led_event
   ▼
下游消费者（协议模块/屏幕/灯，本期只打日志）
```

---

## 3. 文件清单

### 3.1 新增文件

| 文件 | 内容 |
| --- | --- |
| `include/events/hid_led_event.h` + `src/events/hid_led_event.c` | 主机 LED 状态事件（`uint8_t led_state`） |
| `include/events/hid_report_to_send_event.h` + `.c` | 调度层→传输层：待发报告（`report[29]` + `report_size`） |
| `include/events/hid_report_sent_event.h` + `.c` | 传输层→调度层：上一报告已发送（背压释放） |
| `include/hid_report_maps.h` | USB/BLE 共用 HID Report Descriptor 与尺寸宏 |
| `src/hid_scheduler.c` | 发送调度层（抑制尖峰） |
| `src/usb_hid_transport.c` | USB 复合设备（kbd + consumer + CDC ACM） |

### 3.2 复用已有文件（当前工程已实现）

| 文件 | 状态 |
| --- | --- |
| `include/events/hid_key_event.h` + `src/events/hid_key_event.c` | ✅ 已有（29B report 缓冲） |
| `include/events/hid_consumer_event.h` + `.c` | ✅ 已有（16-bit usage） |
| `include/events/set_protocol_event.h` + `.c` | ✅ 已有（HID_PROTOCOL_BOOT/REPORT） |
| `src/app/keyboard_core.c` | ✅ 已有（构建 NKRO/Boot 报告，订阅 set_protocol_event） |
| `include/events/mode_event.h` + `src/events/mode_event.c` | ✅ 已有（mode 切换） |

### 3.3 修改文件

| 文件 | 修改 |
| --- | --- |
| `app.overlay`（或 board dts） | 增加 `hid_kbd`、`hid_consumer` 节点；启用 `&usbd` + `cdc_acm_uart0` |
| `prj.conf` | 增加 USB Device Next Stack / HID / CDC 配置 |
| `CMakeLists.txt` | 挂载新文件 |
| `keymap.c`（可选） | 订阅 `hid_led_event` 打印 LED 状态（便于 RTT 验证） |

---

## 4. 关键设计

### 4.1 HID Report 尺寸一致性（需求 5）

统一在 `include/hid_report_maps.h` 定义，USB / BLE / keyboard_core 共用：

```c
#define HID_KBD_BOOT_SIZE   8    /* Boot protocol: mod(1) + reserved(1) + keys(6) */
#define HID_KBD_NKRO_SIZE  29    /* NKRO: mod(1) + bitmap(28) */
#define HID_CONSUMER_SIZE   2    /* 16-bit consumer usage */
```

对照检查（与参考工程一致）：

| 项 | 值 | 来源 |
| --- | --- | --- |
| keyboard_core `hid_key_event_submit(REPORT, …, HID_NKRO_REPORT_SIZE)` | 29 | `keyboard_core.c` |
| keyboard_core `hid_key_event_submit(BOOT, …, HID_BOOT_REPORT_SIZE)` | 8 | `keyboard_core.c` |
| `hid_key_event.report[29]` 缓冲 | 29 | `hid_key_event.h`（当前工程已按参考实现） |
| hid_kbd DTS `in-report-size = <29>` | 29 | DTS 节点 |
| USB kbd report desc（NKRO bitmap） | 29B 无 Report ID | `hid_report_maps.h` |
| consumer `hid_consumer_event.usage`（uint16） | 2 | `hid_consumer_event.h` |
| hid_consumer DTS `in-report-size = <2>` | 2 | DTS 节点 |
| USB consumer report desc（16-bit Array） | 2B 无 Report ID | `hid_report_maps.h` |

> **一致**：29 / 8 / 2 三档尺寸在 core、event、DTS、descriptor 四处对齐。

### 4.2 复合设备（需求 1）

- 使用 Zephyr **USB Device Next Stack**（`CONFIG_USB_DEVICE_STACK_NEXT=y`）+ `usbd_hid` class + `usbd_cdc_acm` class。
- DTS 节点：

```dts
/ {
	hid_kbd: hid_kbd {
		compatible = "zephyr,hid-device";
		label = "HID_KBD";
		protocol-code = "keyboard";
		in-report-size = <29>;
		out-report-size = <29>;   /* LED output report 大小 */
		in-polling-period-us = <1000>;
		out-polling-period-us = <1000>;
	};

	hid_consumer: hid_consumer {
		compatible = "zephyr,hid-device";
		label = "HID_CONSUMER";
		protocol-code = "none";
		in-report-size = <2>;
		in-polling-period-us = <1000>;
	};
};

&usbd {
	status = "okay";
	cdc_acm_uart0: cdc_acm_uart0 {
		compatible = "zephyr,cdc-acm-uart";
		label = "CDC_ACM_0";
	};
};
```

- `usb_hid_transport.c` 注册：
  - `hid_device_register(_kbd_dev, usb_kbd_report_desc, …, &_kbd_ops)`；
  - `hid_device_register(_consumer_dev, usb_consumer_report_desc, …, &_consumer_ops)`；
  - CDC ACM 由 Zephyr `usbd_cdc_acm` class 自动注册（配置 `CONFIG_USBD_CDC_ACM_CLASS=y` + `CONFIG_SERIAL=y` + `CONFIG_UART_LINE_CTRL=y`）。
- VID/PID：`0x2FE3 / 0x0001`（与参考一致）。

### 4.3 Boot / NKRO 协议切换（需求 2）

- 主机发 `SET_PROTOCOL` → `kbd_set_protocol()` 回调 → `set_protocol_event_submit()` → keyboard_core 切换 `_protocol`（**当前工程已实现**）；
- keyboard_core 按协议构建不同尺寸报告：
  - Report Protocol → 29B NKRO（`hid_build_nkro_report`）；
  - Boot Protocol → 8B Boot（`hid_build_boot_report`）；
- USB 侧无需区分：两份 descriptor 由同一 `usb_kbd_report_desc` 承担（NKRO bitmap 无 Report ID，Boot 时主机只用前 8 字节）。

### 4.4 Consumer 音量/静音（需求 3）

- `keyboard_core.c` 已定义 usage：`CONSUMER_VOL_UP 0xE9 / VOL_DOWN 0xEA / MUTE 0xE2`；
- `hid_consumer_event.usage`（uint16）经调度层 → 2B report（little-endian）→ consumer interface；
- 旋钮映射：编码器 `steps>0 → VOL_UP`、`steps<0 → VOL_DOWN`（已在 keyboard_core 中实现）；旋钮按键 → MUTE（keymap 表 0x10E2）。

### 4.5 LED output report → hid_led_event（需求 4）

- `kbd_set_report()`（type==OUTPUT）与 `kbd_output_report()` 均调用 `kbd_handle_led()`；
- `kbd_handle_led()`：取 `buf[1]`（跳过 Report ID，若 len>=2），提交 `hid_led_event_submit(led)`；
- 订阅方（本期）：`keymap.c` 打日志 `leds=0x..`；后续可接屏幕/指示灯。

---

## 5. 发送调度机制（需求 6）—— hid_scheduler.c

### 5.1 设计目标

- 快速连按按键 / 旋钮快速旋转会产生**尖峰**的 `hid_key_event` / `hid_consumer_event`；
- USB 一次只能有一个 IN report 在途（`in_flight`），直接提交会**丢报告**；
- 调度层在 core 与 transport 之间做**速率限制 + 缓冲**。

### 5.2 策略（对齐参考工程）

| 通道 | 策略 |
| --- | --- |
| 键盘报告 | **last-wins**：单槽覆盖，永远发最新按键状态（键盘本质是状态，无需逐次发送） |
| Consumer 报告 | **16 深度 FIFO**：每个音量步进都保留（旋钮步进有语义，不能丢） |
| 发送优先级 | 键盘 > Consumer |
| 背压 | 等待 `hid_report_sent_event` 后才发送下一条（不会同时提交两个 IN report） |

### 5.3 数据结构

```c
static uint8_t  _kbd_report[29];   /* last-wins 槽 */
static uint8_t  _kbd_size;
static bool     _kbd_pending;
static uint16_t _cc_fifo[16];      /* consumer FIFO */
static uint8_t  _cc_head, _cc_count;
static bool     _sending;          /* 当前是否有报告在途 */
```

### 5.4 流程

```
hid_key_event ──▶ 覆盖 _kbd_report, _kbd_pending=true ──▶ try_flush()
hid_consumer_event ──▶ 入 _cc_fifo ──▶ try_flush()
                                        │
try_flush():
  if _sending: return                     // 在途，等 sent
  if _kbd_pending: 发 hid_report_to_send_event(_kbd_report)   // 键盘优先
  else if _cc_count: 发 hid_report_to_send_event(cc_fifo出队)
  _sending = true

hid_report_sent_event ──▶ _sending=false ──▶ try_flush()      // 背压释放
mode_event ──▶ 清空缓冲（模式切换时丢弃旧状态）
```

### 5.5 为什么能抑制尖峰

- 键盘连发：中间报告被 last-wins 覆盖，主机只收到最终状态（省 USB 带宽、防卡顿）；
- 旋钮快转：FIFO 逐个按步进发送，配合 1ms 轮询 + `in_flight` 背压，USB 侧自然限速，不丢步；
- FIFO 满（16）时丢弃最旧？—— 参考实现为**拒绝新入队**（防丢最新），可接受（旋钮快转过 16 步极罕见）。

---

## 6. prj.conf 配置

```kconfig
# USB Device Next Stack
CONFIG_USB_DEVICE_STACK_NEXT=y
CONFIG_USBD_HID_SUPPORT=y
CONFIG_USBD_HID_IN_BUF_COUNT=4
CONFIG_USBD_HID_OUT_BUF_COUNT=2

# CDC ACM（复合设备第三接口）
CONFIG_SERIAL=y
CONFIG_UART_LINE_CTRL=y
CONFIG_USBD_CDC_ACM_CLASS=y
```

> 说明：参考工程还依赖 MCUboot（`CONFIG_BOOTLOADER_MCUBOOT=y`）做 USB DFU，本期**不引入**，只做 USB HID 枚举。

---

## 7. CMakeLists.txt

```cmake
target_sources(app PRIVATE
  ...
  src/hid_scheduler.c
  src/usb_hid_transport.c
  src/events/hid_led_event.c
  src/events/hid_report_to_send_event.c
  src/events/hid_report_sent_event.c
)
```

---

## 8. 事件订阅关系总览

| 模块 | 订阅 | 发布 |
| --- | --- | --- |
| keyboard_core | button/encoder/set_protocol | hid_key / hid_consumer / funckey |
| hid_scheduler | hid_key / hid_consumer / hid_report_sent / mode | hid_report_to_send |
| usb_hid_transport | hid_report_to_send / mode | hid_report_sent / hid_led |
| keymap（日志） | button / hid_key / hid_consumer / hid_led | — |

---

## 9. 验证计划

1. **枚举**：接入电脑，`lsusb`/设备管理器看到 3 个接口（HID Keyboard、HID Consumer、CDC ACM）；
2. **按键**：按数字键 → 主机输入正确字符；
3. **NKRO**：同时按多键不冲突（默认 Report Protocol）；
4. **Boot**：BIOS 界面（主机发 SET_PROTOCOL Boot）→ 6 键无冲正常；
5. **音量**：旋钮顺/逆时针 → 音量 ±；旋钮按键 → 静音；
6. **LED**：按 CapsLock → RTT 打印 `leds=0x02`；NumLock → `leds=0x01`；
7. **尖峰**：快速连按某键 / 快速旋转旋钮，观察 RTT 无 "Drop report" 刷屏、主机无丢键丢步。

---

## 10. 风险与备注

- **USB 供电/枚举**：nRF52840 的 USB 引脚为专用引脚（无需 pinctrl），但需 VBUS 检测支持（`usbd_can_detect_vbus`）；当前 board dts 未启用 `&usbd`，需在 overlay 启用；
- **Boot Protocol**：Boot 报告只有 8B，键盘 core 已按协议构建，USB descriptor 无需切换（主机按协议解释）；
- **CDC ACM 保留（已确认）**：与 keyboard 一致，CDC ACM 是上下位机私有通信（protocol_module + frame_module + nanopb protobuf）的 USB 传输层，与 BLE NUS 共用 `device_tx_event`/协议层。**本期仅枚举出第三接口**（DTS 节点 + `CONFIG_USBD_CDC_ACM_CLASS=y`），完整数据链路（frame/protocol/nanopb）后续阶段与 keyboard 对齐实现；
- **FIFO 满策略（已确认）**：consumer FIFO 满时**拒绝入队（丢弃新事件）**，与参考 `hid_scheduler.c` 一致——积压时保最新语义、防延迟累积；
- **VID/PID（已确认）**：沿用 `0x2FE3 / 0x0001`（与 keyboard 一致，Nordic 厂商 ID）。调试时同插两块板会显示同名设备，按 USB 端口区分；
- **参考工程差异**：参考工程含 BLE HID + MCUboot DFU，本期只做 USB 部分，BLE 后续阶段接入（hid_report_maps.h 已预留 BLE 段）。

---

## 11. 实施顺序（确认后）

1. 新增 3 个事件（hid_led / hid_report_to_send / hid_report_sent）及 .c；
2. 新增 `hid_report_maps.h`（descriptor + 尺寸宏）；
3. `app.overlay` 增加 hid_kbd / hid_consumer 节点 + 启用 &usbd + cdc_acm；
4. `prj.conf` 增加 USB/HID/CDC 配置；
5. 新增 `hid_scheduler.c`（调度层）；
6. 新增 `usb_hid_transport.c`（复合设备驱动）；
7. CMakeLists 挂载，编译验证；
8. 烧录，按 §9 验证计划逐项验收。
