# USB HID 实现复盘：踩坑记录 / 数据流 / 重难点

| 项目 | 内容 |
| --- | --- |
| 文档版本 | V1.0 |
| 编制日期 | 2026-09-01 |
| 工程 | `F:\ble_key_workspace\01_code` |
| 方案文档 | 见 `src/doc/USB HID.md`（实现前方案） |
| 本文档 | 实现后复盘：踩坑、数据流、重难点 |

---

## 1. 总体数据流

### 1.1 输入方向（按键/旋钮 → USB 主机）

```
矩阵按键 / 旋钮
   │  CAF buttons（行列扫描+去抖） / QDEC sensor
   ▼
button_event / encoder_event
   │
   ▼
keyboard_core.c          ← 键状态(29B bitset) + HID 报告构建(NKRO/Boot) + Consumer usage
   │  hid_key_event{protocol, report[29], size} / hid_consumer_event{usage}
   ▼
hid_scheduler.c          ← 发送调度层：键盘 last-wins + Consumer 16 深环形 FIFO + 背压
   │  hid_report_to_send_event{report[29], size}
   ▼
usb_hid_transport.c      ← 按 size 路由：2B→consumer 接口，8/29B→keyboard 接口
   │  hid_device_submit_report()
   ▼
usbd (USB Device Next) → USB 主机
```

### 1.2 反向方向（USB 主机 → 键盘）

```
USB 主机
   │  OUT report（LED output report）
   ▼
usb_hid_transport.c
   │  kbd_set_report(type=OUTPUT) / kbd_output_report
   ▼
kbd_handle_led()  →  hid_led_event{led_state}
   ▼
下游消费者（当前 keymap.c 打日志；后续接屏幕/指示灯）
```

### 1.3 协议切换方向

```
USB 主机
   │  SET_PROTOCOL（Boot / Report）
   ▼
usb_hid_transport.c  kbd_set_protocol()
   │  set_protocol_event{protocol}
   ▼
keyboard_core.c  _protocol 切换 → 后续报告按 8B(Boot) / 29B(NKRO) 构建
```

### 1.4 模式联动方向

```
mode.c（AIN5 拨档采样）
   │  mode_event{KEYBOARD_MODE_USB/2_4G/BLE}
   ▼
usb_hid_transport.c：USB 档 usbd_enable，其它档 usbd_disable
hid_scheduler.c：清空缓冲（防旧状态残留）
```

---

## 2. 踩坑记录（按时间顺序）

### 坑 1：USB 完全不枚举 —— VBUS 检测陷阱 ⭐核心坑

**现象**：RTT 打印 `USB HID transport initialised`、`bNumInterfaces 4 wTotalLength 132`（配置描述符正常），但电脑设备管理器**完全没有设备**，RTT 无任何枚举请求（无 Set Address / Get Descriptor / Set Configuration）。

**根因链**（三层叠加）：

1. **Zephyr USB Device Next 的默认逻辑**：
   ```c
   // usb_hid_transport.c usbd_setup()（移植自参考工程的原版）
   if (!usbd_can_detect_vbus(&usbd_ctx)) {
       err = usbd_enable(&usbd_ctx);   // 只有不能检测 VBUS 时才主动 enable
   }
   ```

2. **nRF52840 UDC 驱动无条件声明支持 VBUS 检测**：
   ```c
   // udc_nrf.c init 中
   data->caps.can_detect_vbus = true;   // 无条件 = true！
   ```
   → 于是 `usbd_can_detect_vbus()` 恒返回 true → **主动 `usbd_enable()` 被跳过**，改为等 VBUS_READY 事件。

3. **VBUS 事件依赖 USBREG，而 nRF52840 在 Zephyr 中无 USBREG**：
   ```c
   // udc_nrf.c
   #ifdef CONFIG_HAS_HW_NRF_USBREG
       IRQ_CONNECT(USBREGULATOR_IRQn, ..., nrfx_usbreg_irq_handler, 0);  // 未编译！
   #endif
   ```
   且 `nrfx_power_usbevt_enable()` 被 `#if NRF_POWER_HAS_USBREG` 包裹（HAL 层），nRF52840 的 `NRF_POWER_HAS_USBREG` 因 Zephyr 侧无 usbreg DTS 节点/配置而不生效 → **VBUS 检测事件永远不会产生** → `UDC_EVT_VBUS_READY` 不提交 → `usbd_msg_cb` 不触发 → `usbd_enable()` 从未执行。

