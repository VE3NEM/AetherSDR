#include "SpectrumOverlayMenu.h"

#include <QPushButton>
#include <QGridLayout>

namespace AetherSDR {

static constexpr int BTN_W = 60;
static constexpr int BTN_H = 22;

// Band button size (slightly smaller for the grid)
static constexpr int BAND_BTN_W = 48;
static constexpr int BAND_BTN_H = 26;

static QPushButton* makeMenuBtn(const QString& text, QWidget* parent)
{
    auto* btn = new QPushButton(text, parent);
    btn->setFixedSize(BTN_W, BTN_H);
    btn->setStyleSheet(
        "QPushButton { background: rgba(15, 15, 26, 200); "
        "border: 1px solid #304050; border-radius: 2px; "
        "color: #c8d8e8; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); "
        "border: 1px solid #0090e0; }");
    return btn;
}

// SSB center frequencies per band (ARRL band plan).
// Below 10 MHz → LSB convention, above → USB.
// 30m is CW/digital only; we tune to the CW segment center.
struct BandEntry {
    const char* label;
    double freqMhz;
    const char* mode;
};

static constexpr BandEntry BANDS[] = {
    {"160",   1.900, "LSB"},   // 160m: 1.800–2.000, phone (LSB)
    {"80",    3.800, "LSB"},   // 80m:  3.500–4.000, phone (LSB)
    {"60",    5.357, "USB"},   // 60m:  channelized, ch3 center (USB)
    {"40",    7.200, "LSB"},   // 40m:  7.000–7.300, phone (LSB)
    {"30",   10.125, "DIGU"},  // 30m:  10.100–10.150, CW/digital only
    {"20",   14.225, "USB"},   // 20m:  14.000–14.350, phone (USB)
    {"17",   18.130, "USB"},   // 17m:  18.068–18.168, phone (USB)
    {"15",   21.300, "USB"},   // 15m:  21.000–21.450, phone (USB)
    {"12",   24.950, "USB"},   // 12m:  24.890–24.990, phone (USB)
    {"10",   28.400, "USB"},   // 10m:  28.000–29.700, phone (USB)
    {"6",    50.150, "USB"},   // 6m:   50.000–54.000, SSB calling (USB)
    {"WWV",  10.000, "AM"},    // WWV time signal (AM broadcast)
    {"GEN",   0.500, "AM"},    // General coverage (AM)
    {"2200",  0.1375,"CW"},    // 2200m: 135.7–137.8 kHz (CW/digital only)
    {"630",   0.475, "CW"},    // 630m:  472–479 kHz (CW/digital only)
    {"XVTR",  0.0,   ""},      // Transverter — opens sub-menu (not yet implemented)
};

SpectrumOverlayMenu::SpectrumOverlayMenu(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);

    // Toggle button (arrow)
    m_toggleBtn = new QPushButton(this);
    m_toggleBtn->setFixedSize(BTN_W, BTN_H);
    m_toggleBtn->setStyleSheet(
        "QPushButton { background: rgba(15, 15, 26, 200); "
        "border: 1px solid #304050; border-radius: 2px; "
        "color: #c8d8e8; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); "
        "border: 1px solid #0090e0; }");
    connect(m_toggleBtn, &QPushButton::clicked, this, &SpectrumOverlayMenu::toggle);

    // Menu buttons — Band is handled specially
    struct BtnDef { QString text; void (SpectrumOverlayMenu::*sig)(); };
    const BtnDef defs[] = {
        {"+RX",      &SpectrumOverlayMenu::addRxClicked},
        {"+TNF",     &SpectrumOverlayMenu::addTnfClicked},
        {"Band",     nullptr},   // handled by toggleBandPanel()
        {"ANT",      &SpectrumOverlayMenu::antClicked},
        {"Display",  &SpectrumOverlayMenu::displayClicked},
        {"DAX",      &SpectrumOverlayMenu::daxClicked},
    };

    for (const auto& def : defs) {
        auto* btn = makeMenuBtn(def.text, this);
        if (def.sig)
            connect(btn, &QPushButton::clicked, this, def.sig);
        else
            connect(btn, &QPushButton::clicked, this, &SpectrumOverlayMenu::toggleBandPanel);
        m_menuBtns.append(btn);
    }

