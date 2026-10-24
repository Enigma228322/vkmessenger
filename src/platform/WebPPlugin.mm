#include <QImageIOPlugin>
#include <QImageIOHandler>
#include <QIODevice>
#include <QImage>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>

class NativeWebPHandler : public QImageIOHandler {
public:
    static bool detects(QIODevice *device) {
        if (!device) return false;
        const auto header = device->peek(12);
        return header.size() == 12 && header.startsWith("RIFF") && header.mid(8, 4) == "WEBP";
    }
    bool canRead() const override { return detects(device()); }
    bool read(QImage *output) override {
        if (!output || !canRead()) return false;
        const auto bytes = device()->readAll();
        auto data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(bytes.constData()), bytes.size());
        auto source = CGImageSourceCreateWithData(data, nullptr); CFRelease(data);
        if (!source) return false;
        // Inspect dimensions before allocating decoded pixels.
        auto properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
        int width = 0, height = 0;
        if (properties) {
            auto w = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth));
            auto h = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight));
            if (w) CFNumberGetValue(w, kCFNumberIntType, &width);
            if (h) CFNumberGetValue(h, kCFNumberIntType, &height);
            CFRelease(properties);
        }
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192 || qint64(width) * height > 32000000) { CFRelease(source); return false; }
        auto image = CGImageSourceCreateImageAtIndex(source, 0, nullptr); CFRelease(source);
        if (!image) return false;
        QImage result(width, height, QImage::Format_RGBA8888_Premultiplied); result.fill(Qt::transparent);
        auto space = CGColorSpaceCreateDeviceRGB();
        auto context = CGBitmapContextCreate(result.bits(), width, height, 8, result.bytesPerLine(), space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(space);
        if (!context) { CGImageRelease(image); return false; }
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
        CGContextRelease(context); CGImageRelease(image); *output = result; return true;
    }
};

class VkmWebPPlugin : public QImageIOPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "webp.json")
public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override {
        return format == "webp" || (format.isEmpty() && NativeWebPHandler::detects(device)) ? CanRead : Capabilities();
    }
    QImageIOHandler *create(QIODevice *device, const QByteArray &format = {}) const override {
        auto handler = new NativeWebPHandler; handler->setDevice(device); handler->setFormat(format); return handler;
    }
};
#include "WebPPlugin.moc"
