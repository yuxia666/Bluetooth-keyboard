# BLE HID 模块实现方案（与 USB 同级，模式切换）

| 项目 | 内容 |
| --- | --- |
| 文档版本 | V1.1 |
| 编制日期 | 2026-09-01 |
| 状态 | **方案已确认，待实现**（安全基线 `ble-before`） |
| 当前工程 | `F:\ble_key_workspace\01_code` |
| 参考实现 | `F:\ble_key_workspace\keyboard\src\ble_hid_service.c` |
| 回退方式 | `git reset --hard ble-before` |

---

## 1. 目标与定位

BLE 模块与 USB HID 模块**同等级**：同为 `hid_report_to_send_event` 的消费者，由 `mode_event` 按模式切换启停。

```
按键/旋钮 → keyboard_core → hid_scheduler ──▶ hid_report_to_send_event
                                                   │
                    ┌──────────────────────────────┼──────────────────────┐
                    ▼                              ▼                      ▼
            usb_hid_transport              ble_hid_service         (2.4G 预留)
            (USB 档启用)                    (BLE 档启用)
```

| 模式 | USB transport | BLE service | 2.4G（预留） |
| --- | --- | --- | --- |
| USB 档 | ✅ usbd_enable | ⏹ ble_stop | ⏹ |
| 2.4G 档 | ⏹ usbd_disable | ⏹ ble_stop | **预留** |
| BLE 档 | ⏹ usbd_disable | ✅ ble_start | ⏹ |

> 与 USB 一致：`mode_event` 是唯一切换依据；低功耗 `power_down_event` 时 BLE 停止。

---

## 1.5 实现思路（总纲）

BLE 模块**不做任何重复造轮子**：与 USB 共用同一套输入链路（keyboard_core → hid_scheduler → hid_report_to_send_event），
BLE 只是该事件的**第二个消费者**。实现分四步：

```
① 配置层：prj.conf 补齐 BT/HIDS/BAS/Settings（裁剪，不含 NUS/MCUboot）
② 事件层：新增 click_detector / settings_loader 配置头
③ 传输层：移植 ble_hid_service.c（HIDS + 报告 + 连接 + 模式启停）
④ 门控层：补强 SECURED + notify 双门控（参考工程缺陷）
```

> 关键认知：**BLE 与 USB 的差异只在"发送通道"**（bt_hids vs usbd），
> 协议切换、LED、调度背压全部走同一套事件，实现量最小。

---

## 2. 需要做的工作（清单）

### 2.1 复用已有（当前工程已具备，零改动）

| 组件 | 说明 |
| --- | --- |
| `hid_report_to_send_event` / `hid_report_sent_event` | 调度层↔传输层接口（BLE 直接复用） |
| `set_protocol_event` | Boot/Report 协议切换（BLE 的 pm_evt 回调复用） |
| `hid_led_event` | 主机 LED output（BLE output report → 同事件） |
| `hid_report_maps.h` | 已含 BLE report map（Report ID 1/2）+ 尺寸宏（30/3） |
| `hid_scheduler.c` | 发送调度层（BLE 复用，无需改动） |
| `keyboard_core.c` | 报告构建（协议无关） |
| `mode_event` / `mode.c` | 模式切换（BLE 档触发 ble_start） |

### 2.2 新增文件

| 文件 | 内容 |
| --- | --- |
| `src/ble_hid_service.c` | **核心**：HIDS 实例、报告收发、连接管理、模式启停（移植参考工程） |
| `include/click_detector_def.h` | CAF click detector 配置（旋钮按键短/长按，用于配对/清绑定） |
| `include/settings_loader_def.h` | CAF settings loader 配置（BLE 绑定持久化） |

### 2.3 修改文件

| 文件 | 修改 |
| --- | --- |
| `prj.conf` | 增加全部 BT + CAF BLE 配置（见 §3） |
| `CMakeLists.txt` | 挂载 `ble_hid_service.c` |
| `app.overlay` | 无（E73 模块自带天线，BLE 无需 DTS 节点） |
| `keymap.c`（可选） | 订阅 `ble_peer_event` 打印连接状态（便于 RTT 验证） |

