# -*- coding: utf-8 -*-
"""从网表提取三模切换相关网络做交叉验证"""
import re

with open(r'F:\ble_key_workspace\01_code\src\source\Netlist_蓝牙小键盘_2026-05-22.tel', encoding='utf-8') as f:
    data = f.read()

nets = data.split('$NETS')[1].split('$SCHEDULE')[0]
pkg = data.split('$PACKAGES')[1].split('$A_PROPERTIES')[0]

# 电阻值表
resistor_map = {}
for m in re.finditer(r"R0402 ! R0402 ! '([^']+)' ; ([\w\s,]+)", pkg):
    val, comps = m.group(1), m.group(2)
    for c in re.findall(r'R\d+', comps):
        resistor_map[c] = val
print('=== 电阻值表 ===')
for k, v in sorted(resistor_map.items(), key=lambda x: int(x[0][1:])):
    print(f'  {k}: {v}')

print()
print('=== 三模切换相关网络 ===')
for line in nets.splitlines():
    line = line.strip()
    if not line:
        continue
    if any(k in line for k in ['MODE', 'R5.', 'R16.', 'R17.', 'R18.', 'R19.', 'R20.', 'C9.']):
        print(f'  {line}')
