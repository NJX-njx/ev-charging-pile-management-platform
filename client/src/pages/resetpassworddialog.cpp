#include "resetpassworddialog.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "ui/smscodebutton.h"
#include "ui/uienums.h"

ResetPasswordDialog::ResetPasswordDialog(SocketClient *client, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(QStringLiteral("重置密码"));
    setModal(true);
    setMinimumWidth(300);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("忘记密码"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);
    auto *hint = new QLabel(QStringLiteral("通过短信验证码重置登录密码"), this);
    hint->setObjectName(QStringLiteral("hint"));
    root->addWidget(hint);

    m_phoneEdit = new QLineEdit(this);
    m_phoneEdit->setPlaceholderText(QStringLiteral("请输入 11 位手机号"));
    m_phoneEdit->setMaxLength(11);
    root->addWidget(m_phoneEdit);

    auto *codeRow = new QHBoxLayout();
    codeRow->setSpacing(8);
    m_codeEdit = new QLineEdit(this);
    m_codeEdit->setPlaceholderText(QStringLiteral("6 位短信验证码"));
    m_codeEdit->setMaxLength(6);
    m_codeButton = new SmsCodeButton(m_client, m_phoneEdit, this);
    codeRow->addWidget(m_codeEdit, 1);
    codeRow->addWidget(m_codeButton, 0);
    root->addLayout(codeRow);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("新密码（6-20 位，不含空白字符）"));
    m_passwordEdit->setMaxLength(20);
    root->addWidget(m_passwordEdit);

    m_confirmEdit = new QLineEdit(this);
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setPlaceholderText(QStringLiteral("再次输入新密码"));
    m_confirmEdit->setMaxLength(20);
    root->addWidget(m_confirmEdit);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    buttonRow->addWidget(cancelButton, 0);
    m_resetButton = new QPushButton(QStringLiteral("重置密码"), this);
    m_resetButton->setProperty("class", QStringLiteral("smallPrimary"));
    m_resetButton->setDefault(true);
    buttonRow->addWidget(m_resetButton, 0);
    root->addLayout(buttonRow);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_resetButton, &QPushButton::clicked, this, &ResetPasswordDialog::onReset);
}

void ResetPasswordDialog::setPhone(const QString &phone)
{
    m_phoneEdit->setText(phone);
}

void ResetPasswordDialog::setEnsureConnected(std::function<bool()> fn)
{
    m_codeButton->setEnsureConnected(std::move(fn));
}

void ResetPasswordDialog::onReset()
{
    static const QRegularExpression phoneRe(QStringLiteral("^1\\d{10}$"));
    static const QRegularExpression codeRe(QStringLiteral("^\\d{6}$"));
    const QString phone = m_phoneEdit->text().trimmed();
    const QString code = m_codeEdit->text().trimmed();
    const QString newPassword = m_passwordEdit->text();
    if (!phoneRe.match(phone).hasMatch()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    if (!codeRe.match(code).hasMatch()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("请输入 6 位短信验证码"));
        return;
    }
    if (!ui::isPasswordValid(newPassword)) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("新密码须为 6-20 位且不含空白字符"));
        return;
    }
    if (newPassword != m_confirmEdit->text()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("两次输入的新密码不一致"));
        return;
    }

    m_resetButton->setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_password_reset"),
                          QJsonObject{{QStringLiteral("phone"), phone},
                                      {QStringLiteral("code"), code},
                                      {QStringLiteral("newPassword"), newPassword}},
                          [this](int code, const QString &msg, const QJsonObject &) {
                              m_resetButton->setEnabled(true);
                              if (code == 0) {
                                  QMessageBox::information(this, windowTitle(),
                                                           QStringLiteral("密码已重置，请使用新密码登录"));
                                  accept();
                                  return;
                              }
                              QString text;
                              if (code == 1001)
                                  text = QStringLiteral("验证码错误或已过期");
                              else if (code == 2002)
                                  text = QStringLiteral("该手机号尚未注册");
                              else if (code == 1005)
                                  text = QStringLiteral("该账号已注销");
                              else
                                  text = msg.isEmpty() ? QStringLiteral("重置失败，请稍后重试") : msg;
                              QMessageBox::warning(this, windowTitle(), text);
                          });
}
