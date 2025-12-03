#!/usr/bin/env python3
import re
from pathlib import Path

MAK = Path(__file__).resolve().parents[1] / 'Makefile'

text = MAK.read_text()

# Find VERSION_STRING line
m = re.search(r'^(VERSION_STRING\s*\?=\s*)(V[0-9]+\.[0-9]+\.[0-9]+)\s*$', text, flags=re.M)
if not m:
    print('VERSION_STRING not found in Makefile')
    raise SystemExit(1)

prefix = m.group(1)
ver = m.group(2)

vparts = ver.lstrip('V').split('.')
major, minor, patch = map(int, vparts)
patch += 1
newver = f'V{major}.{minor}.{patch}'

text = text[:m.start()] + prefix + newver + text[m.end():]

# Update PROJECT_NAME occurrence (safe replace of Vx.y.z inside it)
text = re.sub(r'(PROJECT_NAME\s*:?=\s*[^\n]*_V)([0-9]+\.[0-9]+\.[0-9]+)',
              lambda mm: mm.group(1) + newver.lstrip('V'),
              text)

MAK.write_text(text)

print(f'Bumped version: {ver} -> {newver}')
