# 技术方案评估:CAF Buttons + nRF Desktop 参考实现

| 项目 | 内容 |
| --- | --- |
| 文档版本 | V1.0 |
| 编制日期 | 2026-06-08 |
| 状态 | **评估中,待确认,未写代码** |
| 评估对象 | "Nordic CAF Buttons 模块 + nRF Desktop 参考实现" 用于本项目蓝牙小键盘 |
| 关联需求 | 矩阵按键、三模切换(USB/蓝牙/2.4G)、OLED 显示屏 |

---

## 1. 结论摘要

| 问题 | 结论 |
| --- | --- |
| CAF Buttons + nRF Desktop 方案可行吗? | ✅ **可行,且强烈推荐**。nRF Desktop 是 Nordic 官方桌面外设参考实现,基于 CAF,已内置 HID 键盘矩阵、BLE/USB 双模 HID 上报、模式切换(selector)、按键状态管理、电池/DFU/低功耗等完整模块,与本项目(蓝牙数字小键盘)形态高度一致 |
| Zephyr 与 CAF 结合使用? | ✅ 本来就是结合使用:nRF Desktop 内部就是 Zephyr 驱动 + CAF 模块化架构。矩阵扫描走 CAF buttons,底层仍是 Zephyr GPIO;显示屏走 Zephyr LVGL;BLE/USB 走 Zephyr 协议栈 |
| 三模切换(USB/蓝牙/2.4G) | ✅ nRF Desktop 原生支持 USB/BLE 双模切换(usb_state + ble + hid_state);本项目三模开关为**单引脚 ADC 电平检测**(U1 SK-13D07-4 → MODE → AI5),需要自写一个小的 selector/模式检测模块(参考 nRF Desktop selector_hw 思路,改用 ADC 采样),或直接用 Zephyr ADC + 事件 |
| 显示屏 | ✅ nRF Desktop 本身**不带显示屏模块**,但 Zephyr 内置 LVGL + 常见 SPI 屏驱动(SSD1306/ST7789/ST7735/ILI9xxx),可新增一个 CAF display 模块(订阅 hid_state/selector/battery 事件刷新 UI),工作量可控 |
| 需要自研的部分 | ① ADC 三模检测(替代 selector_hw 的 GPIO 组合)② 显示屏 CAF 模块(订阅事件刷新)③ EC11 旋钮消费(音量/滚动)④ 本项目特有 hid_keymap_def.h(数字小键盘键码表) |

---

## 2. nRF Desktop 参考实现调研(基于 NCS v3.2.3)

### 2.1 位置与形态

- 应用路径:`nrf/applications/nrf_desktop`;
- 官方定位:桌面输入设备(键盘/鼠标/游戏手柄)参考实现,基于 **CAF** 事件驱动架构;
- 与本项目最接近的参考配置:**`configuration/nrf52kbd_nrf52832`**(nRF52 小键盘):18 行 × 8 列矩阵、`buttons_def.h` 定义 col/row、`hid_keymap_def.h` 定义 KEY_ID→HID 键码(含 keypad 全套键码),与我们的数字小键盘需求完全同构。

### 2.2 可直接复用的模块(nRF Desktop src/modules + src/hw_interface)