---

## 3. prj.conf 配置（对齐参考工程）

### 3.1 Zephyr BLE 基础

```kconfig
CONFIG_BT=y
CONFIG_BT_PERIPHERAL=y
CONFIG_BT_MAX_CONN=1
CONFIG_BT_MAX_PAIRED=1
CONFIG_BT_DEVICE_NAME="Mini Keyboard"
CONFIG_BT_DEVICE_APPEARANCE=961
CONFIG_BT_SMP=y
CONFIG_BT_BONDABLE=y
CONFIG_BT_ATT_TX_COUNT=5
CONFIG_BT_L2CAP_TX_MTU=65
CONFIG_BT_BUF_ACL_RX_SIZE=69
CONFIG_BT_BUF_ACL_TX_SIZE=69
CONFIG_BT_GATT_AUTO_SEC_REQ=n
CONFIG_BT_AUTO_PHY_UPDATE=n
CONFIG_BT_CONN_CTX=y
```

### 3.2 Advertising provisioning（CAF ble_adv 需要）

```kconfig
CONFIG_BT_ADV_PROV_FLAGS=y
CONFIG_BT_ADV_PROV_GAP_APPEARANCE=y
CONFIG_BT_ADV_PROV_DEVICE_NAME=y
CONFIG_BT_ADV_PROV_DEVICE_NAME_SD=y
CONFIG_BT_ADV_PROV_SWIFT_PAIR=y
```

### 3.3 HID over GATT

```kconfig
CONFIG_BT_HIDS=y
CONFIG_BT_HIDS_MAX_CLIENT_COUNT=1
CONFIG_BT_HIDS_INPUT_REP_MAX=2
CONFIG_BT_HIDS_OUTPUT_REP_MAX=1
CONFIG_BT_HIDS_ATTR_MAX=50
CONFIG_BT_GATT_CHRC_POOL_SIZE=20
CONFIG_BT_GATT_UUID16_POOL_SIZE=40
CONFIG_BT_BAS=y          # Battery Service（电池电量上报）
CONFIG_BT_DIS=y          # Device Information Service
CONFIG_BT_DIS_MANUF_NAME=y
CONFIG_BT_DIS_MANUF_NAME_STR="atguigu"
CONFIG_BT_DIS_PNP=y
CONFIG_BT_DIS_PNP_VID_SRC=2
CONFIG_BT_DIS_PNP_VID=0x2FE3
CONFIG_BT_DIS_PNP_PID=0x0001
CONFIG_BT_DIS_PNP_VER=0x0100
```

### 3.4 CAF BLE 模块

```kconfig
# BLE 状态管理（连接事件 → ble_peer_event）
CONFIG_CAF_BLE_STATE=y
CONFIG_CAF_BLE_STATE_SECURITY_REQ=y
CONFIG_CAF_BLE_STATE_PM=y
CONFIG_CAF_BLE_STATE_MAX_LOCAL_ID_BONDS=1

# BLE 广播（由 ble_hid_service 通过 module_resume/suspend 控制启停）
CONFIG_CAF_BLE_ADV=y
CONFIG_CAF_BLE_ADV_MODULE_SUSPEND_EVENTS=y
CONFIG_CAF_BLE_ADV_FAST_ADV=y
CONFIG_CAF_BLE_ADV_FILTER_ACCEPT_LIST=y

# 绑定管理（配对持久化）
CONFIG_CAF_BLE_BOND=y
CONFIG_CAF_BLE_BOND_PEER_ERASE_CLICK=y
CONFIG_CAF_BLE_BOND_PEER_ERASE_CLICK_LONG=y
CONFIG_CAF_BLE_BOND_PEER_ERASE_CLICK_KEY_ID=3
CONFIG_CAF_BLE_BOND_PEER_ERASE_CLICK_TIMEOUT=5000

# 点击检测（旋钮按键短/长按 → 配对/清绑定）
CONFIG_CAF_CLICK_DETECTOR=y
CONFIG_CAF_CLICK_DETECTOR_DEF_PATH="click_detector_def.h"

# 绑定持久化（settings + NVS）
CONFIG_BT_SETTINGS=y
CONFIG_CAF_SETTINGS_LOADER=y
CONFIG_CAF_SETTINGS_LOADER_DEF_PATH="settings_loader_def.h"
CONFIG_CAF_SETTINGS_LOADER_USE_THREAD=y
CONFIG_CAF_SETTINGS_LOADER_THREAD_STACK_SIZE=1792
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_NVS=y
CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_FLASH_MAP=y
```

