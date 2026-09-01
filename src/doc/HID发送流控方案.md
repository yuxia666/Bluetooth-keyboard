# HID 发送流控方案 v2.0

> 状态：**待确认**（方案评审中，未实现）
> 版本：v2.0（2026-09-01，已整合全部评审意见）
> 前置：项目已迁移为 `01_code/01_code` 完整架构（CAF + hid_scheduler + usb_hid_transport + ble_hid_service）
> 范围：仅设计文档，不编码；确认后再实现。

---

## 1. 需求对齐

| 编号 | 需求 | 本方案 |
|------|------|--------|
| F1 | 键盘按键报告使用 latest 策略 | 键盘报告单槽 last-wins（新报告覆盖旧报告） |
| F2 | 音量加减等 consumer pulse 使用 FIFO 策略 | Consumer 环形 FIFO（深度 32），保序 |
| F3 | 支持 USB keys、USB consumer、BLE shared 三个发送通道 | 三个独立发送通道（HID_CHANNEL_USB_KBD / USB_CONSUMER / BLE_SHARED） |
| F4 | 发送前必须等待对应 HID 通道 ready | 每个通道独立 ready 标志；未 ready 时排队/暂存 |
| F5 | 发送完成通过 hid_report_sent_event 释放 in-flight 状态 | 每个通道独立 in-flight，`hid_report_sent_event` 携带 `channel` 字段 |

---

## 2. 现有架构分析

### 2.1 当前数据流

```
keyboard_core
  ├─ hid_key_event      (protocol, report[29], report_size)
  └─ hid_consumer_event (usage 16-bit)
        │
        ▼ CAF
  hid_scheduler
  ├─ _kbd_report[29]  单槽 last-wins
  ├─ _cc_fifo[16]     Consumer 环形 FIFO
  └─ try_flush()  → hid_report_to_send_event (report, size)
        │
        ▼ CAF
  usb_hid_transport / ble_hid_service
  ├─ on_report_to_send()
  ├─ 发送 → hid_device_submit_report() / bt_hids_inp_rep_send()
  └─ 完成后 → hid_report_sent_event_submit()
        │
        ▼ CAF
  hid_scheduler.on_hid_report_sent() → _sending = false → try_flush()
```

### 2.2 当前问题（已识别）

1. **单一发送通道**：`hid_scheduler` 只有一个 `_sending`，不区分 USB/BLE
2. **通道不支持独立 ready**：无法按 USB-KBD / USB-CC / BLE 分别等待
3. **模式切换时无法保持独立队列**：未定义清栈策略
4. **transport 层有独立 in-flight/ready**：与 scheduler 双份状态会打架
5. **iface_ready 变化无法通知 scheduler**：缺事件机制

---

## 3. 发送通道设计

### 3.1 三个通道

```c
enum hid_channel {
    HID_CHANNEL_USB_KBD,      /* USB 键盘通道（NKRO/Boot 报告） */
    HID_CHANNEL_USB_CONSUMER, /* USB Consumer 通道（2B pulse） */
    HID_CHANNEL_BLE_SHARED,   /* BLE 共享通道（键盘 + Consumer 复用） */
};
```

| 通道 | 报告类型 | 策略 | 深度 |
|------|---------|------|------|
| USB-KBD | 键盘（8B Boot / 29B NKRO） | latest（last-wins） | 1 |
| USB-CC | Consumer（2B pulse） | FIFO | **32** |
| BLE-SHARED | 键盘（8B/29B）+ Consumer（2B） | 键 latest + Consumer FIFO | 键 1 + CC **32** |

### 3.2 通道状态

```c
struct hid_channel_state {
    bool ready;           /* 对应 HID 通道已就绪 */
    bool in_flight;       /* 当前有报告正在发送 */

    /* 键盘 latest 槽 */
    uint8_t kbd_report[29];
    uint8_t kbd_size;
    bool    kbd_pending;

    /* Consumer FIFO（仅 USB-CC 和 BLE-SHARED 需要） */
    uint16_t cc_fifo[32];
    uint8_t  cc_head;
    uint8_t  cc_count;
};
```