| 模块 | 路径 | 功能 | 本项目复用度 |
| --- | --- | --- | --- |
| CAF buttons | `nrf/subsys/caf/modules/buttons.c` | 矩阵扫描(buttons_def.h 定义 col/row),输出 `button_event{key_id, pressed}` | ✅ 直接复用(填本项目 4 列 × 6 行) |
| hid_provider_keyboard | `nrf_desktop/src/modules/hid_provider_keyboard.c` | 键盘 HID 报告生成 | ✅ 直接复用 |
| hid_state | `nrf_desktop/src/modules/hid_state.c` | 按键状态管理、重复键/按键队列 | ✅ 直接复用 |
| hid_keymap | `nrf_desktop/src/modules/hid_keymap*` | KEY_ID → HID 键码映射(配置文件 hid_keymap_def.h) | ✅ 直接复用(填数字小键盘键码表) |
| hids | `nrf_desktop/src/modules/hids.c` | BLE HID over GATT | ✅ 直接复用 |
| usb_state | `nrf_desktop/src/modules/usb_state.c` | USB HID 枚举/上报 | ✅ 直接复用(USB 模式) |
| ble_adv_ctrl / ble_bond / ble_state | `nrf_desktop/src/modules/*` | BLE 广播/配对/连接状态 | ✅ 直接复用 |
| selector_hw | `nrf_desktop/src/hw_interface/selector_hw.c` | 模式切换(多 GPIO 组合 → selector_event) | ⚠️ 参考思路,本项目改为 **ADC 单引脚电平检测** |
| click_detector | `nrf/subsys/caf/modules/click_detector.c` | 短按/长按/双击识别 | ✅ 复用(旋钮按键、Fn 层) |
| led_state | `nrf_desktop/src/modules/led_state.c` | 状态 LED(连接/充电等) | ✅ 复用(LED1/LED2) |
| battery_meas / battery_charger | `nrf_desktop/src/hw_interface/*` | 电池采样/充电状态 | ⚠️ 参考(BAT_ADC 引脚不同) |
| dfu / dfu_mcumgr / factory_reset | `nrf_desktop/src/modules/*` | DFU/恢复出厂 | ✅ 可选复用 |
| power_manager / usb_state_pm / hid_state_pm | CAF + desktop | 低功耗 | ✅ 复用 |

### 2.3 事件流(CAF 核心优势,正好支撑"模式切换 + 显示屏")

```
buttons (矩阵扫描)
   └─→ button_event {key_id, pressed}
          └─→ click_detector ──→ click_event (短按/长按/双击)
          └─→ fn_keys ──→ (Fn 层键) hid_state
          └─→ hid_state ──→ hid_event {key_id, pressed, report_id}
                 └─→ hid_provider_keyboard ──→ HID 报告
                        ├─→ hids (BLE) ──→ 空中上报
                        └─→ usb_state (USB) ──→ USB 上报
selector_hw (模式开关,本项目为 ADC 检测)
   └─→ selector_event {selector_id, position}
          └─→ 各模块订阅:BLE 模块关/开、USB 模块开/关、LED 指示、显示屏刷新
battery_meas ──→ battery_event ──→ 显示屏/LED 刷新
[新增] display 模块(自研)
   └─→ 订阅 selector_event / hid_state_event / battery_event / click_event
          └─→ LVGL 界面刷新(电量、模式、连接状态、按键提示)
```

### 2.4 与 Zephyr 的关系

- CAF 不是替代 Zephyr,而是 **Zephyr 之上的应用事件框架**;
- 底层全部是 Zephyr:GPIO 驱动、BLE 协议栈、USB 协议栈、LVGL 显示、Flash 存储(settings)、DFU 等;
- 本项目使用 Zephyr 4.2.99(NCS v3.2.3),nRF Desktop 与当前 NCS 版本匹配,可直接移植。

---

## 3. 三模切换方案(USB / 蓝牙 / 2.4G)

### 3.1 硬件现状(依据网表,已核实)

三模开关 U1 = **SK-13D07-4**(6 脚滑动开关),连接:

```
3V3 ── R16(100k) ──┬── U1.3 ── R17(100k) ── GND      (分压节点)
3V3 ── R19(1k)   ── U1.4
GND ── R18(1k)   ── U1.1
GND ── R20(1k)   ── U1.5
U1.2 ── R5(10k)  ── MODE ── U36.8 (AI5)
```

- SK-13D07-4 不同档位将 U1.2(MODE)接到不同电阻网络,AI5 读出**不同模拟电压**;
- 即:**单引脚 ADC 电平三模检测**(而非 nRF Desktop selector 的多 GPIO 组合)。

### 3.2 实现方案

| 方案 | 说明 | 推荐 |
| --- | --- | --- |
| **A. Zephyr ADC + 自写模式检测模块** | 用 Zephyr ADC 驱动采样 AI5(P0.29/AIN5),按电压区间映射 3 档,发布自定义 `mode_event`(可复用 CAF `selector_event` 语义);BLE/USB/显示模块订阅切换 | ✅ **推荐**。与 CAF 事件架构一致,后续易扩展 |
| B. 直接 GPIO 组合 | 若后续改硬件为多引脚,可换用 nRF Desktop selector_hw 原样 | 备选 |

