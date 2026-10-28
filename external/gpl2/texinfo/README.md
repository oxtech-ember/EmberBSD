# Texinfo in EmberBSD

The base system's `info` and `install-info` use the common Ports Texinfo
source. The full converter is the optional `gtexinfo` Ports package; it
requires Perl and its packaged dependencies rather than adding Perl to base.
Target package and dependency availability depends on the published package
repository or a compatible local package bundle. For a local bundle, set
`PKG_PATH` to its directory and run `/usr/sbin/pkg_add` with the gtexinfo
package filename. This does not imply that a target bundle has been released.
The package installs its programs under `/usr/pkg/bin`, including `makeinfo`,
`texi2any`, `texi2dvi`, `texi2pdf`, and `texindex`.

GNU Info reads the text source of an infokey configuration directly. Keep
the old source as `~/.infokey` or in the XDG Texinfo configuration location;
the old `infokey` compiler and its compiled `.info` key file are no longer
used. A compiled file alone is not the original configuration source and is
not automatically converted. Preserve it while recovering its source.

The current source retains `epsf.tex`, `texinfo.tex`, and `texinfo.dtd` at
the OS's historical `/usr/share/misc` paths. The old `texinfo.cat` and
`texinfo.xsl` files are absent from current upstream Texinfo and are not
installed. The old `info.info` manual belonged to Emacs; current Info falls
back to `info-stnd.info` when it is absent. The historical converter binaries
and their base-set manual pages are obsolete; use the Ports package's tools
and documentation instead.
