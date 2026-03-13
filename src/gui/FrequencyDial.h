#pragma once

#include <QWidget>
#include <QString>

namespace AetherSDR {

// A custom widget that displays and edits a radio frequency.
//
// Visually renders seven digit columns (in MHz, e.g. 014.225.000).
// The user can:
//   - Click a column then scroll to increment/decrement that digit's place value.
//   - Click and drag up/down on a column.
//   - Double-click to enter direct keyboard input.
//
// Emits frequencyChanged(double mhz) when the value changes.
class FrequencyDial : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double frequency READ frequency WRITE setFrequency NOTIFY frequencyChanged)

public:
    explicit FrequencyDial(QWidget* parent = nullptr);

    double frequency() const { return m_frequency; }
    void   setFrequency(double mhz);

    // Step size used when scrolling with no digit column active (Hz).
    void setStepSize(int hz) { m_stepHz = hz; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void frequencyChanged(double mhz);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    static constexpr int NUM_DIGITS    = 9;   // 3 digits before dot, 6 after (GHz→Hz)
    static constexpr int DIGIT_WIDTH   = 28;
    static constexpr int DIGIT_HEIGHT  = 56;
    static constexpr int DOT_WIDTH     = 12;
    static constexpr double MAX_FREQ   = 54.0;   // MHz (FLEX-6600 range)
    static constexpr double MIN_FREQ   = 0.001;  // MHz

    // Column layout: digits 0..2 = MHz hundreds/tens/units,
    //               digit 3 = separator,
    //               digits 4..6 = kHz, digits 7..9 = Hz
    // Internally we store all as integer Hz to avoid float rounding.
    long long frequencyHz() const;
    void      setFrequencyHz(long long hz);

    int  columnAtX(int x) const;           // which digit column is under x?
    double placeValueMhz(int col) const;   // place value of that column in MHz

    void startEditing();
    void commitEditing();

    double  m_frequency{14.225};  // MHz
    int     m_activeColumn{-1};   // column highlighted by mouse
    int     m_stepHz{100};        // step size for wheel with no active column
    bool    m_editing{false};
    QString m_editBuffer;
};

} // namespace AetherSDR
