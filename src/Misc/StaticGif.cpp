#include "Misc/StaticGif.h"

#include <algorithm>
#include <array>
#include <climits>

#include <QHash>
#include <QImage>
#include <QVector>

namespace {

struct ColorCount {
    QRgb rgb;
    quint64 count;
};

int Channel(QRgb color, int channel)
{
    return channel == 0 ? qRed(color) : channel == 1 ? qGreen(color) : qBlue(color);
}

QVector<QRgb> MedianCut(QVector<ColorCount> colors, int limit)
{
    struct Box {
        int begin;
        int end;
    };
    QVector<Box> boxes { {0, int(colors.size())} };
    const auto widest = [&](const Box &box, int *range) {
        std::array<int, 3> low {255, 255, 255};
        std::array<int, 3> high {0, 0, 0};
        for (int index = box.begin; index < box.end; ++index) {
            for (int channel = 0; channel < 3; ++channel) {
                const int value = Channel(colors.at(index).rgb, channel);
                low[channel] = std::min(low[channel], value);
                high[channel] = std::max(high[channel], value);
            }
        }
        int best = 0;
        for (int channel = 1; channel < 3; ++channel)
            if (high[channel] - low[channel] > high[best] - low[best]) best = channel;
        *range = high[best] - low[best];
        return best;
    };
    while (boxes.size() < limit) {
        int chosen = -1;
        int chosenRange = 0;
        int chosenChannel = 0;
        quint64 chosenCount = 0;
        for (int index = 0; index < boxes.size(); ++index) {
            const Box &box = boxes.at(index);
            if (box.end - box.begin < 2) continue;
            int range = 0;
            const int channel = widest(box, &range);
            quint64 count = 0;
            for (int item = box.begin; item < box.end; ++item) count += colors.at(item).count;
            if (range > chosenRange || (range == chosenRange && count > chosenCount)) {
                chosen = index;
                chosenRange = range;
                chosenChannel = channel;
                chosenCount = count;
            }
        }
        if (chosen < 0) break;
        const Box box = boxes.at(chosen);
        std::sort(colors.begin() + box.begin, colors.begin() + box.end, [&](const ColorCount &left, const ColorCount &right) {
            return Channel(left.rgb, chosenChannel) < Channel(right.rgb, chosenChannel);
        });
        quint64 running = 0;
        int split = box.begin + 1;
        for (int item = box.begin; item < box.end - 1; ++item) {
            running += colors.at(item).count;
            split = item + 1;
            if (running * 2 >= chosenCount) break;
        }
        boxes[chosen] = {box.begin, split};
        boxes.append({split, box.end});
    }
    QVector<QRgb> palette;
    for (const Box &box : boxes) {
        std::array<quint64, 3> sums {0, 0, 0};
        quint64 total = 0;
        for (int item = box.begin; item < box.end; ++item) {
            const ColorCount &color = colors.at(item);
            for (int channel = 0; channel < 3; ++channel) sums[channel] += quint64(Channel(color.rgb, channel)) * color.count;
            total += color.count;
        }
        palette.append(qRgb(int((sums[0] + total / 2) / total), int((sums[1] + total / 2) / total),
                            int((sums[2] + total / 2) / total)));
    }
    return palette;
}

int Nearest(const QVector<QRgb> &palette, int first, QRgb color)
{
    int best = first;
    int bestDistance = INT_MAX;
    for (int index = first; index < palette.size(); ++index) {
        const int red = qRed(palette.at(index)) - qRed(color);
        const int green = qGreen(palette.at(index)) - qGreen(color);
        const int blue = qBlue(palette.at(index)) - qBlue(color);
        const int distance = red * red + green * green + blue * blue;
        if (distance < bestDistance) {
            best = index;
            bestDistance = distance;
            if (distance == 0) break;
        }
    }
    return best;
}

class BitWriter {
public:
    explicit BitWriter(QByteArray *output) : m_output(output) {}

    void write(int code, int width)
    {
        m_bits |= quint32(code) << m_count;
        m_count += width;
        while (m_count >= 8) {
            push(char(m_bits & 0xFF));
            m_bits >>= 8;
            m_count -= 8;
        }
    }

    void finish()
    {
        if (m_count > 0) push(char(m_bits & 0xFF));
        flushBlock();
        m_output->append('\0');
    }

private:
    void push(char value)
    {
        m_block.append(value);
        if (m_block.size() == 255) flushBlock();
    }

    void flushBlock()
    {
        if (m_block.isEmpty()) return;
        m_output->append(char(m_block.size()));
        m_output->append(m_block);
        m_block.clear();
    }

