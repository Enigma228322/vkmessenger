#pragma once
#include <QIODevice>
#include <array>
#include <limits>

namespace ReplyFileWriter {
inline constexpr qint64 NetworkBufferSize = 256 * 1024;

// Do not build a QByteArray proportional to bytesAvailable(): a delayed GUI
// callback can see an entire download at once. The same bound applies to the
// last bytes delivered only with finished(), and to replies without a length.
inline bool drain(QIODevice &source, QIODevice &destination,
                  qint64 remainingBudget = std::numeric_limits<qint64>::max())
{
    if (remainingBudget < 0 || source.bytesAvailable() > remainingBudget) return false;
    std::array<char, 64 * 1024> buffer;
    while (source.bytesAvailable() > 0) {
        const auto count = source.read(buffer.data(), qMin(qint64(buffer.size()), remainingBudget));
        if (count <= 0 || destination.write(buffer.data(), count) != count) return false;
        remainingBudget -= count;
        if (source.bytesAvailable() > remainingBudget) return false;
    }
    return true;
}
}
