#include "PanadapterStream.h"
#include "RadioConnection.h"

#include <QNetworkDatagram>
#include <QHostAddress>
#include <QtEndian>
#include <QSet>
#include <QDebug>
#include <cstring>

namespace AetherSDR {

// ─── VITA-49 header layout (28 bytes, big-endian) ─────────────────────────────
// Word 0 (bytes  0- 3): Packet header (type=3 ExtData, flags, count, size)
// Word 1 (bytes  4- 7): Stream ID
// Word 2 (bytes  8-11): Class ID OUI
// Word 3 (bytes 12-15): Class ID — InformationClassCode[15:0] | PacketClassCode[15:0]
// Word 4 (bytes 16-19): Integer timestamp
// Word 5 (bytes 20-23): Fractional timestamp (upper)
// Word 6 (bytes 24-27): Fractional timestamp (lower)
// Byte 28+            : Payload
//
// All FLEX radio streams use ExtDataWithStream (type 3), including audio.
// Audio is identified by PacketClassCode (lower 16 bits of word 3):
//   0x03E3 — SL_VITA_IF_NARROW_CLASS        — float32 stereo, big-endian
//   0x0123 — SL_VITA_IF_NARROW_REDUCED_BW   — int16 mono, big-endian
//   0x8005 — SL_VITA_OPUS_CLASS             — Opus compressed (not yet handled)
//
// Panadapter FFT: stream ID 0x40xxxxxx, PCC = SL_VITA_FFT_CLASS (0x8003)

PanadapterStream::PanadapterStream(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QUdpSocket::readyRead,
            this, &PanadapterStream::onDatagramReady);
}

bool PanadapterStream::isRunning() const
{
    return m_socket.state() == QAbstractSocket::BoundState;
}

bool PanadapterStream::start(RadioConnection* conn)
{
    if (isRunning()) return true;

    static constexpr quint16 LAN_VITA_PORT = 4991;
    bool bound = m_socket.bind(QHostAddress::AnyIPv4, LAN_VITA_PORT,
                               QAbstractSocket::ReuseAddressHint);
    if (bound)
        qDebug() << "PanadapterStream: bound to LAN VITA-49 port" << LAN_VITA_PORT;
    else {
        qDebug() << "PanadapterStream: port" << LAN_VITA_PORT
                 << "unavailable, using OS-assigned port";
        bound = m_socket.bind(QHostAddress::AnyIPv4, 0);
    }
    if (!bound) {
        qWarning() << "PanadapterStream: failed to bind UDP socket:"
                   << m_socket.errorString();
        return false;
    }

    m_localPort = m_socket.localPort();
    qDebug() << "PanadapterStream: bound to UDP port" << m_localPort;

    m_conn = conn;
    return true;
}

void PanadapterStream::stop()
{
    m_socket.close();
    m_localPort = 0;
}

// ─── Datagram reception ───────────────────────────────────────────────────────

void PanadapterStream::setDbmRange(float minDbm, float maxDbm)
{
    m_minDbm = minDbm;
    m_maxDbm = maxDbm;
    qDebug() << "PanadapterStream: dBm range set to" << minDbm << "->" << maxDbm;
}

void PanadapterStream::onDatagramReady()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_socket.receiveDatagram();
        if (!dg.isNull())
            processDatagram(dg.data());
    }
}

