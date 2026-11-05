#!/usr/bin/env python3
# Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE).
"""Compares two Opus plugin kits and flags what a plugin must fix.

    python3 tools/opus_kit_diff.py OLD_KIT NEW_KIT [PLUGIN_DIR ...]

OLD_KIT / NEW_KIT: plugin-kit folders (the one with sdk/ and tools/), e.g. the beta 3.2 kit (d7f28b0) and Beta 4's.
PLUGIN_DIR: a plugin's source folder (default: plugin/sammi.sister). Any plugin works: the scan is generic.

Reports, from the kits' own files (nothing is assumed about Beta 4):
  - functions in sdk/opus_plugin.h and sdk/op1fw.h: removed, added, signature changed
  - structs (opus_effect, opus_fx, opus_need, opus_fx_view, ...): fields changed
  - #define constants: removed or value changed
  - the build's compiler flags (tools/op1mod/build.py, plugin.py, opmod.py) and the toolchain versions it names
  - manifest keywords (docs/manifest.md) and the core-version table (docs/api.md)
Then, per plugin, FLAG lines for every function, struct, constant or manifest keyword the plugin uses that changed or
went away. Exit status 1 when anything is flagged, 0 when the plugin needs nothing.
"""
import re
import sys
from pathlib import Path

HEADERS = ('sdk/opus_plugin.h', 'sdk/op1fw.h')


def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', ' ', s, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', s)


def norm(s):
    return re.sub(r'\s+', ' ', s).strip()


def parse_header(text):
    """functions {name: normalized declaration}, structs {name: normalized body}, defines {name: value}"""
    raw = text
    text = strip_comments(text)
    defines = {}
    for m in re.finditer(r'^\s*#\s*define\s+(\w+)(?:\(([^)]*)\))?[ \t]*(.*)$', raw, flags=re.M):
        defines[m.group(1)] = norm(strip_comments(m.group(3)))
    body = re.sub(r'^\s*#.*$', ' ', text, flags=re.M)
    structs = {}
    for m in re.finditer(r'\bstruct\s+(\w+)\s*\{', body):
        depth, i = 1, m.end()
        while depth and i < len(body):
            depth += {'{': 1, '}': -1}.get(body[i], 0)
            i += 1
        structs[m.group(1)] = norm(body[m.end():i - 1])
    flat = body
    for _ in range(4):                                  # drop struct bodies so their members don't look like functions
        flat = re.sub(r'\{[^{}]*\}', ' ', flat)
    funcs = {}
    for decl in flat.split(';'):
        d = norm(decl)
        m = re.search(r'\b((?:opus|fw|opl)_\w+)\s*\(', d)
        if m and not d.startswith('#'):
            funcs[m.group(1)] = d
            continue
        m = re.search(r'\bextern\s+(\w+_t)\s+((?:opus|fw)_\w+)\b', d)     # op1fw.h: extern fw_x_t fw_x;
        if m:
            funcs[m.group(2)] = d
        m = re.search(r'typedef\s+(.*?)\b((?:opus|fw)_\w+_t)\s*\((.*)\)', d)
        if m:
            funcs[m.group(2)] = d
    return funcs, structs, defines


def kit(path):
    p = Path(path)
    if (p / 'plugin-kit').is_dir():
        p = p / 'plugin-kit'
    if not (p / 'sdk' / 'opus_plugin.h').is_file():
        sys.exit(f'{path}: not a plugin kit (no sdk/opus_plugin.h)')
    funcs, structs, defines = {}, {}, {}
    for h in HEADERS:
        f = p / h
        if f.is_file():
            a, b, c = parse_header(f.read_text(errors='replace'))
            funcs.update(a), structs.update(b), defines.update(c)
    flags = set()
    for t in ('tools/op1mod/build.py', 'tools/op1mod/plugin.py', 'tools/op1mod/opmod.py'):
        f = p / t
        if f.is_file():
            flags |= set(re.findall(r"'(-[mfOWgI][\w=.+-]*)'", f.read_text(errors='replace')))
    versions = set()
    for f in [p / 'tools/toolchain/build-bfin-toolchain.sh', p / 'docs/getting-started.md']:
        if f.is_file():
            versions |= set(re.findall(r'\b(gcc-\d+\.\d+\.\d+|binutils-\d+\.\d+(?:\.\d+)?|GCC \d+\.\d+(?:\.\d+)?)', f.read_text(errors='replace')))
    keywords = set()
    f = p / 'docs/manifest.md'
    if f.is_file():
        keywords = set(re.findall(r'^#+\s*`(\w+)`', f.read_text(errors='replace'), flags=re.M))
    cores = []
    f = p / 'docs/api.md'
    if f.is_file():
        cores = re.findall(r'^\|\s*(\d+\.\d+)\s*\|\s*(.*?)\s*\|\s*$', f.read_text(errors='replace'), flags=re.M)
    return {'funcs': funcs, 'structs': structs, 'defines': defines, 'flags': flags, 'versions': versions,
            'keywords': keywords, 'cores': dict(cores)}