**结论**：`usbd_init()` 只"注册/配置描述符"（所以有 bNumInterfaces 日志），**不等于使能 USB**；真正使能靠 `usbd_enable()`，而它在 nRF52840 上被 VBUS 检测逻辑卡死。

**解决**：改为**无条件 `usbd_enable()`**，不依赖 VBUS 检测：
```c
err = usbd_enable(&usbd_ctx);   // 无条件
```
（nRF52840 由 usbd_enable 直接驱动外设 + D+ 上拉，无需 VBUS 事件。）

**教训**：
- `usbd_init` 日志 ≠ USB 已使能；看是否真的进入枚举（Set Configuration）才算通。
- 移植参考工程的 USB 代码时，**VBUS 检测路径必须按具体 SoC 重新评估**——参考工程能跑不代表目标板能跑（参考工程可能未真正测过 USB 枚举，或硬件不同）。

### 坑 2：插了 J-Link 烧录线，没插 USB 数据线

**现象**：修完坑 1 后电脑仍无设备。

**根因**：调试时一直插着 J-Link 烧录线，**Type-C USB 数据线没插**。nRF52840 的 USB 外设需要 **VDDUSB 供电（来自 VBUS）**，仅靠 J-Link 的 3.3V 供电不够，USB 外设不工作。

**解决**：插入 Type-C 数据线 + 复位（让固件在 VDDUSB 有电的情况下重新 `usbd_enable`）。

**教训**：USB 调试三板斧——① 确认 USB 数据线已插（不是烧录线）；② 插入后复位/重上电；③ 看设备管理器是否出现设备/未知设备。

### 坑 3：HID key report 刷屏（RTT 噪音）

**现象**：每次按键 RTT 打印大量 `HID key report proto=nkro size=29` + 29 字节 hexdump，干扰调试。

**解决**：
- `keymap.c`：`handle_hid_key_event()` 去掉 `LOG_INF("HID key report…")` 和 `LOG_HEXDUMP_INF(...)`（改为 ARG_UNUSED）；
- `hid_key_event.c`：`APP_EVENT_TYPE_DEFINE` 去掉 `APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE` flag（消掉 `e:hid_key_event proto=1 size=29` 事件日志）。

**教训**：HID 报告是高频事件，默认不应逐包打印；需要时用 debug 级或条件编译。

### 坑 4：Windows 显示 "USB Composite Device" 而非 "Mini Keyboard"

**现象**：枚举成功后设备管理器显示父设备 "USB Composite Device"。

**原因**：**正常现象**。多接口复合设备（2 HID + CDC）在 Windows 中父节点统一显示 "USB Composite Device"，产品字符串 "Mini Keyboard" 在子设备属性/描述符里。

**教训**：不要以父设备名判断枚举结果；看是否出现 + 子设备（HID Keyboard / USB 输入设备 / COM 口）+ 实际功能（按键输出、音量）。

### 坑 5（前置遗留）：乘除号键位反了

**现象**：按下乘号键打印"除号"。

**根因**：物理键帽排列 COL1=乘号(*)、COL2=除号(/)，代码按 COL1=除号、COL2=乘号写。

**解决**：`keymap.c` 键名表与 `keyboard_core.c` HID usage 同步交换（0x54 除号 ↔ 0x55 乘号）。

**教训**：键名表与 HID usage 表必须与**物理键帽排列**一致，且两处同步改。

### 坑 6（前置遗留）：旋钮方向反了

**现象**：顺时针旋转输出负数/音量减小。

**解决**：`encoder.c` 中 `accum_udeg` 累积取反（`-=` 替代 `+=`），并加中文调试打印。

---

## 3. 重难点

### 3.1 VBUS 检测与 usbd_enable 的关系（坑 1 的本质）

- `usbd_init()`：注册描述符、初始化配置（打印 bNumInterfaces/wTotalLength）——**不使能外设**。
- `usbd_enable()`：真正使能 UDC 控制器、D+ 上拉、控制管道——**USB 开始工作**。
- 默认代码 `if (!usbd_can_detect_vbus()) usbd_enable()` 依赖 VBUS 事件；nRF52840 无可用 VBUS 事件 → 必须无条件 enable。
- 后续若加"USB 拔出自动禁用"需求，需自建 VBUS 检测（GPIO 检测 VBUS 或轮询），不能依赖 Zephyr 默认路径。

### 3.2 复合设备配置（3 逻辑接口 = 4 接口描述符）

