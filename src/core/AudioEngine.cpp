#include "AudioEngine.h"

#include <QMediaDevices>
#include <QAudioDevice>
#include <QDebug>
#include <cmath>

namespace AetherSDR {

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{}

AudioEngine::~AudioEngine()
{
    stopRxStream();
    stopTxStream();
}

QAudioFormat AudioEngine::makeFormat() const
{
    QAudioFormat fmt;
    fmt.setSampleRate(DEFAULT_SAMPLE_RATE);
    fmt.setChannelCount(2);                        // stereo
    fmt.setSampleFormat(QAudioFormat::Int16);
    return fmt;
}

// ─── RX stream ───────────────────────────────────────────────────────────────

bool AudioEngine::startRxStream()
{
    if (m_audioSink) return true;   // already running

    const QAudioFormat fmt = makeFormat();
    const QAudioDevice defaultOutput = QMediaDevices::defaultAudioOutput();

    if (!defaultOutput.isFormatSupported(fmt))
        qWarning() << "AudioEngine: default output does not support 24kHz stereo Int16";

    m_audioSink   = new QAudioSink(defaultOutput, fmt, this);
    m_audioSink->setVolume(m_rxVolume);
    m_audioDevice = m_audioSink->start();   // push-mode

    if (!m_audioDevice) {
        qWarning() << "AudioEngine: failed to open audio sink";
        delete m_audioSink;
        m_audioSink = nullptr;
        return false;
    }

    qDebug() << "AudioEngine: RX stream started";
    emit rxStarted();
    return true;
}

void AudioEngine::stopRxStream()
{
    if (m_audioSink) {
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink   = nullptr;
        m_audioDevice = nullptr;
    }
    emit rxStopped();
}

void AudioEngine::setRxVolume(float v)
{
    m_rxVolume = qBound(0.0f, v, 1.0f);
    if (m_audioSink)
        m_audioSink->setVolume(m_rxVolume);
}

void AudioEngine::setMuted(bool muted)
{
    m_muted = muted;
    if (m_audioSink)
        m_audioSink->setVolume(muted ? 0.0f : m_rxVolume);
}

void AudioEngine::feedAudioData(const QByteArray& pcm)
{
    if (m_audioDevice && m_audioDevice->isOpen())
        m_audioDevice->write(pcm);

    emit levelChanged(computeRMS(pcm));
}

float AudioEngine::computeRMS(const QByteArray& pcm) const
{
    const int samples = pcm.size() / 2;  // 16-bit samples
    if (samples == 0) return 0.0f;

    const int16_t* data = reinterpret_cast<const int16_t*>(pcm.constData());
    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double s = data[i] / 32768.0;
        sum += s * s;
    }
    return static_cast<float>(std::sqrt(sum / samples));
}

// ─── TX stream ────────────────────────────────────────────────────────────────

bool AudioEngine::startTxStream(const QHostAddress& radioAddress, quint16 radioPort)
{
    m_txAddress = radioAddress;
    m_txPort    = radioPort;

    const QAudioFormat fmt = makeFormat();
    const QAudioDevice defaultInput = QMediaDevices::defaultAudioInput();

    m_audioSource = new QAudioSource(defaultInput, fmt, this);
    m_micDevice   = m_audioSource->start();

    if (!m_micDevice) {
        qWarning() << "AudioEngine: failed to open audio source";
        delete m_audioSource;
        m_audioSource = nullptr;
        return false;
    }

    connect(m_micDevice, &QIODevice::readyRead,
            this, &AudioEngine::onTxAudioReady);

    qDebug() << "AudioEngine: TX stream started ->" << radioAddress << ":" << radioPort;
    return true;
}

void AudioEngine::stopTxStream()
{
    if (m_audioSource) {
        m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
        m_micDevice   = nullptr;
    }
    m_txSocket.close();
}

void AudioEngine::onTxAudioReady()
{
    // TX stub — full VITA-49 framing needed before this is usable.
}

} // namespace AetherSDR