void PanadapterStream::processDatagram(const QByteArray& data)
{
    if (data.size() < VITA49_HEADER_BYTES) return;

    const auto* raw = reinterpret_cast<const uchar*>(data.constData());

    const quint32 word0    = qFromBigEndian<quint32>(raw);
    const quint32 streamId = qFromBigEndian<quint32>(raw + 4);
    const bool    hasTrailer = (word0 & 0x04000000u) != 0;

    // PacketClassCode is in the lower 16 bits of word 3 (bytes 12-15).
    const quint16 pcc = static_cast<quint16>(qFromBigEndian<quint32>(raw + 12) & 0xFFFFu);

    // Log the first occurrence of each unique stream ID.
    static QSet<quint32> seenIds;
    if (!seenIds.contains(streamId)) {
        seenIds.insert(streamId);
        qDebug() << "PanadapterStream: new stream" << data.size()
                 << "bytes, word0=0x" + QString::number(word0, 16)
                 << "streamId=0x" + QString::number(streamId, 16)
                 << "pcc=0x" + QString::number(pcc, 16)
                 << "trailer=" << hasTrailer;
    }

    // Remote audio — float32 stereo, big-endian (SL_VITA_IF_NARROW_CLASS)
    if (pcc == PCC_IF_NARROW) {
        decodeNarrowAudio(raw, data.size(), hasTrailer);
        return;
    }

    // DAX audio reduced-BW — int16 mono, big-endian (SL_VITA_IF_NARROW_REDUCED_BW_CLASS)
    if (pcc == PCC_IF_NARROW_REDUCED) {
        decodeReducedBwAudio(raw, data.size(), hasTrailer);
        return;
    }

    // Only process panadapter FFT streams (0x40xxxxxx).
    if ((streamId & 0xFF000000u) != 0x40000000u) return;

    // ── FFT sub-header (bytes 28–39) ──────────────────────────────────────────
    // From VitaFFTPacket.cs (FlexLib reference):
    //   uint16 start_bin_index
    //   uint16 num_bins
    //   uint16 bin_size          (bytes per bin, always 2)
    //   uint16 total_bins_in_frame
    //   uint32 frame_index
    static constexpr int FFT_SUBHEADER_BYTES = 12;
    if (data.size() < VITA49_HEADER_BYTES + FFT_SUBHEADER_BYTES) return;

    const uchar* sub = raw + VITA49_HEADER_BYTES;
    const quint16 startBin   = qFromBigEndian<quint16>(sub + 0);
    const quint16 numBins    = qFromBigEndian<quint16>(sub + 2);
    const quint16 binSize    = qFromBigEndian<quint16>(sub + 4);
    const quint16 totalBins  = qFromBigEndian<quint16>(sub + 6);
    const quint32 frameIndex = qFromBigEndian<quint32>(sub + 8);

    if (numBins == 0 || binSize == 0 || totalBins == 0) return;

    const int binDataOffset = VITA49_HEADER_BYTES + FFT_SUBHEADER_BYTES;
    int binDataBytes = numBins * binSize;
    const int available = data.size() - binDataOffset - (hasTrailer ? 4 : 0);
    if (available < binDataBytes) {
        binDataBytes = available;
        if (binDataBytes <= 0) return;
    }

    const uchar* binData = raw + binDataOffset;

    if (frameIndex != m_frame.frameIndex)
        m_frame.reset(frameIndex, totalBins);

    if (startBin + numBins > static_cast<quint16>(m_frame.buf.size()))
        return;

    for (quint16 i = 0; i < numBins; ++i)
        m_frame.buf[startBin + i] = qFromBigEndian<quint16>(binData + i * 2);

    m_frame.binsReceived += numBins;

    if (!m_frame.isComplete()) return;

    // ── Convert to dBm and emit ───────────────────────────────────────────────
    const float range = m_maxDbm - m_minDbm;
    const int   count = m_frame.buf.size();
    QVector<float> bins(count);
    for (int i = 0; i < count; ++i)
        bins[i] = m_minDbm + (static_cast<float>(m_frame.buf[i]) / 65535.0f) * range;

    emit spectrumReady(bins);
}

// ─── Audio decode ─────────────────────────────────────────────────────────────

void PanadapterStream::decodeNarrowAudio(const uchar* raw, int totalBytes, bool hasTrailer)
{
    // Payload: big-endian float32 stereo interleaved (L, R, L, R, ...).
    // Convert to little-endian int16 stereo for QAudioSink (Int16, 24 kHz).
    const int payloadStart = VITA49_HEADER_BYTES;
    const int payloadBytes = totalBytes - payloadStart - (hasTrailer ? 4 : 0);
    if (payloadBytes < 4) return;

    const int numFloats = payloadBytes / 4;
    const uchar* src = raw + payloadStart;

    QByteArray pcm(numFloats * 2, Qt::Uninitialized);
    auto* dst = reinterpret_cast<qint16*>(pcm.data());

    for (int i = 0; i < numFloats; ++i) {
        // Read big-endian uint32, reinterpret as float (byte-swap = big→native float).
        const quint32 u = qFromBigEndian<quint32>(src + i * 4);
        float f;
        std::memcpy(&f, &u, 4);
        dst[i] = static_cast<qint16>(qBound(-1.0f, f, 1.0f) * 32767.0f);
    }

    emit audioDataReady(pcm);
}

void PanadapterStream::decodeReducedBwAudio(const uchar* raw, int totalBytes, bool hasTrailer)
{
    // Payload: big-endian int16 mono. Duplicate to stereo for QAudioSink.
    const int payloadStart = VITA49_HEADER_BYTES;
    const int payloadBytes = totalBytes - payloadStart - (hasTrailer ? 4 : 0);
    if (payloadBytes < 2) return;

    const int monoSamples = payloadBytes / 2;
    const uchar* src = raw + payloadStart;

    QByteArray pcm(monoSamples * 4, Qt::Uninitialized);  // stereo int16
    auto* dst = reinterpret_cast<qint16*>(pcm.data());

    for (int i = 0; i < monoSamples; ++i) {
        const qint16 s = qFromBigEndian<qint16>(src + i * 2);
        dst[i * 2]     = s;  // L
        dst[i * 2 + 1] = s;  // R
    }

    emit audioDataReady(pcm);
}

} // namespace AetherSDR
