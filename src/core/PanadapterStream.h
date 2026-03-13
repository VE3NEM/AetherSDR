#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QVector>

namespace AetherSDR {

class RadioConnection;

// Receives all VITA-49 UDP datagrams from the radio on the single "client udpport"
// and routes them by PacketClassCode (bytes 14-15 of the VITA-49 class ID):
//   • PCC 0x03E3 → narrow audio, float32 stereo big-endian → audioDataReady()
//   • PCC 0x0123 → narrow audio reduced-BW, int16 mono big-endian → audioDataReady()
//   • stream ID 0x40xxxxxx  → FFT bins → spectrumReady()
//   • everything else       → silently dropped
//
// All packets from the radio use ExtDataWithStream (VITA-49 type 3), not IFDataWithStream.
//
// Protocol:
//   1. Call start(conn) — binds port 4991 (LAN VITA port), falls back to OS-assigned.
//   2. Register the port with the radio via "client udpport <port>" (done by RadioModel).
//   3. The radio streams panadapter and audio to that port.

class PanadapterStream : public QObject {
    Q_OBJECT

public:
    static constexpr int VITA49_HEADER_BYTES = 28;

    explicit PanadapterStream(QObject* parent = nullptr);

    // Bind a local UDP port (OS-chosen) and register it with the radio.
    // conn must remain valid for the lifetime of this stream.
    bool start(RadioConnection* conn);
    void stop();

    quint16 localPort() const { return m_localPort; }
    bool    isRunning() const;

    // Update the dBm range used to scale incoming FFT bins.
    // Called whenever the radio reports min_dbm / max_dbm for the panadapter.
    void setDbmRange(float minDbm, float maxDbm);

signals:
    void spectrumReady(const QVector<float>& binsDbm);
    // Raw PCM payload (header stripped) from IF-Data (audio) VITA-49 packets.
    // Format: 16-bit signed, stereo, 24 kHz, little-endian.
    void audioDataReady(const QByteArray& pcm);

private slots:
    void onDatagramReady();

private:
    void processDatagram(const QByteArray& data);
    void decodeNarrowAudio(const uchar* raw, int totalBytes, bool hasTrailer);
    void decodeReducedBwAudio(const uchar* raw, int totalBytes, bool hasTrailer);

    // PacketClassCodes for audio streams (from FlexLib VitaFlex.cs)
    static constexpr quint16 PCC_IF_NARROW         = 0x03E3u; // float32 stereo, big-endian
    static constexpr quint16 PCC_IF_NARROW_REDUCED = 0x0123u; // int16 mono, big-endian

    // Frame assembly: a VITA-49 FFT frame may arrive in multiple UDP packets.
    // Each packet carries start_bin_index + num_bins so we can stitch them.
    struct FrameAssembler {
        quint32        frameIndex{0xFFFFFFFF};
        quint16        totalBins{0};
        quint16        binsReceived{0};
        QVector<quint16> buf;          // raw uint16 bins, host byte-order

        void reset(quint32 idx, quint16 total) {
            frameIndex   = idx;
            totalBins    = total;
            binsReceived = 0;
            buf.resize(total);
        }
        bool isComplete() const { return totalBins > 0 && binsReceived >= totalBins; }
    };

    QUdpSocket      m_socket;
    quint16         m_localPort{0};
    float           m_minDbm{-130.0f};
    float           m_maxDbm{-20.0f};
    RadioConnection* m_conn{nullptr};
    FrameAssembler  m_frame;
};

} // namespace AetherSDR