---

## 4. 流控策略

### 4.1 键盘 latest（last-wins）

```
键盘报告到达：
  → 覆盖对应通道的 kbd_report 槽（新报告替换旧报告）
  → 若通道 not in_flight 且 ready：
      立即发送最新报告
  → 若 in_flight：
      保持 pending 标志，等 hid_report_sent_event 后再发送最新版
```

### 4.2 Consumer FIFO 深度与溢出策略（**修正**）

**FIFO 深度 = 32**（不是 16）。

原因：旋钮 10 卡点 = 20 条 FIFO 条目（每个 step 提交 usage + 0x0000 两次，`keyboard_core.c:289-292`）。背压时瞬间 20 条 > 16 必然溢出。深度 32 有足够裕量。

**溢出策略：丢队首 2 条（不是 1 条）**。

原因：`keyboard_core` 保证 press/release **严格交替提交**（usage, 0x0000, usage, 0x0000…），FIFO 中奇偶位置固定配对。满时丢 1 条会破坏配对：
- 丢 press → 孤立 release 白发一次
- 丢 release → 对端音量持续步进直到下次

**满时丢队首 2 条即可保持交替不破**，这个规律简化处理。

### 4.3 发送前必须等待通道 ready

#### ready 条件（**修正**）

| 通道 | ready 条件 |
|------|-----------|
| USB-KBD | `_kbd_iface_ready` 为 true |
| USB-CC | `_consumer_iface_ready` 为 true |
| BLE-SHARED | **只查 `_active_conn != NULL`**（**不查 `_peer_notify_on`**） |

> **重要**：BLE ready **不能**硬门控 `_peer_notify_on`。现有代码 `ble_hid_service.c:134-148` 明确注明：已绑定设备重连时 CCC 通知状态可能不重新触发，不宜硬阻塞。若硬门控，重连场景队列会永久冻结。`_peer_notify_on` 仅作诊断日志。

#### 未 ready 行为

- 键盘：保留在 kbd_report 槽，等 ready 后发送最新版（不丢最新状态）
- Consumer：保留在 cc_fifo，等 ready 后按序发送（不丢脉冲）

---

## 5. 通道 ready 事件机制

### 5.1 新增 `hid_channel_ready_event`

**必须新增**此事件（方案 v1 缺口）。用于 transport 层把 iface_ready 变化通知给 scheduler。

```c
struct hid_channel_ready_event {
    struct app_event_header header;
    enum hid_channel channel;   /* 哪个通道 */
    bool ready;                 /* true=就绪, false=失联 */
};
```

### 5.2 ready 事件来源与提交方/时机

| 通道 | 提交方 | 时机 |
|------|--------|------|
| USB-KBD | `usb_hid_transport` | `kbd_iface_ready()` 回调（ready true/false） |
| USB-CC | `usb_hid_transport` | `consumer_iface_ready()` 回调（ready true/false） |
| BLE-SHARED | `ble_hid_service` | `connected()` / `disconnected()` / `hids_notify_handler()` |

**scheduler 单一持有 ready 状态**。transport 事件只做增量修正。

### 5.3 模式切换时 scheduler 自己维护 current_mode

**不要等 transport 的 ready 事件迟到再反应**：

```
mode_event 一到：
  → scheduler 立即：
      · 新模式通道 ready = true
      · 旧模式通道 ready = false
      · 清空对应通道队列（kbd_pending + cc_fifo + in_flight）
  → 同时提交带 channel 的 sent（或直接清 in_flight），避免 sent 迟到与清空互踩
  → transport 的 ready 事件只做增量修正
```

**§5.3 的"模式切换清队列"与通道启用/禁用是原子的一步**，不需要猜时序。

---

## 6. 发送失败兜底（**修正：transport 的 ready 检查不能全删**）

### 6.1 transport ready 检查保留，语义改为"失败兜底"

ready 检查**必须保留**，但语义变为：