def plugin_uses(d):
    d = Path(d)
    src = ''.join(f.read_text(errors='replace') for f in sorted(d.glob('*')) if f.suffix in ('.c', '.h', '.S'))
    code = strip_comments(src)
    calls = set(re.findall(r'\b((?:opus|fw)_\w+)\s*\(', code)) | set(re.findall(r'\b((?:opus|fw)_\w+_t)\b', code))
    structs = set(re.findall(r'\bstruct\s+(opus_\w+|opl_\w+)\b', code))
    consts = set(re.findall(r'\b(OPUS_[A-Z0-9_]+|OP1_[A-Z0-9_]+)\b', code))
    keys, core = set(), None
    m = d / 'manifest.txt'
    if m.is_file():
        for line in m.read_text().splitlines():
            w = line.split()
            if w and not w[0].startswith('#'):
                keys.add(w[0])
                if w[0] == 'core' and len(w) > 1:
                    core = w[1]
    return calls, structs, consts, keys, core


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    old, new = kit(sys.argv[1]), kit(sys.argv[2])
    plugins = sys.argv[3:] or [str(Path(__file__).resolve().parents[1] / 'plugin' / 'sammi.sister')]
    out = []

    def section(title, rows):
        out.append(f'\n== {title}')
        out.extend(rows if rows else ['  (no change)'])

    of, nf = old['funcs'], new['funcs']
    section('functions', [f'  removed  {n}' for n in sorted(of.keys() - nf.keys())] +
            [f'  added    {n}' for n in sorted(nf.keys() - of.keys())] +
            [f'  changed  {n}\n      was: {of[n]}\n      now: {nf[n]}' for n in sorted(of.keys() & nf.keys()) if of[n] != nf[n]])
    os_, ns = old['structs'], new['structs']
    section('structs', [f'  removed  struct {n}' for n in sorted(os_.keys() - ns.keys())] +
            [f'  added    struct {n}' for n in sorted(ns.keys() - os_.keys())] +
            [f'  changed  struct {n}\n      was: {{ {os_[n]} }}\n      now: {{ {ns[n]} }}' for n in sorted(os_.keys() & ns.keys()) if os_[n] != ns[n]])
    od, nd = old['defines'], new['defines']
    section('constants', [f'  removed  {n}' for n in sorted(od.keys() - nd.keys()) if not n.endswith('_H')] +
            [f'  changed  {n}: {od[n]} -> {nd[n]}' for n in sorted(od.keys() & nd.keys()) if od[n] != nd[n]])
    section('compiler flags', [f'  removed  {x}' for x in sorted(old['flags'] - new['flags'])] +
            [f'  added    {x}' for x in sorted(new['flags'] - old['flags'])])
    section('toolchain versions named', [f'  was {sorted(old["versions"])}', f'  now {sorted(new["versions"])}']
            if old['versions'] != new['versions'] else [])
    section('manifest keywords', [f'  removed  {k}' for k in sorted(old['keywords'] - new['keywords'])] +
            [f'  added    {k}' for k in sorted(new['keywords'] - old['keywords'])])
    section('core versions', [f'  new core {c}: {new["cores"][c]}' for c in sorted(new['cores'].keys() - old['cores'].keys(),
                                                                               key=lambda v: [int(x) for x in v.split('.')])])

    changed_f = (of.keys() - nf.keys()) | {n for n in of.keys() & nf.keys() if of[n] != nf[n]}
    changed_s = (os_.keys() - ns.keys()) | {n for n in os_.keys() & ns.keys() if os_[n] != ns[n]}
    changed_d = (od.keys() - nd.keys()) | {n for n in od.keys() & nd.keys() if od[n] != nd[n]}
    flagged = 0
    for pd in plugins:
        calls, structs, consts, keys, core = plugin_uses(pd)
        rows = []
        for n in sorted(calls):
            if n in changed_f:
                rows.append(f'  FLAG function {n}: ' + ('removed' if n not in nf else 'signature changed'))
            elif n not in nf and n in of:
                rows.append(f'  FLAG function {n}: not in the new kit')
            elif n not in nf and n not in of:
                rows.append(f'  note function {n}: in neither kit (the plugin\'s own?)')
        rows += [f'  FLAG struct {n}: ' + ('removed' if n not in ns else 'fields changed') for n in sorted(structs) if n in changed_s]
        rows += [f'  FLAG constant {n}: ' + ('removed' if n not in nd else f'{od[n]} -> {nd[n]}') for n in sorted(consts) if n in changed_d]
        rows += [f'  FLAG manifest keyword {k}: not documented in the new kit' for k in sorted(keys)
                 if old['keywords'] and new['keywords'] and k in old['keywords'] and k not in new['keywords']]
        if old['flags'] != new['flags']:
            rows.append('  FLAG compiler flags changed: rebuild and re-run the tests (warnings are errors)')
        if old['versions'] != new['versions']:
            rows.append('  FLAG the kit names another toolchain version: decide on the shared toolchain first')
        n_flags = sum(r.startswith('  FLAG') for r in rows)
        flagged += n_flags
        section(f'plugin {pd} (core {core}; {len(calls)} kit names used): {n_flags} to fix', [r for r in rows if r.strip()])
    print('\n'.join(out).lstrip('\n'))
    print(f'\n{flagged} item(s) flagged.' if flagged else '\nNothing flagged: rebuild unchanged and run the checklist.')
    sys.exit(1 if flagged else 0)


if __name__ == '__main__':
    main()
