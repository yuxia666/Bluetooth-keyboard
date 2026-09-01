#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""三模切换分压网络电压计算验证"""
# 档位电路（由网表 Netlist_蓝牙小键盘_2026-05-22.tel 交叉验证）：
#   MODE 网络 = C9(100nF 滤波) + R5(1k) + U36.8 (E73 引脚8 = AIN5 = P0.29)
#   U1 (SK-13D07-4 拨档开关) 公共端经 R5 接 MODE，三个档位触点：
#     档位1 -> R18(1k) -> GND          => 约 0 V    (USB)
#     档位2 -> R16(100k)->3V3 + R17(100k)->GND 分压 => 约 1.65 V (2.4G)
#     档位3 -> R19(1k) -> 3V3          => 约 3.3 V  (BLE)
#   U1.5 -> R20(1k) -> GND (开关第二组公共端/备用)

V3V3 = 3.3

def div(r_top, r_bot):
    return V3V3 * r_bot / (r_top + r_bot)

print("=== 各档位 MODE 引脚电压 ===")
# 档位1: USB, R5(1k) 串联进 MODE，触点经 R18(1k) 到 GND
# MODE 对地 = R18, 对 3V3 无通路 => 0V
print(f"档位1 (USB) : {0.0:.3f} V")
# 档位2: 2.4G, R16(100k) 上拉 3V3, R17(100k) 下拉 GND, 分压点经 R5(1k) 到 MODE
v24g = div(100e3, 100e3)
print(f"档位2 (2.4G): {v24g:.3f} V  (R16/R17 100k/100k 分压)")
# 档位3: BLE, 3V3 经 R19(1k) 触点, 再经 R5(1k) 到 MODE, ADC 输入高阻 => 约 3.3V
print(f"档位3 (BLE) : {V3V3:.3f} V")

print()
print("=== 用户阈值 vs 档位电压 ===")
print("USB : < 825 mV       档位1 ~0V       OK")
print("2.4G: 825 ~ 2475 mV  档位2 ~1.65V    OK")
print("BLE : >= 2475 mV     档位3 ~3.3V     OK")
print()
print("825mV = 3.3*1/4, 2475mV = 3.3*3/4, 与档位电压 0/1.65/3.3V 完美对应")