---

## 4. ble_hid_service.c 关键设计（移植参考工程）

### 4.1 HIDS 实例

```c
BT_HIDS_DEF(hids_obj, BLE_KBD_REPORT_SIZE, BLE_CC_REPORT_SIZE);
/* BLE_KBD_REPORT_SIZE=30（1B ID + 29B NKRO），BLE_CC_REPORT_SIZE=3（1B ID + 2B） */
```

### 4.2 报告映射（Report ID）

| 报告 | Report ID | 载荷 | bt_hids 行为 |
| --- | --- | --- | --- |
| 键盘 NKRO | 1 | 29B（1 mod + 28 bitmap） | bt_hids 自动加 ID，发送 29B 载荷 |
| Consumer | 2 | 2B（16-bit usage） | bt_hids 自动加 ID，发送 2B 载荷 |
| LED Output | 1 | 1B | 主机写 → `hids_outp_rep_handler` → `hid_led_event` |

### 4.3 报告发送（on_report_to_send）

- **NKRO（29B）**：`bt_hids_inp_rep_send(…, BLE_INPUT_REP_KBD_IDX, …)`
- **Consumer（2B）**：`bt_hids_inp_rep_send(…, BLE_INPUT_REP_CC_IDX, …)`
- **Boot（8B）**：
  - 对端 Boot 模式 → `bt_hids_boot_kb_inp_rep_send(...)`
  - 对端 Report 模式 → `boot8_to_nkro()` 转 29B 再发
- 发送完成 → `hid_report_sent_event_submit()`（驱动调度层背压释放，与 USB 一致）

### 4.4 协议切换

`hids_pm_evt_handler(BT_HIDS_PM_EVT_BOOT_MODE_ENTERED)` → `set_protocol_event_submit()` → keyboard_core 切协议（与 USB 完全同链路）

### 4.5 LED output

`hids_outp_rep_handler` / `hids_boot_kb_outp_rep_handler` → `hid_led_event_submit(rep->data[0])`

### 4.6 连接管理

- `ble_peer_event`（CAF ble_state 产生）→ CONNECTED：`bt_hids_connected()` + 记录 `_active_conn`；DISCONNECTED：`bt_hids_disconnected()` + 清空
- 无连接时丢弃 `hid_report_to_send_event`（与 USB 的 iface_ready 检查同理）

### 4.7 模式启停（核心，与 USB 对称）

```c
if (is_mode_event(aeh)) {
    if (mode == KEYBOARD_MODE_BLE) {
        ble_start();   // module_resume_req(ble_adv) → 开始广播
    } else {
        ble_stop();    // module_suspend_req(ble_adv) + 断开连接
    }
}
```

- `ble_start()`：向 CAF `ble_adv` 模块发 `module_resume_req_event` → 开始广播
- `ble_stop()`：向 `ble_adv` 发 `module_suspend_req_event` + `bt_conn_disconnect()` 断开现有连接 + 清 `_active_conn`
- **幂等**：`_ble_active` 标志防重复启停
- `power_down_event` → `ble_stop()`（低功耗停止广播）

---

## 5. 模式切换联动矩阵（完整）

