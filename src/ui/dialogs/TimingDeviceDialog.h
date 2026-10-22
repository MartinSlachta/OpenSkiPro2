#pragma once

#include "timing/TimingDevice.h"

#include <QDialog>
#include <array>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

// Local configuration > Timing Device 1 / 2.
class TimingDeviceDialog : public QDialog {
    Q_OBJECT
public:
    TimingDeviceDialog(int slot, const TimingDeviceConfig& cfg, QWidget* parent = nullptr);
    TimingDeviceConfig config() const;

private:
    void updateEnabled();
    void testDevice();

    int m_slot;
    TimingDeviceConfig m_cfg;
    QComboBox* m_type;
    QRadioButton* m_serial;
    QRadioButton* m_tcp;
    QComboBox* m_port;
    QComboBox* m_baud;
    QCheckBox* m_flowAck;
    QLineEdit* m_host;
    QSpinBox* m_tcpPort;
    QSpinBox* m_start;
    QSpinBox* m_finish;
    std::array<QSpinBox*, 3> m_inter{};
    std::array<QCheckBox*, 4> m_keyboard{};
    std::array<QSpinBox*, 4> m_soundChannel{};
    std::array<QLineEdit*, 4> m_soundFile{};
    QGroupBox* m_simBox;
    QSpinBox* m_simInterval;
    QSpinBox* m_simRunTime;
    QSpinBox* m_simSpread;
};
