#!/usr/bin/env python3
"""Builds the VoyagerCalc User Manual PDF from manual.src.html (+ key tables, optional SHA256SUMS) with WeasyPrint.

usage: build.py [--out FILE.pdf] [--html FILE.html]
Default output: ../VoyagerCalc-User-Manual.pdf (that is docs/VoyagerCalc-User-Manual.pdf, which the packaging scripts bundle).
Needs: pip install weasyprint. Put the release's SHA256SUMS next to this file to list it in Appendix A.
"""
import argparse, re, html, pathlib, weasyprint
VERSION = '0.7.4'
D = pathlib.Path(__file__).resolve().parent
ap = argparse.ArgumentParser(); ap.add_argument('--out', default=str(D.parent / 'VoyagerCalc-User-Manual.pdf')); ap.add_argument('--html', default='')
a = ap.parse_args()
src = (D/'manual.src.html').read_text()
src = src.replace('{{KEYS16}}', (D/'keys-16C.html').read_text()).replace('{{KEYS15}}', (D/'keys-15C.html').read_text())
sf = D/'SHA256SUMS'
if sf.exists():
    sums = ''.join(f'<tr><td>{html.escape(n)}</td><td>{h}</td></tr>' for h, n in (l.split(None, 1) for l in sf.read_text().splitlines() if l.strip()))
    src = src.replace('{{SUMS}}', sums)
else:
    src = re.sub(r'<table class="sums">\s*\{\{SUMS\}\}\s*</table>', '', src)
# table of contents from h1 (with id) and h2 (ids added where missing)
n = 0
def addid(m):
    global n
    tag, attrs, text = m.group(1), m.group(2), m.group(3)
    if 'notoc' in attrs: return m.group(0)
    if 'id=' not in attrs:
        n += 1; attrs += f' id="s{n}"'
    return f'<{tag}{attrs}>{text}</{tag}>'
body_start = src.index('<!-- ============ 1 INTRO')
head, body = src[:body_start], src[body_start:]
body = re.sub(r'<(h[12])([^>]*)>(.*?)</\1>', addid, body, flags=re.S)
toc = []
for m in re.finditer(r'<(h[12])([^>]*)>(.*?)</\1>', body, flags=re.S):
    i = re.search(r'id="([^"]+)"', m.group(2)).group(1)
    toc.append(f'<li class="l{m.group(1)[1]}"><a href="#{i}">{m.group(3)}</a></li>')
src = (head + body).replace('<ul id="toc"></ul>', '<ul id="toc">' + '\n'.join(toc) + '</ul>')
if a.html: pathlib.Path(a.html).write_text(src)
doc = weasyprint.HTML(string=src, base_url=str(D)).render()
doc.metadata.title = f'VoyagerCalc {VERSION} User Manual'
doc.metadata.authors = ['The Calculator Store']
doc.write_pdf(a.out)
print('pages:', len(doc.pages), '->', a.out)
