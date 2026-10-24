# Third-party code

## rlottie

Animated stickers use [TelegramMessenger/rlottie](https://github.com/TelegramMessenger/rlottie), revision `53c9679069eb6b6513ccc3c892ef4f4a760658da`.

The renderer is fetched as a SHA-256-verified source archive and linked statically. Its source is available in `build/_deps/rlottie-src`; the renderer can be rebuilt or replaced through `FETCHCONTENT_SOURCE_DIR_RLOTTIE`.

rlottie is primarily licensed under LGPL-2.1-or-later. Its FreeType, Pixman, stb and RapidJSON portions have their respective licenses, listed in upstream `COPYING` and `licenses/`. These notices are copied into the macOS bundle at `Contents/Resources/licenses/rlottie`.

The animation fixture in `tests/fixtures/sticker.json` is original test data for this project.

## Qt and FFmpeg

The macOS bundle includes dynamically linked Qt frameworks and plugins from the configured Qt SDK, including Qt SQL/SQLite and Qt Multimedia. The multimedia backend also deploys the SDK's FFmpeg libraries. The local Qt 6.11.1 SDK reports FFmpeg 7.1.3 at runtime.

Source and component information: [Qt](https://code.qt.io/), [Qt Multimedia](https://code.qt.io/cgit/qt/qtmultimedia.git/), [FFmpeg](https://ffmpeg.org/). These components retain their respective copyrights and licenses; they are not original VKM code.

## libopus

Voice messages use libopus 1.6.1 (BSD-style license), https://opus-codec.org/.
The library is built from the pinned official archive or explicitly supplied at configure time.
The copyright, redistribution conditions, disclaimer, and patent-license references are included in `licenses/opus/COPYING` and in the app bundle under `Contents/Resources/licenses/opus/COPYING`.
