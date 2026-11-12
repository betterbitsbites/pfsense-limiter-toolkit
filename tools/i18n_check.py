#!/usr/bin/env python3
"""Lists GUI texts without a German translation and placeholder mismatches.

Texts are English in the code (scg_t('...') in PHP, t('...') in page JavaScript,
state texts in shaperclassgen_autorate.inc). German translations live in
scg_dict_de() in files/usr/local/pkg/shaperclassgen_i18n.inc.
Usage: tools/i18n_check.py   (exit code 1 if something is missing)
"""
import glob, json, os, re, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'files')
I18N = os.path.join(ROOT, 'usr/local/pkg/shaperclassgen_i18n.inc')
php = 'function config_get_path($a,$b=null){return "";} require "%s"; echo json_encode(scg_dict_de());' % I18N
de = json.loads(subprocess.check_output(['php', '-r', php]))

def unq(s, q):
    return s.replace('\\' + q, q).replace('\\\\', '\\')

keys = []
for f in glob.glob(ROOT + '/usr/local/pkg/*.inc') + glob.glob(ROOT + '/usr/local/www/*.php'):
    if f.endswith('_i18n.inc'):
        continue
    src = open(f, encoding='utf-8').read()
    keys += [unq(m.group(1), "'") for m in re.finditer(r"\b(?:scg_t|t)\(\s*'((?:[^'\\]|\\.)*)'", src)]
    keys += [unq(m.group(1), '"') for m in re.finditer(r'\b(?:scg_t|t)\(\s*"((?:[^"\\]|\\.)*)"', src)]
src = open(os.path.join(ROOT, 'usr/local/pkg/shaperclassgen_autorate.inc'), encoding='utf-8').read()
blk = src[src.index('function scg_ar_state_texts'):src.index('foreach ($t as $k => $v)')]
keys += [m.group(1) for m in re.finditer(r"=> '((?:[^'\\]|\\.)*)'", blk)]

ph = lambda s: sorted(re.findall(r'%[sd%]', s))
bad = 0
for k in sorted(set(keys)):
    if k not in de:
        print('MISSING   ', k); bad += 1
    elif ph(k) != ph(de[k]):
        print('PLACEHOLDER', k, '=>', de[k]); bad += 1
print('%d texts, %d translations, %d problems' % (len(set(keys)), len(de), bad))
sys.exit(1 if bad else 0)
