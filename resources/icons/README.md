# Иконка приложения VK

- `VK.png` — исходник 1024×1024 с прозрачностью для Qt и повторной упаковки.
- `VK.icns` — иконка Finder/Dock, размеры 16, 32, 64, 128, 256, 512 и 1024 пикселя, включая варианты Retina.
- Пересборка ICNS на macOS: `sh scripts/build-app-icon.sh`.

Рисунок создан встроенным инструментом `image_gen`, затем уменьшен системной утилитой `sips` для комплекта размеров. ICNS собран `iconutil`. Внешний сервис и API-ключ для воспроизведения упаковки не нужны. PNG встроен в Qt resources, ICNS включён в `VK.app/Contents/Resources` и указан в `CFBundleIconFile`. На macOS иконку загружает сама система из ICNS; вызов `QGuiApplication::setWindowIcon` не используется из-за падения Qt/Cocoa на macOS 27. На других платформах Qt использует PNG.

## Промпт генерации

Create ONE finished macOS desktop application icon for the existing Russian VK Messenger app, named VK. This is a production icon asset, not a mockup. Square 1024x1024 canvas with real transparent alpha outside the icon. Center a macOS rounded-square app tile occupying approximately 82% of canvas width, aligned straight on, with generous equal transparent margins. Rich VK blue (#2688EB to #1262DC) with a very restrained, polished soft gradient and a subtle edge highlight, crisp clean silhouette, understated modern macOS finish. In the middle place a bold white highly legible connected geometric 'VK' monogram, visually centered and large enough to remain clear at 16 and 32 pixels. Integrate a small simple speech-bubble cue into the white mark's lower-right silhouette, without distracting from the V and K. Favor a simple strong silhouette and large clean negative spaces. No extra text, no captions, no device mockup, no surrounding scene, no grid, no multiple variants, no checkerboard drawn into the background, no photographic texture, no strong 3D perspective. Output the single isolated icon, high quality clean antialiased edges and transparent outer corners/margins, ready for an ICNS iconset.

## Промпт уточнения прозрачности

Edit this exact macOS app icon. ONLY clean the transparency around the blue rounded square. Remove absolutely ALL stray blue speckles, blue grain, detached blue pixels and glow/shadow outside the tile, especially below the tile and above it. The outside must be completely empty transparent alpha, including around rounded corners, with no scattered pixels. The rounded-square silhouette must be a smooth clean antialiased edge. Preserve the existing blue rounded-square tile, its current size and placement, its blue gradient and interior edge highlight, and preserve the white VK monogram and speech-bubble tip exactly. Do not redraw the lettering or change the composition. Do not add a cast shadow, backdrop, glow, frame or anything outside the single tile. Transparent outer canvas. A clean isolated production-ready icon, single asset, square image.
