#pragma once

#include <QPushButton>

#include <functional>

class QLineEdit;
class QTimer;
class SocketClient;

// 「获取验证码」按钮：校验手机号 -> user_code_request -> 展示模拟验证码 -> 60 秒倒计时
class SmsCodeButton : public QPushButton
{
    Q_OBJECT
public:
    SmsCodeButton(SocketClient *client, QLineEdit *phoneEdit, QWidget *parent = nullptr);

    // 未连接时的兜底：返回 true 表示已发起连接（连上后自动继续获取），false 表示放弃
    void setEnsureConnected(std::function<bool()> fn);

private:
    void onClicked();
    void sendRequest();
    void startCountdown();

    SocketClient *m_client;
    QLineEdit *m_phoneEdit;
    QTimer *m_timer;
    std::function<bool()> m_ensureConnected;
    int m_remain = 0;
    bool m_waitingConnect = false;
};
