# zeroPL mpv patches

This branch starts at upstream `v0.41.0`, commit
`41f6a645068483470267271e1d09966ca3b9f413`. zeroPL applies these changes:

1. Detect hardware screenshot frames with `IMGFMT_IS_HWACCEL` before downloading
   them. This preserves hardware decoding during thumbnail capture.
2. Add the opt-in `poc-p010` software render target, declared in
   `include/mpv/zeropl_p010.h`. It produces limited-range 10-bit YUV and composites
   libass graphics in linear light for PQ and HLG, with explicit subtitle white.

Existing render targets retain their behavior. Applications must request the new
target explicitly. This extension is specific to this fork; upstream libmpv does
not implement it. Interface-compatible replacements used by zeroPL HDR playback
must preserve version 1 of this extension as well as the upstream client ABI.
FFmpeg, libass, and libplacebo require no source changes for these patches.

## Building and testing

The ordinary mpv Meson build compiles these sources and installs the extension
header. Install the dependencies described in `README.md`, then, for a minimal
shared LGPL library build:

```sh
meson setup build -Ddefault_library=shared -Dauto_features=disabled \
  -Dgpl=false -Dlibmpv=true -Dcplayer=false
meson compile -C build
meson install -C build
```

Platform output and audio options must be enabled for the intended application.
zeroPL's corresponding-source materials include its exact macOS/iOS cross-build
recipes, dependency pins, configuration, and SDK records. Builds consume a fixed
fork commit archive and checksum, never the moving branch tip.

The standalone compositor test uses synthetic pixels and requires no media:

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -I. -Iinclude \
  test/zeropl_hdr.c -lm -o /tmp/zeropl-hdr-test
/tmp/zeropl-hdr-test
```

## Licensing and source delivery

The renderer changes are LGPL-2.1-or-later. The new application interface header
is MIT, with its full notice in the file. Existing upstream notices and license
choices remain in force; see `Copyright` and `LICENSE.LGPL`. zeroPL builds exclude
GPL-only components and retain the LGPL dependency configuration.

The modified source files identify zeroPL's changes and their date. This archive
contains the complete patched mpv sources. Distributors must also retain the
matching dependency sources, build/install scripts, notices, and application
relinking materials required by their chosen LGPL distribution route. Publishing
this fork alone does not provide application relinking materials.
