"""The mechanical rename after the retarget: in the CODE of src/ and tests/ (not in // comments), every 3663 that is
not part of a number (identifiers such as kFoo3663, v3663, is_3663, and "1.0.0.3663" inside string literals, and the
enum value 3663 itself) becomes 3732. Hex literals and longer decimal numbers are left alone. --dry lists the lines."""
import io
import re
import sys
from pathlib import Path

ROOT = Path(r'D:/SteamLibrary/steamapps/common/Crimson Desert/multi')
DRY = '--dry' in sys.argv
TOKEN = re.compile(r'0[xX][0-9A-Fa-f]+|[A-Za-z_][A-Za-z0-9_]*|[0-9]+(?:\.[0-9]+)*|.', re.S)


def split_comment(line):
    """(code, comment): the // comment that is not inside a string or char literal."""
    i, quote = 0, None
    while i < len(line):
        c = line[i]
        if quote:
            if c == '\\':
                i += 2
                continue
            if c == quote:
                quote = None
        elif c in '"\'':
            quote = c
        elif c == '/' and line[i:i + 2] == '//':
            return line[:i], line[i:]
        i += 1
    return line, ''


def fix_code(code):
    out = []
    for m in TOKEN.finditer(code):
        t = m[0]
        if re.fullmatch(r'0[xX][0-9A-Fa-f]+', t):
            out.append(t)
        elif re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', t):
            out.append(re.sub(r'(?<![0-9])3663(?![0-9])', '3732', t))
        elif re.fullmatch(r'[0-9]+(?:\.[0-9]+)*', t):
            out.append('.'.join('3732' if part == '3663' else part for part in t.split('.')))
        else:
            out.append(t)
    return ''.join(out)


changed_files = changed_lines = 0
for base in ('src', 'tests'):
    for path in sorted((ROOT / base).rglob('*')):
        if path.suffix not in ('.h', '.cpp', '.inc'):
            continue
        # tests: only the one that names the build's identifiers; the others mention 3663 as fixture data or history
        if base == 'tests' and path.name != 'native_opcode_tests.cpp':
            continue
        text = io.open(path, encoding='utf-8', newline='').read()
        if '3663' not in text:
            continue
        lines = text.split('\n')
        n = 0
        in_block = False
        for i, line in enumerate(lines):
            body = line[:-1] if line.endswith('\r') else line
            tail = line[len(body):]
            if in_block:
                if '*/' in body:
                    in_block = False
                continue
            if body.lstrip().startswith('/*') and '*/' not in body:
                in_block = True
                continue
            code, comment = split_comment(body)
            new = fix_code(code)
            if new != code:
                n += 1
                if DRY and n <= 2:
                    print(f'  {path.relative_to(ROOT).as_posix()}:{i + 1}: {new.strip()[:120]}')
                lines[i] = new + comment + tail
        if n:
            changed_files += 1
            changed_lines += n
            if not DRY:
                io.open(path, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print(f'{"would change" if DRY else "changed"} {changed_lines} lines in {changed_files} files')
