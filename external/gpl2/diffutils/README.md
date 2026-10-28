# OS diff and diff3

The OS builds `diff` and `diff3` from the prepared common Ports diffutils
source. The existing BSD `cmp` and `sdiff` are separate OS utilities. The
upstream Info manual is installed as `diffutils.info`, with `diff.info` kept
as a compatibility link.

The previous OS diffutils configuration did not enable localization. The
current source uses its normal configure detection; the existing base libintl
does not satisfy its GNU gettext probe, so this build remains unlocalized.
