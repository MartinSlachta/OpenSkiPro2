#pragma once

#include <QDialog>

class ApiServer;
class QCheckBox;
class QLabel;
class QSpinBox;

// Local configuration > API Server: the read-only REST / WebSocket API (docs/api.md).
class ApiSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ApiSettingsDialog(ApiServer* api, QWidget* parent = nullptr);
    void accept() override;

private:
    void updateInfo();

    ApiServer* m_api;
    QCheckBox* m_enabled;
    QSpinBox* m_port;
    QLabel* m_state;
    QLabel* m_urls;
};
