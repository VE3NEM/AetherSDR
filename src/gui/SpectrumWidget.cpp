#include "SpectrumWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <cmath>
#include <cstring>

namespace AetherSDR {

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void SpectrumWidget::setFrequencyRange(double centerMhz, double bandwidthMhz)
{
    m_centerMhz    = centerMhz;
    m_bandwidthMhz = bandwidthMhz;
    update();
}

void SpectrumWidget::setDbmRange(float minDbm, float maxDbm)
{
    m_wfMinDbm    = minDbm;
    m_wfMaxDbm    = maxDbm;
    m_refLevel    = maxDbm;
    m_dynamicRange = maxDbm - minDbm;
    update();
}

void SpectrumWidget::setSliceFrequency(double freqMhz)
{
    m_sliceFreqMhz = freqMhz;
    update();
}

void SpectrumWidget::updateSpectrum(const QVector<float>& binsDbm)
{
    if (m_smoothed.size() != binsDbm.size())
        m_smoothed = binsDbm;
    else {
        for (int i = 0; i < binsDbm.size(); ++i)
            m_smoothed[i] = SMOOTH_ALPHA * binsDbm[i] + (1.0f - SMOOTH_ALPHA) * m_smoothed[i];
    }
    m_bins = binsDbm;

    // Rebuild waterfall image if the widget has been sized
    if (!m_waterfall.isNull())
        pushWaterfallRow(binsDbm, m_waterfall.width());

    update();
}

void SpectrumWidget::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);

    const int wfHeight = static_cast<int>(height() * (1.0f - SPECTRUM_FRAC));
    if (wfHeight > 0 && width() > 0) {
        // Resize waterfall: allocate new image, copy old content scaled
        QImage newWf(width(), wfHeight, QImage::Format_RGB32);
        newWf.fill(Qt::black);
        if (!m_waterfall.isNull())
            newWf = m_waterfall.scaled(width(), wfHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        m_waterfall = newWf;
    }
}

// ─── Colour map ───────────────────────────────────────────────────────────────

QRgb SpectrumWidget::dbmToRgb(float dbm) const
{
    // Normalise to [0, 1]: 0 = weakest (cold), 1 = strongest (hot)
    const float t = qBound(0.0f, (dbm - m_wfMinDbm) / (m_wfMaxDbm - m_wfMinDbm), 1.0f);

    // Map through a blue → cyan → green → yellow → red heat map.
    // Use HSV: hue sweeps 240° (blue) → 0° (red) as t goes 0 → 1.
    // Value ramps up for the first 10% so very cold signals show as near-black.
    const float hue = (1.0f - t) * 240.0f;
    // Minimum 8% brightness so noise floor shows as dim blue rather than pure black
    const float val = qBound(0.08f, t * 5.0f + 0.08f, 1.0f);
    return QColor::fromHsvF(hue / 360.0f, 1.0f, val).rgba();
}

// ─── Waterfall update ─────────────────────────────────────────────────────────

void SpectrumWidget::pushWaterfallRow(const QVector<float>& bins, int destWidth)
{
    if (m_waterfall.isNull() || destWidth <= 0) return;

    const int h = m_waterfall.height();
    if (h <= 1) return;

    // Scroll existing content down by one row.
    uchar* bits = m_waterfall.bits();
    const qsizetype bpl = m_waterfall.bytesPerLine();
    std::memmove(bits + bpl, bits, static_cast<size_t>(bpl) * (h - 1));

    // Paint the new frame at row 0.
    auto* row = reinterpret_cast<QRgb*>(bits);
    for (int x = 0; x < destWidth; ++x) {
        const int binIdx = x * bins.size() / destWidth;
        const float dbm  = (binIdx < bins.size()) ? bins[binIdx] : m_wfMinDbm;
        row[x] = dbmToRgb(dbm);
    }
}

// ─── Paint ────────────────────────────────────────────────────────────────────

void SpectrumWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int specH = static_cast<int>(height() * SPECTRUM_FRAC);
    const int wfY   = specH;
    const int wfH   = height() - specH;

    const QRect specRect(0, 0, width(), specH);
    const QRect wfRect(0, wfY, width(), wfH);

    // Background
    p.fillRect(specRect, QColor(0x0a, 0x0a, 0x14));

    drawGrid(p, specRect);
    drawSpectrum(p, specRect);
    drawWaterfall(p, wfRect);
    drawSliceMarker(p, height());
}