- HID Keyboard（29B 无 Report ID）、HID Consumer（2B 无 Report ID）、CDC ACM（占位，占 2 个接口描述符：通信类 + 数据类）。
- DTS：`hid_kbd` / `hid_consumer` 节点（`zephyr,hid-device`）+ `usbd` 下 `cdc_acm_uart0`（`zephyr,cdc-acm-uart`）。
- prj.conf：`USB_DEVICE_STACK_NEXT` + `USBD_HID_SUPPORT` + `USBD_CDC_ACM_CLASS` + `SERIAL` + `UART_LINE_CTRL`。

### 3.3 报告尺寸一致性（29/8/2 四处对齐）

| 尺寸 | keyboard_core | hid_key_event 缓冲 | DTS in-report-size | USB descriptor |
| --- | --- | --- | --- | --- |
| 29 (NKRO) | ✅ | ✅ report[29] | ✅ kbd=29 | ✅ 无 ID bitmap |
| 8 (Boot) | ✅ | 用前 8 字节 | 同上 | 同描述符（主机按协议解释） |
| 2 (Consumer) | ✅ usage uint16 | ✅ | ✅ consumer=2 | ✅ 16-bit array |

统一宏：`HID_KBD_NKRO_SIZE 29 / HID_KBD_BOOT_SIZE 8 / HID_CONSUMER_SIZE 2`（`include/hid_report_maps.h`）。

### 3.4 NKRO 无 Report ID 的 descriptor 设计

- 键盘 29B：byte0=modifier(8bit)，byte1-28=224bit 位图（usage 0x00-0xDF），**无 Report ID** → kbdhid.sys 直接绑定，避免报告 ID 解析错位。
- Boot 协议：同一描述符，主机只用前 8 字节（mod+reserved+6keys），键盘 core 按协议构建对应尺寸。

### 3.5 发送调度层（hid_scheduler）

| 通道 | 策略 | 原因 |
| --- | --- | --- |
| 键盘 | **last-wins**（单槽覆盖） | 键盘是状态量，只发最新状态，防连发刷屏 |
| Consumer | **16 深环形 FIFO** | 旋钮步进有语义，逐步保留防丢步 |
| 优先级 | 键盘 > Consumer | 键盘响应优先 |
| 背压 | 等 `hid_report_sent_event` 再发下一条 | USB 一次只能一个 IN report 在途，防提交失败丢包 |

`hid_report_sent_event` 由 USB transport 的 `input_report_done` 回调产生，驱动调度层 `_sending=false → try_flush()`。

### 3.6 协议切换（Boot/NKRO）

- 主机 `SET_PROTOCOL` → `kbd_set_protocol()` → `set_protocol_event` → keyboard_core 切 `_protocol`。
- USB 侧无需换描述符：主机按协议解释同一 29B 描述符（Boot 只看前 8 字节）。

### 3.7 LED output report 解析

- 主机 OUT report（LED）→ `kbd_set_report(type=OUTPUT)` 或 `kbd_output_report` → `kbd_handle_led()`。
- 解析：`len>=2 ? buf[1] : buf[0]`（跳过可能存在的 Report ID）→ `hid_led_event_submit(led)`。

### 3.8 模式联动

- `mode_event` → usb_hid_transport：USB 档 `usbd_enable`、其它档 `usbd_disable` + 清 in_flight；
- hid_scheduler 收到 mode_event 清空缓冲，防切档后旧报告残留。

---

## 4. 验证清单（冒烟测试）

1. USB 线插入 + 复位 → 设备管理器出现 "USB Composite Device"；
2. 记事本按数字键 → 有输出（键盘 HID 通）；
3. 旋钮旋转 → 音量 ±2；旋钮按压 → 静音 toggle（Consumer 通）；
4. CapsLock 按下 → RTT 打印 `HID LED state=0x02`（OUT report 通）；
5. 拨到 BLE/2.4G 档 → USB 断开；拨回 USB 档 → 重新枚举（模式联动通）；
6. 快速连按/快速旋转 → RTT 无 "Drop report" 刷屏、主机无丢键丢步（调度层通）。

---

## 5. 遗留与后续

- **CDC ACM**：本期仅枚举占位，未实现 frame/protocol/nanopb 数据链路（后续与参考工程对齐）。
- **BLE HID**：`hid_report_maps.h` 已预留 BLE 报告段（Report ID 1/2），后续 `ble_hid_service` 复用同一调度层。
- **VBUS 拔出检测**：当前无条件 enable，USB 拔出后不会自动禁用（无 VBUS 事件）；如需省电可后续加 GPIO VBUS 检测。
- **屏幕接口**：display 模块订阅 `mode_event` / `hid_led_event` 即可嫁接（当前工程无屏幕硬件）。
