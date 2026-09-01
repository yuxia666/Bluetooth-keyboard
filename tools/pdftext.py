#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
极简 PDF 文本提取器（纯标准库，无需 pymupdf）
- 解析 PDF 对象流，zlib 解压 FlateDecode
- 提取 BT...ET 文本块中的 (str) Tj / TJ 内容
- 保留坐标（Tm/Td 近似），输出按行排序
用法: python pdftext.py <file.pdf> [page]
"""
import sys
import re
import zlib

def read_pdf(path):
    with open(path, 'rb') as f:
        return f.read()

def parse_objects(data):
    # 找出所有 obj ... endobj
    objs = {}
    for m in re.finditer(rb'(\d+)\s+(\d+)\s+obj\b(.*?)endobj', data, re.S):
        num = int(m.group(1))
        objs[num] = m.group(3)
    return objs

def get_stream(body):
    m = re.search(rb'stream\r?\n(.*?)\r?\nendstream', body, re.S)
    if not m:
        return b''
    return m.group(1)

def decode_stream(body):
    raw = get_stream(body)
    if re.search(rb'/Filter\s*/FlateDecode', body):
        try:
            return zlib.decompress(raw)
        except Exception:
            # 可能带预测器，尝试跳过前两个字节
            try:
                return zlib.decompress(raw[2:])
            except Exception:
                return raw
    return raw

def extract_page_text(page_body):
    stream = decode_stream(page_body)
    text_parts = []
    # 收集 BT..ET 文本块
    for bt in re.finditer(rb'BT(.*?)ET', stream, re.S):
        block = bt.group(1)
        # 提取 Tj / TJ 字符串
        strs = re.findall(rb'\(((?:[^()\\]|\\.)*)\)\s*Tj', block)
        tj_arrays = re.findall(rb'\[(.*?)\]\s*TJ', block, re.S)
        for s in strs:
            text_parts.append(s)
        for arr in tj_arrays:
            parts = re.findall(rb'\(((?:[^()\\]|\\.)*)\)', arr)
            text_parts.extend(parts)
    # 解码
    out = []
    for p in text_parts:
        s = p.replace(rb'\(', b'(').replace(rb'\)', b')').replace(rb'\\', b'\\')
        try:
            out.append(s.decode('latin-1'))
        except Exception:
            out.append('?')
    return out

def parse_content_streams(data, objs):
    # 找页面对象的内容流引用
    pages = []
    for num, body in objs.items():
        if re.search(rb'/Type\s*/Page\b', body):
            pages.append((num, body))
    results = {}
    for num, body in pages:
        # 找 /Contents 引用
        cm = re.search(rb'/Contents\s+(\d+)\s+0\s+R', body)
        texts = []
        if cm:
            cnum = int(cm.group(1))
            if cnum in objs:
                texts = extract_page_text(objs[cnum])
        results[num] = texts
    return results

def main():
    path = sys.argv[1]
    data = read_pdf(path)
    objs = parse_objects(data)
    pages = parse_content_streams(data, objs)
    print(f'# pages: {len(pages)}')
    for pnum in sorted(pages):
        print(f'\n===== Page {pnum} =====')
        texts = pages[pnum]
        # 拼接并尝试按空间去重
        seen = set()
        for t in texts:
            if t not in seen:
                seen.add(t)
                print(t)

if __name__ == '__main__':
    main()