| 事件 | USB transport | BLE service | 效果 |
| --- | --- | --- | --- |
| `mode_event`=USB | `usbd_enable` | `ble_stop` | USB 可枚举，BLE 停 |
| `mode_event`=2.4G | `usbd_disable` | `ble_stop` | 两者停（2.4G 预留） |
| `mode_event`=BLE | `usbd_disable` | `ble_start` | BLE 广播，USB 停 |
| `power_down_event` | 清 in_flight | `ble_stop` | 低功耗全停 |
| `wake_up_event` | 恢复 | 由下次 mode_event 决定 | — |

---

## 6. 新增头文件（CAF 配置）

### 6.1 `include/click_detector_def.h`

```c
#include <caf/click_detector.h>

static const struct click_detector_config click_detector_config[] = {
	{
		.key_id = KEY_ID(3, 0),   /* 旋钮按键（ROW0 COL3） */
		.consume_button = true,
		.click_detection = CLICK_DETECTION_CLICK_ONLY,
		.click_timeout = 500,
	},
};
```

> 用途：`ble_bond` 用旋钮按键长按清除绑定（`PEER_ERASE_CLICK_LONG`）。

### 6.2 `include/settings_loader_def.h`

```c
#include <caf/settings_loader.h>

static const struct settings_loader_config settings_loader_config[] = {
	SETTINGS_LOADER_BT_BOND_CONFIG(0),
};
```

---

## 7. 依赖与风险

| 项 | 说明 |
| --- | --- |
| **CAF 事件依赖** | `ble_peer_event`（CAF ble_state）、`module_suspend_event`（CAF 核心）需在 CAF 已启用（当前 `CONFIG_CAF=y` ✅） |
| **广播控制** | 依赖 `CAF_BLE_ADV_MODULE_SUSPEND_EVENTS`（默认 y，随 CAF_MODULE_SUSPEND_EVENTS） |
| **绑定持久化** | 需要 `CONFIG_BT_SETTINGS` + NVS + flash 分区（当前 board 有 `storage_partition` ✅） |
| **内存** | BLE 栈需更大堆/ACL buffer，`CONFIG_HEAP_MEM_POOL_SIZE` 可能需要增大 |
| **低功耗** | BLE 连接时 CAF Power Manager 不应进入深睡（`CAF_BLE_STATE_PM` 处理） |
| **2.4G** | 本期不做，仅预留模式枚举 `KEYBOARD_MODE_2_4G` 与响应矩阵位 |

---

## 7.5 重难点（实现时重点关注）

| # | 难点 | 说明 | 对策 |
| --- | --- | --- | --- |
| 1 | **加密+通知门控**（要求 2） | 参考工程只查 `_active_conn`，未加密/未开通知即发报告会失败或丢失 | `PEER_STATE_SECURED` + `notify_handler` 双门控 |
| 2 | **Boot 报告转换** | BLE 端对端 Report 模式收到 8B Boot 报告时，bt_hids 需要 29B | `boot8_to_nkro()` 位图展开（参考已实现） |
| 3 | **背压一致性** | `hid_report_sent_event` 必须与 USB 一样在发送完成/失败时都触发，否则调度层卡死 | 复用 `inp_report_done` 回调 + 错误分支补发 |
| 4 | **模式切换竞态** | 切 USB 档瞬间 BLE 可能仍在发报告 | `ble_stop()` 幂等（`_ble_active` 标志）+ 清 `_active_conn` |
| 5 | **绑定持久化** | 重启后需自动重连（settings 加载 bond） | `CAF_SETTINGS_LOADER` + `BT_SETTINGS` + NVS |
| 6 | **广播启停** | 非 BLE 档必须停广播（省电、避免干扰） | `module_suspend_req(ble_adv)`（`ble_stop()`） |
| 7 | **内存预算** | BLE 栈 + HIDS + settings 增加 RAM/Flash | 裁剪 prj.conf（去 NUS/MCUboot）；必要时增大 HEAP |
| 8 | **RTT 调试** | 需同时看到：连接状态、报告收发、电压 | 保留 `mode`/`battery` 打印；新增 `BLE peer connected` 等日志 |