> 说明:本项目 MODE 走 ADC,与 nRF Desktop selector_hw(GPIO 数字组合)不同,故不直接复用 selector_hw.c,但**复用其事件模型**(selector_event: id + position),保证下游模块(USB/BLE/显示)逻辑与 nRF Desktop 一致。

### 3.3 模式与行为映射

| 档位(建议) | AI5 电压 | 模式 | 行为 |
| --- | --- | --- | --- |
| 1 | 接近 3V3 | 蓝牙 | BLE HID 广播/连接,USB 仅充电 |
| 2 | 中间 | USB | USB HID 上报,关闭 BLE |
| 3 | 接近 GND | 2.4G(预留) | 预留扩展(当前无 2.4G 芯片) |

> 具体电压阈值需实测(取决于 SK-13D07-4 档位触点组合),实现时用 ADC 多次采样 + 迟滞(滞回)判断,避免临界抖动。**建议在验证阶段用万用表实测三档电压后定阈值。**

---

## 4. 显示屏方案(LVGL)

### 4.1 现状

- 原理图:屏幕接口 P3(8P):SCREEN_BLK/RES/DC/SDA(SPI MOSI)/SCL/CS + 3V3 + GND;
- SPI 屏(如 1.3"/1.54" OLED SSD1306/SSD1315,或 LCD ST7789);
- nRF Desktop **无显示模块**,需自研;
- Zephyr 已内置:**LVGL**(`zephyr/modules/lvgl`)+ 显示驱动(`drivers/display`:ssd1306/st7789v/st7735r/ili9xxx 等)+ **LVGL input 桥接**(`zephyr/dts/bindings/input/zephyr,lvgl-*`)。

### 4.2 方案设计

| 层 | 组件 | 说明 |
| --- | --- | --- |
| 显示驱动 | Zephyr `display` 驱动(按所选屏型号启用,如 ssd1306 或 st7789v) | SPI + DC/RES/BLK/CS 由 devicetree 配置 |
| 图形库 | **LVGL v8**(Zephyr 内置模块) | 界面渲染 |
| CAF display 模块(自研,~150-300 行) | 订阅 `selector_event`/`battery_event`/`hid_state_event`/`click_event`/BLE 连接事件 | 刷新:模式图标、电量、连接状态、Fn 层提示、旋钮操作反馈 |
| 背光控制 | SCREEN_BLK(P1.11)可作 PWM 或 GPIO 控制 | 空闲熄屏、按键唤醒亮屏 |

### 4.3 事件 → 显示内容映射(建议)

| 事件 | 显示内容 |
| --- | --- |
| selector_event(模式) | 顶部状态栏:蓝牙 / USB / 2.4G 图标 |
| battery_event | 电池图标 + 百分比(配合 BAT_ADC / IP5306 I2C 电量) |
| ble 连接/断开 | 蓝牙图标亮/灰,设备名 |
| hid 按键 / click_event | 当前 Fn 层、旋钮模式提示 |
| 无操作超时 | 熄屏(背光关) |

> LVGL 刷屏在 nRF52840(64MHz, 256KB RAM)上可行;若屏幕分辨率 ≤128×64 OLED,资源开销很小。**建议首期屏幕选 0.96"/1.3" SPI OLED(SSD1306/SSD1315)**,驱动成熟、功耗低。

---

## 5. 整体软件架构(CAF + Zephyr 融合)

```
┌────────────────────────────────────────────────────────────┐
│  应用层(基于 CAF 事件架构,nRF Desktop 风格)                │
│                                                            │
│  buttons(矩阵扫描)  click_detector  fn_keys  hid_state      │
│  hid_provider_keyboard  hids(BLE)  usb_state(USB)          │
│  mode_detect(ADC 三模,自研)  display(LVGL,自研)            │
│  battery  led_state  dfu  power_manager  settings          │
└──────────────────────────────┬─────────────────────────────┘
                               │ CAF app_event_manager
┌──────────────────────────────┴─────────────────────────────┐
│  Zephyr 底层                                                │
│  GPIO / ADC / SPI / PWM / UART                              │
│  BLE 协议栈(Host + Controller)  USB 协议栈  LVGL            │
│  Flash:settings / MCUboot / DFU                             │
└─────────────────────────────────────────────────────────────┘
```

