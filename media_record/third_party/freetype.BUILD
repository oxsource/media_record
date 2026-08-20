# FreeType 2.13.2 — cc_library for Bazel.
# Source list mirrors upstream CMake (src/CMakeLists.txt BASE_SRCS + ftsystem
# + ftdebug). Optional deps rely on ftoption.h defaults being undefined:
#   - FT_CONFIG_OPTION_USE_PNG / _USE_BZIP2 / _USE_HARFBUZZ are off by default
#   - FT_CONFIG_OPTION_USE_ZLIB is ON by default and pulls FreeType's OWN
#     bundled zlib sources (src/gzip/{zutil,inffast,inflate,inftrees,adler32,
#     crc32}.c) via `#include` in ftgzip.c when FT_CONFIG_OPTION_SYSTEM_ZLIB is
#     NOT defined — so NO system zlib dependency is required.
# `FT2_BUILD_LIBRARY` is the sole required define (builds the library, not
# the freetype-config client). The `include/` dir is the public include root
# (`<ft2build.h>`, `<freetype/...>`).
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "freetype",
    srcs = [
        "src/autofit/autofit.c",
        "src/base/ftbase.c",
        "src/base/ftbbox.c",
        "src/base/ftbdf.c",
        "src/base/ftbitmap.c",
        "src/base/ftcid.c",
        "src/base/ftfstype.c",
        "src/base/ftgasp.c",
        "src/base/ftglyph.c",
        "src/base/ftgxval.c",
        "src/base/ftinit.c",
        "src/base/ftmm.c",
        "src/base/ftotval.c",
        "src/base/ftpatent.c",
        "src/base/ftpfr.c",
        "src/base/ftstroke.c",
        "src/base/ftsynth.c",
        "src/base/fttype1.c",
        "src/base/ftwinfnt.c",
        "src/cache/ftcache.c",
        "src/cff/cff.c",
        "src/cid/type1cid.c",
        "src/bdf/bdf.c",
        "src/gzip/ftgzip.c",
        "src/lzw/ftlzw.c",
        "src/pcf/pcf.c",
        "src/pfr/pfr.c",
        "src/psaux/psaux.c",
        "src/pshinter/pshinter.c",
        "src/psnames/psnames.c",
        "src/raster/raster.c",
        "src/sdf/sdf.c",
        "src/sfnt/sfnt.c",
        "src/smooth/smooth.c",
        "src/truetype/truetype.c",
        "src/type1/type1.c",
        "src/type42/type42.c",
        "src/winfonts/winfnt.c",
        "src/base/ftsystem.c",
        "src/base/ftdebug.c",
    ],
    hdrs = glob([
        "include/ft2build.h",
        "include/freetype/**/*.h",
        # Private/internal headers AND amalgamated sub-sources under src/** are
        # #include-ed by the master objects above (autofit.c includes af*.c,
        # sfnt.c includes sfdriver.c, ftbase.c includes ftadvanc.c, ...).
        # Declared as hdrs so Bazel tracks them as rule inputs WITHOUT compiling
        # them as separate objects (would otherwise duplicate symbols).
        "src/**/*.h",
        "src/*/*.c",
    ]),
    strip_include_prefix = "",
    includes = ["include"],
    defines = ["FT2_BUILD_LIBRARY"],
)