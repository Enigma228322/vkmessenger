import Foundation
import AVFoundation
import CoreVideo

let destination = URL(fileURLWithPath: CommandLine.arguments[1])
if FileManager.default.fileExists(atPath: destination.path) { try FileManager.default.removeItem(at: destination) }
let writer = try AVAssetWriter(outputURL: destination, fileType: .mp4)
let input = AVAssetWriterInput(mediaType: .video, outputSettings: [
    AVVideoCodecKey: AVVideoCodecType.h264,
    AVVideoWidthKey: 320, AVVideoHeightKey: 180
])
let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: [
    kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32ARGB,
    kCVPixelBufferWidthKey as String: 320, kCVPixelBufferHeightKey as String: 180
])
writer.add(input)
guard writer.startWriting() else { fatalError(String(describing: writer.error)) }
writer.startSession(atSourceTime: .zero)
for frame in 0..<60 {
    while !input.isReadyForMoreMediaData { Thread.sleep(forTimeInterval: 0.002) }
    var buffer: CVPixelBuffer?
    guard CVPixelBufferPoolCreatePixelBuffer(nil, adaptor.pixelBufferPool!, &buffer) == kCVReturnSuccess else { fatalError("Pixel buffer") }
    CVPixelBufferLockBaseAddress(buffer!, [])
    let bytes = CVPixelBufferGetBaseAddress(buffer!)!.assumingMemoryBound(to: UInt8.self)
    let stride = CVPixelBufferGetBytesPerRow(buffer!)
    for y in 0..<180 { for x in 0..<320 {
        let index = y * stride + x * 4
        bytes[index] = 255
        bytes[index + 1] = UInt8((x + frame * 4) % 256)
        bytes[index + 2] = UInt8((y + frame * 3) % 256)
        bytes[index + 3] = UInt8((x / 2 + y / 2 + frame * 2) % 256)
    }}
    CVPixelBufferUnlockBaseAddress(buffer!, [])
    guard adaptor.append(buffer!, withPresentationTime: CMTime(value: Int64(frame), timescale: 12)) else { fatalError(String(describing: writer.error)) }
}
input.markAsFinished()
let done = DispatchSemaphore(value: 0)
writer.finishWriting { done.signal() }
done.wait()
guard writer.status == .completed else { fatalError(String(describing: writer.error)) }
print("Created deterministic 5-second H.264 fixture")
