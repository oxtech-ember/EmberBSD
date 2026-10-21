#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Extract complete native consumers, including their original license.
import pathlib
import sys
src = pathlib.Path(sys.argv[1]).read_text()
notice = src[:src.index('#include')]
out = [notice]
if 'm_copy_rcvif(' in src:
    mbuf = (pathlib.Path(__file__).resolve().parents[2] / 'sys/sys/mbuf.h').read_text()
    start = mbuf.index('\nm_copy_rcvif(') + 1
    end = mbuf.index('\n}', start) + 2
    out += [mbuf[:mbuf.index('#ifndef')], 'static inline void\n' + mbuf[start:end]]
for name, declaration in [('ieee80211_amsdu_read_mbuf', 'static int'),
                          ('ieee80211_decap', 'static struct mbuf *'),
                          ('ieee80211_input_amsdu', 'static struct mbuf *'),
                          ('ieee80211_accept_data', 'static int'),
                          ('ieee80211_input_data', 'static int')]:
    marker = '\n' + name + '('
    if marker not in src:
        continue
    start = src.index(marker) + 1
    end = src.index('\n}', start) + 2
    body = declaration + '\n' + src[start:end]
    if name == 'ieee80211_decap':
        # The unmodified legacy llc pointer is reset to NULL later.
        body = body.replace('struct llc *llc;', '// cppcheck-suppress constVariablePointer\n\tstruct llc *llc;')
    if name == 'ieee80211_decap':
        # Existing legacy function compares int m_len with sizeof(struct llc).
        body = '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wsign-compare\"\n' + body + '\n#pragma GCC diagnostic pop'
    if name == 'ieee80211_input_data':
        call = '\t\tm = ieee80211_defrag(ni, m, hdrspace);\n'
        check = '\t\tif (m == NULL) {\n'
        # The identity-return path triggers GCC's assumed-nonnull heuristic.
        # The consuming-NULL path requires this result check. Annotate one line.
        annotation = '''/* GCC 16.2 tracks the pass-through pointer as assumed nonnull.
 * The other defrag outcome consumes the mbuf and returns NULL.
 * See gcc/analyzer/sm-malloc.cc:2072-2096 and 2158-2203:
 * https://github.com/gcc-mirror/gcc/blob/releases/gcc-16.2.0/gcc/analyzer/sm-malloc.cc
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-deref-before-check"
#endif
'''
        restore = '''#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
'''
        if body.count(call + check) != 1:
            raise SystemExit('expected one post-defrag NULL result check')
        body = body.replace(call + check, call + annotation + check + restore, 1)
    out += [body]
pathlib.Path(sys.argv[2]).write_text('\n'.join(out) + '\n')
