# Media fixtures

`viewer-video.mp4` is an original, generated test pattern: 320×180, H.264, 12 fps,
5 seconds, no audio and no user data. `generate_video.swift` recreates it using
macOS AVFoundation:

```sh
swift tests/fixtures/generate_video.swift tests/fixtures/viewer-video.mp4
```

The generator replaces the output file. Tests use the checked-in clip directly;
they do not need a video encoder or a network connection.