void SpectrumWidget::drawGrid(QPainter& p, const QRect& r)
{
    const int w = r.width();
    const int h = r.height();

    p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1, Qt::DotLine));

    // Horizontal dB lines every 20 dB
    const int steps = static_cast<int>(m_dynamicRange / 20.0f);
    for (int i = 0; i <= steps; ++i) {
        const int y = r.top() + static_cast<int>(h * i / static_cast<float>(steps));
        p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1, Qt::DotLine));
        p.drawLine(0, y, w, y);

        const float dbm = m_refLevel - (m_dynamicRange * i / steps);
        p.setPen(QColor(0x40, 0x60, 0x70));
        p.drawText(2, y + 12, QString("%1 dBm").arg(static_cast<int>(dbm)));
    }

    // Vertical frequency lines every ~50 kHz
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double stepMhz  = 0.050;
    const double firstLine = std::ceil(startMhz / stepMhz) * stepMhz;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;

    p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1, Qt::DotLine));
    for (double f = firstLine; f <= endMhz; f += stepMhz) {
        const int x = static_cast<int>((f - startMhz) / m_bandwidthMhz * w);
        p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1, Qt::DotLine));
        p.drawLine(x, r.top(), x, r.bottom());

        p.setPen(QColor(0x40, 0x60, 0x70));
        const int khz = static_cast<int>(std::round((f - m_centerMhz) * 1000.0));
        p.drawText(x + 2, r.bottom() - 4, QString("%1k").arg(khz));
    }
}

void SpectrumWidget::drawSpectrum(QPainter& p, const QRect& r)
{
    if (m_smoothed.isEmpty()) {
        p.setPen(QColor(0x00, 0x60, 0x80));
        p.drawText(r, Qt::AlignCenter, "No panadapter data — waiting for radio stream");
        return;
    }

    const int w = r.width();
    const int h = r.height();
    const int n = m_smoothed.size();

    QPainterPath path;
    bool first = true;

    for (int i = 0; i < n; ++i) {
        const float dbm  = m_smoothed[i];
        const float norm = qBound(0.0f, (m_refLevel - dbm) / m_dynamicRange, 1.0f);
        const int   x    = r.left() + static_cast<int>(static_cast<float>(i) / n * w);
        const int   y    = r.top()  + qMin(static_cast<int>(norm * h), h - 1);

        if (first) { path.moveTo(x, y); first = false; }
        else        path.lineTo(x, y);
    }

    // Filled area
    path.lineTo(r.right(), r.bottom());
    path.lineTo(r.left(),  r.bottom());
    path.closeSubpath();

    QLinearGradient grad(0, r.top(), 0, r.bottom());
    grad.setColorAt(0.0, QColor(0x00, 0xe5, 0xff, 200));
    grad.setColorAt(1.0, QColor(0x00, 0x40, 0x60,  60));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillPath(path, grad);
    p.setPen(QPen(QColor(0x00, 0xe5, 0xff), 1.5));
    p.drawPath(path);
    p.setRenderHint(QPainter::Antialiasing, false);
}

void SpectrumWidget::drawWaterfall(QPainter& p, const QRect& r)
{
    if (m_waterfall.isNull()) {
        p.fillRect(r, Qt::black);
        return;
    }
    p.drawImage(r, m_waterfall);
}

void SpectrumWidget::drawSliceMarker(QPainter& p, int totalHeight)
{
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    if (m_sliceFreqMhz < startMhz ||
        m_sliceFreqMhz > m_centerMhz + m_bandwidthMhz / 2.0)
        return;

    const int x = static_cast<int>(
        (m_sliceFreqMhz - startMhz) / m_bandwidthMhz * width());

    // Vertical line across the full widget (spectrum + waterfall)
    p.setPen(QPen(QColor(0xff, 0xa0, 0x00, 180), 1.5, Qt::DashLine));
    p.drawLine(x, 0, x, totalHeight);

    // Triangle at top
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xff, 0xa0, 0x00));
    QPolygon tri;
    tri << QPoint(x - 6, 0) << QPoint(x + 6, 0) << QPoint(x, 10);
    p.drawPolygon(tri);
}

} // namespace AetherSDR