---

## 6. 与"矩阵按键.md"的衔接

- 方案 B(CAF buttons)升级为**正式推荐方案**,取代原先"方案 A(Zephyr input)为主"的建议;
- 矩阵扫描:CAF buttons 的 `buttons_def.h` 填本项目 4 列 × 6 行 GPIO(见矩阵按键.md §3.1);
- 键帽映射:CAF 用 `hid_keymap_def.h`(KEY_ID(col,row) → HID 键码),与矩阵按键.md §4.3 的键帽表一一对应;
- 旋钮:CAF buttons 不含旋转解码,旋钮 A/B 仍用 Zephyr `gpio-qdec`(可发 CAF 事件或直接消费),旋钮按键并入矩阵;
- 三模与显示屏为本评估新增内容,后续并入整体规划。

---

## 7. 工作量与风险

### 7.1 工作量估算(建议顺序)

| 阶段 | 内容 | 工作量(参考) |
| --- | --- | --- |
| M1 | 移植 nRF Desktop 框架到本项目 board,点亮板卡 | 1-2 天 |
| M2 | 填 buttons_def.h + hid_keymap_def.h,矩阵按键可用(BLE HID) | 1-2 天 |
| M3 | ADC 三模检测(mode_detect)+ USB/BLE 切换 | 1-2 天 |
| M4 | EC11 旋钮(gpio-qdec + CAF 事件)+ 音量/滚动 | 1 天 |
| M5 | LVGL 显示屏 CAF 模块 + 界面 | 2-3 天 |
| M6 | 电池(BAT_ADC + IP5306 I2C)+ 低功耗 + 整机联调 | 2-3 天 |

### 7.2 风险与对策

| 风险 | 对策 |
| --- | --- |
| nRF Desktop 配置复杂(18 个 board 配置) | 以 nrf52kbd_nrf52832 为模板,删减不需要的模块(fast_pair/dfu 等按需) |
| CAF 依赖 app_event_manager,学习曲线 | 事件流清晰(2.3 节),先跑通最小闭环(buttons→hid_state→hids) |
| 三模 ADC 阈值未实测 | 验证阶段万用表实测三档电压,加迟滞判断 |
| 显示屏增加功耗/内存 | 选小屏(≤128×64)、空闲熄屏、LVGL 精简配置 |
| NCS 版本升级导致 API 变动 | 锁定 NCS v3.2.3,nRF Desktop 与当前版本匹配 |

---

## 8. 待确认问题(请确认后实施)

1. **是否采用 CAF buttons + nRF Desktop 方案作为最终方案?**(推荐:是)
2. **三模档位定义**:确认"蓝牙 / USB / 2.4G(预留)"三档的 ADC 电压区间(实现前需实测);
3. **屏幕型号**:首期是否选 **0.96"/1.3" SPI OLED(SSD1306/SSD1315)**?(也可用 ST7789 LCD)
4. **旋钮首期功能**:音量调节(Consumer Control)还是页面滚动?
5. **2.4G 档**:本期是否只是占位(无 2.4G 芯片),后续硬件扩展?
6. **是否保留 Zephyr input 子系统方案作为备选**?(文档中仍保留对比)
7. **移植范围**:nRF Desktop 的 DFU/快速配对(fast_pair)/配置文件(settings)等是否本期都要,还是先裁剪?

---

## 9. 参考资料

1. nRF Desktop:`F:\ncs\v3.2.3\nrf\applications\nrf_desktop`(src/modules、src/hw_interface、configuration/nrf52kbd_nrf52832)
2. CAF buttons:`nrf/subsys/caf/modules/buttons.c`、`Kconfig.buttons`
3. CAF selector 参考:`nrf_desktop/src/hw_interface/selector_hw.c` + `selector_hw_def.h` + `nrf_desktop/src/events/selector_event.*`
4. Zephyr LVGL:`zephyr/modules/lvgl`、`zephyr/drivers/display`
5. 本项目:`Netlist_蓝牙小键盘_2026-05-22.tel`、`SCH_蓝牙小键盘_2026-03-28.pdf`、`src/doc/矩阵按键.md`、`src/doc/整体规划.md`
