# PlatformIO extra script: the device page for the firmware's web server, made at build time as PortalWeb.h
# in the build folder: web/index.html and the page's dictionaries (i18n/web/<lang>.json, as
# tools/web_i18n.cjs build makes them), each compressed with gzip and sent with Content-Encoding: gzip
# (src/Portal.cpp). The header is rewritten only when its contents change.
import gzip
import json
import os

Import('env')  # noqa: F821

root = env.subst('$PROJECT_DIR')  # noqa: F821
out_dir = os.path.join(env.subst('$BUILD_DIR'), 'web')  # noqa: F821
LANGS = ['en', 'uk', 'es', 'pt', 'fr', 'de', 'it', 'pl', 'tr', 'zh', 'ja', 'ko', 'ar', 'id']


def packed(data):
    return gzip.compress(data, compresslevel=9, mtime=0)


def array(name, data):
    rows = ','.join(str(b) for b in data)
    return f'static const uint8_t {name}[] PROGMEM={{{rows}}};\n'


def dictionary(code):
    path = os.path.join(root, 'i18n', 'web', code + '.json')
    with open(path, encoding='utf-8') as f:
        d = json.load(f)
    d = {k: v for k, v in d.items() if (v if isinstance(v, str) else any(v.values()))}
    text = json.dumps(d, ensure_ascii=False, separators=(',', ':')).replace('<', '\\u003c')
    return ('globalThis.MeshMeshWebDict=' + text + ';\n').encode()


with open(os.path.join(root, 'web', 'index.html'), 'rb') as f:
    page = f.read()
header = ['#pragma once\n// Made by tools/pio_web.py: web/index.html and i18n/web, gzip.\n#include <stdint.h>\n#include <stddef.h>\n',
          array('portalPage', packed(page)), 'static const size_t portalPageSize=sizeof(portalPage);\n']
langs = []
for code in LANGS:
    header.append(array('portalLang_' + code, packed(dictionary(code))))
    langs.append(f'{{"{code}",portalLang_{code},sizeof(portalLang_{code})}}')
header.append('struct PortalLang{const char* code;const uint8_t* data;size_t size;};\n')
header.append('static const PortalLang portalLangs[]={' + ','.join(langs) + '};\n')
text = ''.join(header)

os.makedirs(out_dir, exist_ok=True)
target = os.path.join(out_dir, 'PortalWeb.h')
if not os.path.exists(target) or open(target).read() != text:
    with open(target, 'w') as f:
        f.write(text)
env.Append(CPPPATH=[out_dir])  # noqa: F821
