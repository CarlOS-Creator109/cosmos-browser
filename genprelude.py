#!/usr/bin/env python3
"""Genera prelude.h (cadena C) a partir de prelude.js."""
import sys
src = open(sys.argv[1], encoding='utf-8').read()
out = ['/* generado por genprelude.py - no editar */', 'static const char PRELUDE_JS[] ='] 
for line in src.split('\n'):
    b = line.encode('utf-8')
    s = ''.join(chr(c) if 32 <= c < 127 and chr(c) not in '"\\?' else '\\%03o' % c for c in b)
    out.append('"%s\\n"' % s)
out.append(';')
open(sys.argv[2], 'w').write('\n'.join(out) + '\n')
