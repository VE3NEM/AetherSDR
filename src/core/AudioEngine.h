#pragma once

#include <QObject>
#include <QAudioSink>
#include <QAudioSource>
#include <QAudioFormat>
#include <QIODevice>
#include <QUdpSocket>
#include <QByteArray>

namespace AetherSDR {

// AudioEngine handles audio playback (RX) and capture (TX).
//
// RX path:
//   Audio PCM arrives via PanadapterStream::audioDataReady() — the radio sends
//   VITA-49 IF-Data packets to the single "client udpport" socket owned by
//   PanadapterStream. PanadapterStream strips the header and emits the raw PCM;
//   connect that signal to feedAudioData() then call startRxStream() to open
//   the QAudioSink.
//
// TX path (stub — not yet implemented):
//   Captures mic audio and sends it to the radio via UDP.

class AudioEngine : public QObject {
    Q_OBJECT

public:
    static constexpr int DEFAULT_SAMPLE_RATE = 24000;

    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    // Open the QAudioSink. Must be called once when connected.
    bool startRxStream();
    void stopRxStream();

    // TX (microphone) – send raw PCM to radio's audio stream endpoint
    bool startTxStream(const QHostAddress& radioAddress, quint16 radioPort);
    void stopTxStream();

    float rxVolume() const  { return m_rxVolume; }
    void  setRxVolume(float v);

    bool isMuted() const   { return m_muted; }
    void setMuted(bool m);

public slots:
    // Receives stripped PCM from PanadapterStream::audioDataReady().
    void feedAudioData(const QByteArray& pcm);

signals:
    void rxStarted();
    void rxStopped();
    void levelChanged(float rms);  // audio level for VU meter, 0.0–1.0

private slots:
    void onTxAudioReady();

private:
    QAudioFormat makeFormat() const;
    float computeRMS(const QByteArray& pcm) const;

    // RX
    QAudioSink*   m_audioSink{nullptr};
    QIODevice*    m_audioDevice{nullptr};   // raw device from QAudioSink

    // TX
    QUdpSocket    m_txSocket;
    QAudioSource* m_audioSource{nullptr};
    QIODevice*    m_micDevice{nullptr};
    QHostAddress  m_txAddress;
    quint16       m_txPort{0};

    float m_rxVolume{1.0f};
    bool  m_muted{false};
};

} // namespace AetherSDR