---

## 7.6 RTT 打印（与 USB 模式一致，验证用）

BLE 模式下 RTT 应能看到（与 USB 模式同源的日志）：

```
Mode: BLE                              ← 模式切换
BLE peer connected                     ← 连接成功
BLE report mode entered                ← 协议（report/boot）
[3,3]加号 按下                          ← 按键（keymap 同源）
e:hid_report_to_send_event size=29    ← 调度层（同源）
Battery: 4120 mV, SOC 100%            ← 电压（power_mgr 同源，每秒）
HID LED state=0x02                     ← LED output（同源）
旋钮: 顺时针 18°, 1 卡点               ← 编码器（同源）
```

> 全部来自既有模块（keyboard_core / power_mgr / keymap / encoder），BLE 不新增重复打印，
> 保证"BLE 与 USB 行为一致、可对比验证"。

---

## 8. 验证计划

1. **广播**：BLE 档 → 手机/电脑蓝牙可见 "Mini Keyboard"；
2. **连接**：配对连接成功，RTT 打印 `BLE peer connected`；
3. **按键**：连接后按数字键 → 对端输入数字；
4. **旋钮**：音量 ± / 静音（Consumer over BLE）；
5. **协议**：对端切 Boot 模式 → RTT 打印 `BLE boot mode entered`；
6. **LED**：对端 CapsLock → RTT 打印 `HID LED state=0x02`；
7. **模式切换**：拨到 USB 档 → BLE 断开、USB 枚举；拨回 BLE 档 → 重新广播；
8. **绑定**：配对本机，重启后自动重连（settings 持久化）；
9. **清绑定**：旋钮按键长按 → 清除配对。

---

## 9. 实施顺序（已确认，开始执行）

1. `prj.conf` 增加 BT/CAF BLE 配置（裁剪：不含 NUS/MCUboot）；
2. 新增 `click_detector_def.h`、`settings_loader_def.h`；
3. 移植 `ble_hid_service.c`（HIDS/报告/连接/模式启停）+ **补强 SECURED/notify 门控**；
4. CMakeLists 挂载，编译验证；
5. 烧录，按 §8 验证计划逐项验收（含 RTT 电压打印）；
6. （后续）LCD 显示、灯带、时间显示（见 `未来功能预留规划.md`）。

> 回退：任一步骤异常可 `git reset --hard ble-before` 回到 USB 模式完毕基线。

---

## 10. 与 USB 模块的一致性说明

| 维度 | USB | BLE | 复用点 |
| --- | --- | --- | --- |
| 事件接口 | hid_report_to_send/sent | 同 | ✅ 同一事件 |
| 调度层 | hid_scheduler | 同 | ✅ 无需改动 |
| 协议切换 | set_protocol_event | 同 | ✅ 同一事件 |
| LED | hid_led_event | 同 | ✅ 同一事件 |
| 模式启停 | mode_event → usbd_enable/disable | mode_event → ble_start/stop | ✅ 同一模式事件 |
| 报告源 | keyboard_core | 同 | ✅ |

---

## 11. 设计要求确认（5 条，已评审）

### 要求 1：支持 keyboard / consumer / boot keyboard —— ✅ 参考工程已实现，直接移植

参考工程 `ble_hid_service.c` 已完整支持三态：

| 报告 | Report ID | 载荷 | bt_hids 行为 |
| --- | --- | --- | --- |
| NKRO | 1 | 29B（1 mod + 28 bitmap） | `bt_hids_inp_rep_send`（自动加 ID） |
| Consumer | 2 | 2B（16-bit usage） | `bt_hids_inp_rep_send`（自动加 ID） |
| Boot | — | 8B | 对端 Boot 模式 → `bt_hids_boot_kb_inp_rep_send`；对端 Report 模式 → `boot8_to_nkro()` 转 29B 再发 |

`hid_report_maps.h` 中 BLE report map（Report ID 1/2）与尺寸宏（30/3）已备好。

