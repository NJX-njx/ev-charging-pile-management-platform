#include "passworddialog.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "ui/passwordtoggle.h"
#include "ui/uienums.h"

PasswordDialog::PasswordDialog(SocketClient *client, bool hasPassword, bool allowSkip, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
    , m_hasPassword(hasPassword)
{
    const QString titleText = hasPassword ? QStringLiteral("修改密码") : QStringLiteral("设置登录密码");
    setWindowTitle(titleText);
    setModal(true);
    setMinimumWidth(340);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    auto *title = new QLabel(titleText, this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);
    if (!hasPassword) {
        auto *hint = new QLabel(QStringLiteral("当前账号还未设置登录密码，设置后下次可直接使用密码登录"), this);
        hint->setObjectName(QStringLiteral("hint"));
        hint->setWordWrap(true);
        root->addWidget(hint);
    }

    if (m_hasPassword) {
        m_oldEdit = new QLineEdit(this);
        m_oldEdit->setEchoMode(QLineEdit::Password);
        m_oldEdit->setPlaceholderText(QStringLiteral("请输入原密码"));
        m_oldEdit->setMaxLength(20);
        root->addWidget(m_oldEdit);
    }

    m_newEdit = new QLineEdit(this);
    m_newEdit->setEchoMode(QLineEdit::Password);
    m_newEdit->setPlaceholderText(QStringLiteral("新密码（6-20 位，不含空白字符）"));
    m_newEdit->setMaxLength(20);
    root->addWidget(ui::withPasswordToggle(m_newEdit, this));

    m_confirmEdit = new QLineEdit(this);
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setPlaceholderText(QStringLiteral("再次输入新密码"));
    m_confirmEdit->setMaxLength(20);
    root->addWidget(m_confirmEdit);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto *cancelButton = new QPushButton(allowSkip ? QStringLiteral("暂不设置") : QStringLiteral("取消"), this);
    buttonRow->addWidget(cancelButton, 0);
    m_submitButton = new QPushButton(QStringLiteral("确定"), this);
    m_submitButton->setProperty("class", QStringLiteral("smallPrimary"));
    m_submitButton->setDefault(true);
    buttonRow->addWidget(m_submitButton, 0);
    root->addLayout(buttonRow);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_submitButton, &QPushButton::clicked, this, &PasswordDialog::onSubmit);
}

void PasswordDialog::onSubmit()
{
    const QString oldPassword = m_hasPassword ? m_oldEdit->text() : QString();
    const QString newPassword = m_newEdit->text();
    if (m_hasPassword && oldPassword.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("请输入原密码"));
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
    if (m_hasPassword && newPassword == oldPassword) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("新密码不能与原密码相同"));
        return;
    }

    m_submitButton->setEnabled(false);
    QJsonObject payload{{QStringLiteral("newPassword"), newPassword}};
    if (m_hasPassword)
        payload.insert(QStringLiteral("oldPassword"), oldPassword);
    m_client->sendRequest(QStringLiteral("user_password_update"), payload,
                          [this, newPassword](int code, const QString &msg, const QJsonObject &) {
                              m_submitButton->setEnabled(true);
                              if (code == 0) {
                                  emit passwordUpdated(newPassword);
                                  QMessageBox::information(this, windowTitle(),
                                                           m_hasPassword
                                                               ? QStringLiteral("密码修改成功")
                                                               : QStringLiteral("密码设置成功，下次登录可使用密码登录"));
                                  accept();
                                  return;
                              }
                              QString text;
                              if (code == 1001)
                                  text = m_hasPassword ? QStringLiteral("原密码错误")
                                                       : (msg.isEmpty() ? QStringLiteral("设置失败") : msg);
                              else if (code == 2001)
                                  text = msg.isEmpty() ? QStringLiteral("新密码不符合规则") : msg;
                              else
                                  text = msg.isEmpty() ? QStringLiteral("操作失败，请稍后重试") : msg;
                              QMessageBox::warning(this, windowTitle(), text);
                          });
}