```
不 ready / 提交失败：
  → 立即提交带 channel 的 hid_report_sent_event（释放 in_flight）
  → 不静默丢弃
```

原因：ready 状态从 transport 回调同步到 scheduler 存在**时序窗口**——scheduler 刚收到 ready 事件时接口可能已 down。若 transport 静默丢弃（现行为 `usb_hid_transport.c:309-311`），scheduler 的 in_flight 永远不释放，**队列冻结**。兜底路径是流控正确性的最后防线。

### 6.2 模式切换/接口挂起时强制释放 in_flight

以下三个时机，transport **必须**提交带 channel 的 sent（或 scheduler 直接清 in_flight）：

1. **mode 切换**
2. **iface_ready(false)**
3. **power down**

**清队列同时清 in_flight**，避免 sent 迟到与清空互踩。

---

## 7. 事件流（示例）

```
[keyboard_core] hid_key_event(protocol=BOOT, report=8B)
  → hid_scheduler.on_hid_key_event()
  → 通道: BLE-SHARED（假设当前 BLE 模式）
  → kbd_report 覆盖 (latest)
  → try_flush()
  → if ready && !in_flight:
      hid_report_to_send_event_submit(channel=BLE_SHARED, report, size)
      in_flight = true

[ble_hid_service] on_report_to_send()
  → 检查 _active_conn（ready 兜底）
  → bt_hids_inp_rep_send()
  → 完成后 hid_report_sent_event_submit(channel=BLE_SHARED)
  → 失败时也提交 hid_report_sent_event_submit(channel=BLE_SHARED)

[hid_scheduler] on_hid_report_sent(channel=BLE_SHARED)
  → in_flight = false
  → try_flush() → 发送下一个 pending
```

---

## 8. 需要修改的事件定义

### 8.1 `hid_report_to_send_event`（新增 channel）

```c
struct hid_report_to_send_event {
    struct app_event_header header;
    enum hid_channel channel;   /* 目标通道 */
    uint8_t report[29];
    uint8_t report_size;
};
```

### 8.2 `hid_report_sent_event`（新增 channel — **必须**）

```c
struct hid_report_sent_event {
    struct app_event_header header;
    enum hid_channel channel;   /* 完成通道 */
};
```

> **必须加 channel**：USB 双接口可同时 in-flight，方案 B（按当前模式推断）不可行。

### 8.3 `hid_channel_ready_event`（**新增**）

```c
struct hid_channel_ready_event {
    struct app_event_header header;
    enum hid_channel channel;
    bool ready;
};
```

### 8.4 `enum hid_channel`（新建头文件）

放置于 `include/events/hid_channel.h`，**纯头文件，不需要源文件**。

---

## 9. 文件修改清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/events/hid_channel.h` | **新建** | `enum hid_channel` 定义（纯头文件） |
| `include/events/hid_report_to_send_event.h` | **修改** | 新增 `channel` 字段 |
| `include/events/hid_report_sent_event.h` | **修改** | 新增 `channel` 字段 |
| `include/events/hid_channel_ready_event.h` | **新建** | 新增 ready 事件 |
| `src/events/hid_report_to_send_event.c` | **修改** | 日志/分析加 channel |
| `src/events/hid_report_sent_event.c` | **修改** | 日志/分析加 channel |
| `src/events/hid_channel_ready_event.c` | **新建** | 事件实现 |
| `src/hid_scheduler.c` | **修改** | 改为三通道状态机 + 模式管理 |
| `src/usb_hid_transport.c` | **修改** | 发送带 channel；ready 回调发 ready 事件；失败兜底发 sent；删除按 report_size 路由逻辑 |
| `src/ble_hid_service.c` | **修改** | 发送带 channel；connected/disconnected 发 ready 事件；失败兜底发 sent |
| `src/app/keyboard_core.c` | **修改** | 订阅 mode_event，切到 USB/BLE 时主动 `submit_report()` 重发当前 keystate |
| `CMakeLists.txt` | **修改** | 仅加 `include/events`（已在 include 路径）；新增 `hid_channel_ready_event.c` 源 |
| `src/doc/HID发送流控方案.md` | **新增** | 本方案 |