### 要求 2：加密完成 + 通知打开才允许发送 —— ⚠️ 参考工程缺陷，必须补强

**参考工程现状**：`on_report_to_send` 只检查 `_active_conn != NULL`（已连接），**未检查加密与通知状态**。存在两个真实问题：

- 连接建立但**未加密**（SMP 未完成）即发 HID report → 发送失败或数据泄露；
- 主机**未开启通知**（CCC 未写 0x0001）时 report 发了也收不到。

**可用机制**（CAF 与 bt_hids 现成）：
- `ble_peer_event` → **`PEER_STATE_SECURED`**（加密完成）；
- `bt_hids` → **`notify_handler`** 回调（`BT_HIDS_CCCD_EVT_NOTIFY_ENABLED / DISABLED`）。

**方案**：`ble_hid_service.c` 增加双门控标志：

```c
static bool _peer_secured;    /* PEER_STATE_SECURED 置位 */
static bool _peer_notify_on;  /* CCC 通知开启置位 */
/* on_report_to_send: 任一未满足则丢弃（与 USB 的 iface_ready 门控对称） */
```

### 要求 3：LED output → hid_led_event —— ✅ 参考工程已实现，直接复用

- `hids_outp_rep_handler` / `hids_boot_kb_outp_rep_handler` → `hid_led_event_submit(rep->data[0])`；
- 与 USB `kbd_handle_led` 走**同一事件**，下游（keymap 日志/屏幕）零改动。

### 要求 4：与 USB 共用 keyboard_core + hid_flowctrl —— ✅ 架构天然支持

当前 `hid_scheduler.c`（调度层）与传输无关：只订阅 `hid_key/consumer_event` 产出 `hid_report_to_send_event`。USB 与 BLE 都是该事件的消费者：

```
keyboard_core → hid_scheduler → hid_report_to_send_event
                                     ├─ usb_hid_transport（USB 档）
                                     └─ ble_hid_service（BLE 档）
```

复用零改动：`keyboard_core` / `hid_scheduler` / `set_protocol_event` / `hid_led_event`。

### 要求 5：prj.conf BLE/HIDS/BAS/Settings 配置完整性 —— ⚠️ 当前全缺，需补齐

当前 `prj.conf` 无任何 BT 配置。需补齐（见 §3 完整清单），且**裁剪**：不含 BLE NUS、MCUboot 等参考工程专有项：

| 配置组 | 关键项 | 当前状态 |
| --- | --- | --- |
| BT 基础 | `BT=y / PERIPHERAL / SMP / BONDABLE / DEVICE_NAME` | ❌ 缺 |
| HIDS | `BT_HIDS=y / INPUT_REP_MAX=2 / OUTPUT_REP_MAX=1 / ATTR_MAX=50` | ❌ 缺 |
| BAS/DIS | `BT_BAS=y / BT_DIS=y`（电池上报 + 设备信息） | ❌ 缺 |
| ADV_PROV | `BT_ADV_PROV_*`（flags/appearance/name） | ❌ 缺 |
| CAF BLE | `CAF_BLE_STATE / BLE_ADV / BLE_BOND` | ❌ 缺 |
| Settings/NVS | `BT_SETTINGS / SETTINGS_NVS / NVS / FLASH` | ❌ 缺 |
| Click | `CAF_CLICK_DETECTOR`（旋钮长按清绑定） | ❌ 缺 |

### 设计要求汇总

| 要求 | 结论 | 动作 |
| --- | --- | --- |
| 1 三态报告 | ✅ 参考已实现 | 移植，零改动 |
| 2 加密+通知门控 | ⚠️ 参考缺陷 | **补强**：SECURED + notify 双门控 |
| 3 LED | ✅ 参考已实现 | 复用，零改动 |
| 4 共用 core/scheduler | ✅ 架构支持 | 复用，零改动 |
| 5 配置完整性 | ⚠️ 当前全缺 | 补齐裁剪后的 prj.conf |
