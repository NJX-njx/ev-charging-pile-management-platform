#include "smscodebutton.h"

#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTimer>

#include "net/socketclient.h"

namespace {
constexpr int kCodeResendSec = 60;
}

SmsCodeButton::SmsCodeButton(SocketClient *client, QLineEdit *phoneEdit, QWidget *parent)
    : QPushButton(QStringLiteral("获取验证码"), parent)
    , m_client(client)
    , m_phoneEdit(phoneEdit)
    , m_timer(new QTimer(this))
{
    setProperty("class", QStringLiteral("small"));
    m_timer->setInterval(1000);
    connect(this, &QPushButton::clicked, this, &SmsCodeButton::onClicked);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        --m_remain;
        if (m_remain <= 0) {
            m_timer->stop();
            setText(QStringLiteral("获取验证码"));
            setEnabled(true);
            return;
        }
        setText(QStringLiteral("%1 秒后重发").arg(m_remain));
    });
    connect(m_client, &SocketClient::connected, this, [this]() {
        if (m_waitingConnect) {
            m_waitingConnect = false;
            setText(QStringLiteral("获取验证码"));
            sendRequest();
        }
    });
    connect(m_client, &SocketClient::connectFailed, this, [this](const QString &reason) {
        if (!m_waitingConnect)
            return;
        m_waitingConnect = false;
        setText(QStringLiteral("获取验证码"));
        setEnabled(true);
        QMessageBox::warning(this, QStringLiteral("获取验证码"),
                             QStringLiteral("无法连接服务器：%1").arg(reason));
    });
}

void SmsCodeButton::setEnsureConnected(std::function<bool()> fn)
{
    m_ensureConnected = std::move(fn);
}

void SmsCodeButton::onClicked()
{
    static const QRegularExpression phoneRe(QStringLiteral("^1\\d{10}$"));
    if (!phoneRe.match(m_phoneEdit->text().trimmed()).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("获取验证码"),
                             QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    if (!m_client->isConnected()) {
        if (!m_ensureConnected || !m_ensureConnected()) {
            QMessageBox::warning(this, QStringLiteral("获取验证码"),
                                 QStringLiteral("未连接到服务器，请先确认服务器地址"));
            return;
        }
        m_waitingConnect = true;
        setText(QStringLiteral("连接中…"));
        setEnabled(false);
        return;
    }
    sendRequest();
}

void SmsCodeButton::sendRequest()
{
    const QString phone = m_phoneEdit->text().trimmed();
    setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_code_request"),
                          QJsonObject{{QStringLiteral("phone"), phone}},
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              if (code != 0) {
                                  setEnabled(true);
                                  QString text;
                                  if (code == 1005)
                                      text = QStringLiteral("该账号已注销，无法获取验证码");
                                  else if (code == 2001)
                                      text = QStringLiteral("手机号格式不正确");
                                  else
                                      text = msg.isEmpty() ? QStringLiteral("获取验证码失败，请稍后重试") : msg;
                                  QMessageBox::warning(this, QStringLiteral("获取验证码"), text);
                                  return;
                              }
                              const QString smsCode = data.value(QStringLiteral("code")).toString();
                              const int validSec = data.value(QStringLiteral("validSec")).toInt(300);
                              QMessageBox::information(this, QStringLiteral("获取验证码"),
                                                       QStringLiteral("模拟短信验证码：%1，%2 分钟内有效")
                                                           .arg(smsCode)
                                                           .arg(validSec / 60));
                              startCountdown();
                          });
}

void SmsCodeButton::startCountdown()
{
    m_remain = kCodeResendSec;
    setText(QStringLiteral("%1 秒后重发").arg(m_remain));
    setEnabled(false);
    m_timer->start();
}
