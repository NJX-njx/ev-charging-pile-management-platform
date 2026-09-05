#include "rechargedialog.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "net/socketclient.h"

namespace {
const QList<int> kPresetAmounts{50, 100, 200, 500};
}

RechargeDialog::RechargeDialog(SocketClient *client, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
{
    setWindowTitle(QStringLiteral("余额充值"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *presetTitle = new QLabel(QStringLiteral("选择充值金额"), this);
    presetTitle->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(presetTitle);

    auto *presetRow = new QHBoxLayout();
    presetRow->setSpacing(8);
    for (int amount : kPresetAmounts) {
        auto *btn = new QPushButton(QStringLiteral("¥%1").arg(amount), this);
        btn->setObjectName(QStringLiteral("segmentButton"));
        btn->setCheckable(true);
        connect(btn, &QPushButton::clicked, this, [this, amount, btn]() {
            for (QPushButton *b : m_presetButtons)
                b->setChecked(b == btn);
            m_amountEdit->setText(QString::number(amount));
        });
        m_presetButtons.append(btn);
        presetRow->addWidget(btn, 1);
    }
    layout->addLayout(presetRow);

    auto *customTitle = new QLabel(QStringLiteral("自定义金额"), this);
    customTitle->setObjectName(QStringLiteral("hint"));
    layout->addWidget(customTitle);

    m_amountEdit = new QLineEdit(this);
    m_amountEdit->setPlaceholderText(QStringLiteral("充值金额（≤10000，最多两位小数）"));
    m_amountEdit->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
    layout->addWidget(m_amountEdit);
    connect(m_amountEdit, &QLineEdit::textEdited, this, [this]() {
        for (QPushButton *b : m_presetButtons)
            b->setChecked(false);
    });

    m_payButton = new QPushButton(QStringLiteral("支付"), this);
    m_payButton->setProperty("class", QStringLiteral("primary"));
    layout->addWidget(m_payButton);

    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    layout->addWidget(cancelButton);

    connect(m_payButton, &QPushButton::clicked, this, &RechargeDialog::onPay);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    resize(380, 300);
}

void RechargeDialog::onPay()
{
    static const QRegularExpression amountRe(QStringLiteral("^[0-9]+(\\.[0-9]{1,2})?$"));
    const QString text = m_amountEdit->text().trimmed();
    if (!amountRe.match(text).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("充值"),
                             QStringLiteral("金额格式不正确，最多两位小数"));
        return;
    }
    const double amount = text.toDouble();
    if (amount <= 0 || amount > 10000) {
        QMessageBox::warning(this, QStringLiteral("充值"),
                             QStringLiteral("充值金额须大于 0 且不超过 10000"));
        return;
    }
    setBusy(true);
    // exec() 期间用户可关闭对话框（栈上对象随即析构），回调必须经 QPointer 判空
    const QPointer<RechargeDialog> guard(this);
    m_client->sendRequest(QStringLiteral("wallet_recharge"),
                          QJsonObject{{QStringLiteral("amount"), amount}},
                          [guard](int code, const QString &msg, const QJsonObject &data) {
                              if (!guard)
                                  return;
                              RechargeDialog *self = guard.data();
                              self->setBusy(false);
                              if (code == SocketClient::kErrConnectionLost) {
                                  // 结果未知：不自动重试，避免重复扣款
                                  QMessageBox::warning(self, QStringLiteral("充值"),
                                                       QStringLiteral("网络中断，充值结果未知，请刷新余额确认后再决定是否重新充值"));
                                  return;
                              }
                              if (code != 0) {
                                  QMessageBox::warning(self, QStringLiteral("充值"),
                                                       msg.isEmpty() ? QStringLiteral("充值失败") : msg);
                                  return;
                              }
                              const double balance = data.value(QStringLiteral("balance")).toDouble();
                              emit self->recharged(balance);
                              QMessageBox::information(self, QStringLiteral("充值"),
                                                       QStringLiteral("充值成功，当前余额 %1 元")
                                                           .arg(balance, 0, 'f', 2));
                              self->accept();
                          });
}

void RechargeDialog::setBusy(bool busy)
{
    m_payButton->setEnabled(!busy);
    m_amountEdit->setEnabled(!busy);
    for (QPushButton *b : m_presetButtons)
        b->setEnabled(!busy);
}
