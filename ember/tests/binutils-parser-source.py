#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
"""Test host-binutils parser rules through the real BSD/GNU make boundary.

Use an existing build.sh make wrapper and its host lex/yacc/GNU make tools.
No target binaries run. All generated fixtures use a temporary directory.
"""

import argparse
from pathlib import Path
import shlex
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def rule(text, suffix):
    start = text.index(suffix + ':\n')
    end = text.index('\n\n', start)
    result = text[start:end] + '\n'
    require('$(SHELL) $(YLWRAP)' in result, 'unexpected upstream parser rule')
    return result


def invoke(command, cwd):
    return subprocess.run(['/bin/sh', '-c', command], cwd=cwd,
                          text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--make-wrapper', type=Path, required=True)
    parser.add_argument('--makefile', type=Path,
                        help='candidate tools/binutils/Makefile; default: source copy')
    args = parser.parse_args()
    source = args.source.resolve()
    makefile = (args.makefile or source / 'tools/binutils/Makefile').resolve()
    dist = source / 'external/gpl3/binutils/dist'
    template = (dist / 'ld/Makefile.in').read_text()
    rules = rule(template, '.l.c') + '\n' + rule(template, '.y.c')
    yacc_conversion = template[template.index('am__yacc_c2h ='):template.index('YACCCOMPILE =')]
    guards = 'am__skiplex =\nam__skipyacc =\n'
    require(guards in (dist / 'ld/Makefile.am').read_text(),
            'upstream no longer overrides the standard guards; review this test')
    require('BINUTILS_REGENERATE_PARSERS' in makefile.read_text(),
            'candidate lacks the explicit parser-maintainer option')
    commands = {}
    for mode in ('no', 'yes'):
        commands[mode] = subprocess.check_output([
            str(args.make_wrapper.resolve()), '-C', str(source / 'tools/binutils'),
            '-f', str(makefile), 'BINUTILS_REGENERATE_PARSERS=' + mode,
            '-V', '${BUILD_COMMAND}'], text=True).strip()
        require(commands[mode].startswith('/usr/bin/env -i '),
                'test needs the actual GNU make BUILD_COMMAND')
    require("am__skiplex='test -f $@ ||'" in commands['no'] and
            "am__skipyacc='test -f $@ ||'" in commands['no'],
            'BSD make did not preserve the automatic variable for GNU make')
    require('am__skiplex=' not in commands['yes'],
            'explicit maintainer mode must enable upstream regeneration')
    lexer = '%%\n. ;\n\\n ;\n%%\n'
    grammar = '%token WORD\n%%\nstart: WORD ;\n%%\n'
    marker = '/* shipped parser: must remain unchanged */\n'
    with tempfile.TemporaryDirectory(prefix='binutils-parser-source-') as tmp:
        root = Path(tmp)
        for mode in ('no', 'yes'):
            work = root / mode
            (work / 'src').mkdir(parents=True)
            (work / 'obj').mkdir()
            old = []
            for stem, suffix, content in (('lexer', 'l', lexer),
                                          ('parser', 'y', grammar)):
                target = work / 'src' / (stem + '.c')
                target.write_text(marker)
                target.chmod(0o444 if mode == 'no' else 0o644)
                original = (target.read_bytes(), target.stat().st_mode)
                old.append((target, original))
                (target.with_suffix('.' + suffix)).write_text(content)
                (work / 'obj' / ('fresh' + stem + '.' + suffix)).write_text(content)
            # The recursive make inherits the command-line values through
            # MAKEFLAGS. This catches early expansion of $@ in the environment.
            (work / 'Makefile').write_text('all:\n\t$(MAKE) -f child.mk all\n')
            child = ('.SUFFIXES: .l .y .c\n' + guards +
                     'SHELL = /bin/sh\nYLWRAP = ' + shlex.quote(str(dist / 'ylwrap')) + '\n' +
                     'LEX_OUTPUT_ROOT = lex.yy\nLEXCOMPILE = $(LEX)\n'
                     'YACCCOMPILE = $(YACC) -d\n' + yacc_conversion +
                     'all: src/lexer.c src/parser.c obj/freshlexer.c obj/freshparser.c\n\n' + rules)
            (work / 'child.mk').write_text(child)
            # -B asks GNU make to execute the rule even on coarse-timestamp
            # filesystems. No source timestamp is changed by the test.
            result = invoke(commands[mode] + ' -B all', work)
            require(result.returncode == 0, mode + ': ' + result.stdout)
            for target, original in old:
                current = (target.read_bytes(), target.stat().st_mode)
                if mode == 'no':
                    require(current == original, 'normal build rewrote ' + target.name)
                else:
                    require(current[0] != original[0],
                            'baseline/maintainer build did not reproduce source regeneration')
            for stem in ('lexer', 'parser'):
                require((work / 'obj' / ('fresh' + stem + '.c')).is_file(),
                        'guard suppressed a missing output')
            # A missing output with an invalid grammar must still fail.
            for suffix in ('l', 'y'):
                bad = work / 'obj' / ('invalid.' + suffix)
                bad.write_text('this is not a valid grammar\n')
                result = invoke(commands[mode] + ' -f child.mk obj/invalid.c', work)
                require(result.returncode != 0, 'invalid grammar was silently accepted')
                bad.unlink()
                for generated in (work / 'obj').glob('invalid.*'):
                    generated.unlink()
    print('PASS: normal build preserves shipped parsers; missing outputs and errors remain active')
    print('PASS: explicit maintainer mode reproduces baseline source regeneration through recursive GNU make')


if __name__ == '__main__':
    main()