    buildBandPanel();
    updateLayout();
}

void SpectrumOverlayMenu::buildBandPanel()
{
    // The band panel is a child of our parent (the SpectrumWidget),
    // so it can be positioned independently to the right of this menu.
    m_bandPanel = new QWidget(parentWidget());
    m_bandPanel->setAttribute(Qt::WA_NoSystemBackground, true);
    m_bandPanel->setAutoFillBackground(false);
    m_bandPanel->hide();

    auto* grid = new QGridLayout(m_bandPanel);
    grid->setContentsMargins(2, 2, 2, 2);
    grid->setSpacing(2);

    const QString bandBtnStyle =
        "QPushButton { background: rgba(30, 40, 55, 220); "
        "border: 1px solid #304050; border-radius: 3px; "
        "color: #c8d8e8; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); "
        "border: 1px solid #0090e0; }";

    // Layout: 3 columns
    // Row 0: 160, 80, 60
    // Row 1: 40, 30, 20
    // Row 2: 17, 15, 12
    // Row 3: 10, 6, (empty)
    // Row 4: WWV, GEN, (empty)
    // Row 5: 2200, 630, XVTR
    constexpr int layout[][3] = {
        {0, 1, 2},      // 160, 80, 60
        {3, 4, 5},      // 40, 30, 20
        {6, 7, 8},      // 17, 15, 12
        {9, 10, -1},    // 10, 6
        {11, 12, -1},   // WWV, GEN
        {13, 14, 15},   // 2200, 630, XVTR
    };

    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 3; ++col) {
            int idx = layout[row][col];
            if (idx < 0) continue;

            auto* btn = new QPushButton(BANDS[idx].label, m_bandPanel);
            btn->setFixedSize(BAND_BTN_W, BAND_BTN_H);
            btn->setStyleSheet(bandBtnStyle);

            double freq = BANDS[idx].freqMhz;
            QString mode = QString::fromLatin1(BANDS[idx].mode);
            if (mode.isEmpty()) {
                // Placeholder (e.g. XVTR) — not yet implemented
                btn->setEnabled(false);
            } else {
                connect(btn, &QPushButton::clicked, this, [this, freq, mode]() {
                    m_bandPanelVisible = false;
                    m_bandPanel->hide();
                    emit bandSelected(freq, mode);
                });
            }

            grid->addWidget(btn, row, col);
        }
    }

    m_bandPanel->adjustSize();
}

void SpectrumOverlayMenu::toggleBandPanel()
{
    m_bandPanelVisible = !m_bandPanelVisible;
    if (m_bandPanelVisible) {
        // Position to the right of the menu
        m_bandPanel->move(x() + width(), y());
        m_bandPanel->raise();
        m_bandPanel->show();
    } else {
        m_bandPanel->hide();
    }
}

void SpectrumOverlayMenu::toggle()
{
    m_expanded = !m_expanded;
    // Close band panel when collapsing
    if (!m_expanded && m_bandPanelVisible) {
        m_bandPanelVisible = false;
        m_bandPanel->hide();
    }
    updateLayout();
}

void SpectrumOverlayMenu::updateLayout()
{
    constexpr int pad = 2;   // padding from top-left corner
    constexpr int gap = 2;   // gap between buttons

    m_toggleBtn->setText(m_expanded ? QStringLiteral("\u2190") : QStringLiteral("\u2192"));

    // Position toggle button
    m_toggleBtn->move(pad, pad);

    // Show/hide and position menu buttons
    int y = pad + BTN_H + gap;
    for (auto* btn : m_menuBtns) {
        btn->setVisible(m_expanded);
        if (m_expanded) {
            btn->move(pad, y);
            y += BTN_H + gap;
        }
    }

    // Resize this widget to fit its contents
    int totalH = m_expanded ? (pad + BTN_H + gap + m_menuBtns.size() * (BTN_H + gap))
                            : (pad + BTN_H + pad);
    setFixedSize(pad + BTN_W + pad, totalH);
}

} // namespace AetherSDR
