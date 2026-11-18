# native/pkg: building PS4 packages on the PS4

`pkgbuild.h` and `keys.h` are a C++ port of parts of [LibOrbisPkg](https://github.com/maxton/LibOrbisPkg)
by Maxton and contributors (PkgBuilder, PfsBuilder, the PFSC wrapper, PlayGo, license entries, the
Mersenne Twister padding and the public fake/debug keys). LibOrbisPkg is licensed under the GNU LGPL
version 3; this port is too: see `COPYING.LESSER`. `crypto.h` (SHA-256, HMAC, AES-128 CBC/XTS, RSA) is
written for EmuForwarder from the published algorithms.

With the same files, date and time, `pkgbuild::build` writes the same bytes as PkgTool's `pkg_build`.