    QByteArray *m_output;
    QByteArray m_block;
    quint32 m_bits = 0;
    int m_count = 0;
};

void AppendWord(QByteArray *output, int value)
{
    output->append(char(value & 0xFF));
    output->append(char((value >> 8) & 0xFF));
}

// GIF LZW with the dictionary reset used by gif.h: clear at 4095 codes, and
// end with a clear code before EOI so the decoder never has to grow the width.
void WriteLzw(QByteArray *output, const QVector<uchar> &indexes, int minimumCodeSize)
{
    output->append(char(minimumCodeSize));
    BitWriter writer(output);
    const int clear = 1 << minimumCodeSize;
    int width = minimumCodeSize + 1;
    int maximum = clear + 1;
    QHash<quint32, int> codes;
    writer.write(clear, width);
    int current = -1;
    for (const uchar value : indexes) {
        if (current < 0) {
            current = value;
            continue;
        }
        const quint32 key = (quint32(current) << 8) | value;
        const auto found = codes.constFind(key);
        if (found != codes.constEnd()) {
            current = found.value();
            continue;
        }
        writer.write(current, width);
        codes.insert(key, ++maximum);
        if (maximum >= 4095) {
            writer.write(clear, width);
            codes.clear();
            width = minimumCodeSize + 1;
            maximum = clear + 1;
        } else if (maximum >= (1 << width)) {
            ++width;
        }
        current = value;
    }
    if (current >= 0) writer.write(current, width);
    writer.write(clear, width);
    writer.write(clear + 1, minimumCodeSize + 1);
    writer.finish();
}

}

namespace StaticGif {

QByteArray Encode(const QImage &source)
{
    if (source.isNull() || source.width() > 0xFFFF || source.height() > 0xFFFF) return QByteArray();
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int width = image.width();
    const int height = image.height();

    bool transparent = false;
    QRgb transparentColor = 0;
    QHash<QRgb, quint64> counts;
    QVector<QRgb> order;
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) == 0) {
                if (!transparent) transparentColor = line[x] & RGB_MASK;
                transparent = true;
                continue;
            }
            const QRgb color = line[x] | ~RGB_MASK;
            auto found = counts.find(color);
            if (found == counts.end()) {
                counts.insert(color, 1);
                order.append(color);
            } else {
                ++found.value();
            }
        }
    }

    const int first = transparent ? 1 : 0;
    const int limit = 256 - first;
    QVector<QRgb> palette;
    if (transparent) palette.append(transparentColor);
    const bool exact = order.size() <= limit;
    if (exact) {
        palette += order;
    } else {
        QVector<ColorCount> colors;
        colors.reserve(order.size());
        for (const QRgb color : order) colors.append({color, counts.value(color)});
        palette += MedianCut(colors, limit);
    }
    if (palette.isEmpty()) palette.append(qRgb(0, 0, 0));

    QHash<QRgb, uchar> lookup;
    if (exact)
        for (int index = first; index < palette.size(); ++index) lookup.insert(palette.at(index), uchar(index));
    QVector<uchar> indexes;
    indexes.reserve(width * height);
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) == 0) {
                indexes.append(0);
                continue;
            }
            const QRgb color = line[x] | ~RGB_MASK;
            auto found = lookup.constFind(color);
            if (found == lookup.constEnd()) found = lookup.insert(color, uchar(Nearest(palette, first, color)));
            indexes.append(found.value());
        }
    }

    int tableBits = 1;
    while ((1 << tableBits) < palette.size()) ++tableBits;
    QByteArray output("GIF89a");
    AppendWord(&output, width);
    AppendWord(&output, height);
    output.append(char(0x80 | ((tableBits - 1) << 4) | (tableBits - 1)));
    output.append('\0');
    output.append('\0');
    for (int index = 0; index < (1 << tableBits); ++index) {
        const QRgb color = index < palette.size() ? palette.at(index) : 0;
        output.append(char(qRed(color)));
        output.append(char(qGreen(color)));
        output.append(char(qBlue(color)));
    }
    if (transparent) {
        output.append("\x21\xF9\x04\x01\x00\x00\x00\x00", 8);
    }
    output.append(char(0x2C));
    AppendWord(&output, 0);
    AppendWord(&output, 0);
    AppendWord(&output, width);
    AppendWord(&output, height);
    output.append('\0');
    WriteLzw(&output, indexes, std::max(2, tableBits));
    output.append(char(0x3B));
    return output;
}

}
