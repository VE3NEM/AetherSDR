#pragma once

#include <QWidget>
#include <QVector>

class QPushButton;

namespace AetherSDR {

// Floating overlay menu anchored to the top-left of the SpectrumWidget.
// Open by default; collapses to a single arrow button when closed.
// Buttons are placeholders — signals emitted for parent to wire.
class SpectrumOverlayMenu : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumOverlayMenu(QWidget* parent = nullptr);

signals:
    void addRxClicked();
    void addTnfClicked();
    void antClicked();
    void displayClicked();
    void daxClicked();
    // Emitted when user selects a band from the sub-panel.
    void bandSelected(double freqMhz, const QString& mode);

private:
    void toggle();
    void updateLayout();
    void toggleBandPanel();
    void buildBandPanel();

    QPushButton* m_toggleBtn{nullptr};
    QVector<QPushButton*> m_menuBtns;
    bool m_expanded{true};

    // Band sub-panel (shown to the right of the menu)
    QWidget* m_bandPanel{nullptr};
    bool m_bandPanelVisible{false};
};

} // namespace AetherSDR
