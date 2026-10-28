# GNU grep source integration

The GNU base profile builds the prepared common Ports `textproc/grep` source
selected by `EMBER_GREP_SOURCE`. The shared source is GNU grep 3.12, licensed
under GPL version 3 or later; its `COPYING` and notices remain with that source.
The historical `dist` and its GPL version 2 or later notices remain temporarily
for provenance and are not inputs to this producer.

The base profile retains the historical lack of Perl regular expressions.
The `egrep` and `fgrep` entry points select `-E` and `-F` on `/usr/bin/grep`;
gzip's zgrep entry points remain owned by gzip. `MKBSDGREP=yes` instead selects
the independent BSD implementation. For NLS the GNU producer checks and uses
the base libintl public API. The upstream Bokmål `nb` catalog is also installed
under the historical `no` name; the older `no` translation identified itself
as Bokmål. Target execution and translation checks are separate from building.
