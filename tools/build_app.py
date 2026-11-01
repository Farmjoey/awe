#!/usr/bin/env python3
"""Builds app/index.html (one self-contained file) from app/src/index.template.html:
inlines app/widgetlib.js and the starter widgets, so the app also works as a single file."""
import json, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
tpl = (root / 'app/src/index.template.html').read_text(encoding='utf-8')
lib = (root / 'app/widgetlib.js').read_text(encoding='utf-8')
idx = json.loads((root / 'app/widgets/starter/index.json').read_text())
starters = [json.loads((root / f'app/widgets/starter/{n}.json').read_text()) for n in idx['widgets']]
inline = lib + '\nwindow.STARTERS = ' + json.dumps(starters, ensure_ascii=True) + ';\n'
assert '/*INLINE*/' in tpl
out = tpl.replace('/*INLINE*/', inline, 1)
(root / 'app/index.html').write_text(out, encoding='utf-8')
print('app/index.html', len(out), 'bytes')