> **注意**：`hid_channel` 是纯枚举，**不需要源文件**；`enum` 直接放在头文件即可。

---

## 10. 模式切换后 keystate 重发（**并入本方案**）

**问题**：2.4G 期间按住键，切回 USB/BLE 时 keyboard_core 没有新事件（keystate 只在变化时发报告），对端收不到当前按下状态。

**解决**：`keyboard_core` 订阅 `mode_event`，切到 USB/BLE 时主动 `submit_report()` 一次，重发当前 keystate 快照。

```c
/* keyboard_core 订阅 mode_event */
if (is_mode_event(aeh)) {
    if (new_mode == MODE_USB || new_mode == MODE_BLE) {
        submit_report();   /* 重发当前 keystate 快照（keyboard_core.c:222-233 现成函数） */
    

}

**成本很低，顺手并入本方案**，不列为后续项。

---

## 11. 验证方案

### 11.1 功能验证

| 编号 | 操作 | 预期 |
|------|------|------|
| V1 | 快速连按 5 个不同键 | 最终只发送最后一次键盘状态（latest） |
| V2 | 快速转旋钮 10 卡点 | Consumer FIFO 保序发送 20 条（press+release），**不溢出**（深度 32） |
| V3 | 键盘+旋钮同时操作 | 键盘优先，Consumer 排队 |
| V4 | USB 插拔 | 未 ready 时 pending 保留，ready 后发送；**接口 down 时兜底发 sent 释放** |
| V5 | BLE 配对/断开/重连 | 未连接时 pending 保留，重连后发送（**不因 notify 未触发冻结**） |
| V6 | USB→BLE 切换 | 清空 USB 队列，BLE 队列正常；**keystate 重发** |
| V7 | BLE→USB 切换 | 清空 BLE 队列，USB 队列正常；**keystate 重发** |
| V8 | 传输失败 | 兜底发 sent 释放 in_flight，不冻结 |
| V9 | FIFO 溢出（压力测试） | 丢队首 2 条，保持 press/release 配对 |

### 11.2 回归验证

| 项 | 预期 |
|----|------|
| USB HID | 不回归 |
| BLE HID | 不回归 |
| 三模切换 | 不回归 |
| 电池/低功耗 | 不回归 |

---

## 12. 风险与注意事项

| 编号 | 风险 | 应对 |
|------|------|------|
| R1 | `hid_report_sent_event` 不带 channel | **必须**扩展 channel（USB 双接口同时 in-flight） |
| R2 | BLE notify 硬门控 | **禁止**——只查 `_active_conn`，否则重连冻结 |
| R3 | Consumer FIFO 溢出丢 1 条 | 丢队首 **2 条**（保持 press/release 配对） |
| R4 | transport 静默丢弃导致队列冻结 | **必须**兜底发 sent |
| R5 | ready 事件时序窗口 | scheduler 单一持有 ready，模式切换时主动置位 |
| R6 | mode 切换/挂起时 in_flight 残留 | **必须**强制释放（发 sent 或清 in_flight） |
| R7 | 发送失败丢 consumer pulse | 接受（"丢失策略"，非 bug）；键盘 latest 无影响 |

---

## 13. 待确认事项

| 项 | 建议 | 状态 |
|----|------|------|
| `hid_channel` 枚举位置 | `include/events/hid_channel.h`（纯头文件） | ✅ 已定 |
| `hid_report_sent_event` 加 channel | 必须加 | ✅ 已定 |
| Consumer FIFO 深度 | 32 | ✅ 已定 |
| FIFO 溢出策略 | 丢队首 2 条 | ✅ 已定 |
| BLE ready 条件 | 只查 `_active_conn` | ✅ 已定 |
| `hid_channel_ready_event` | 必须新增 | ✅ 已定 |
| transport 失败兜底 | 发带 channel 的 sent | ✅ 已定 |
| keystate 重发 | 并入本方案 | ✅ 已定 |

---

*本方案待确认。确认后按 §9 文件清单实施。*
