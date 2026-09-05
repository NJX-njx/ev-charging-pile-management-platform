#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class SocketClient;

// 独立充值页（需求矩阵 NO.16/17）：预设金额档位 + 自定义金额（>0、≤10000、两位小数），
// 点「支付」调 wallet_recharge；结果未知（断线）时不自动重试，提示先刷新余额。
class RechargeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RechargeDialog(SocketClient *client, QWidget *parent = nullptr);

signals:
    void recharged(double newBalance);

private:
    void onPay();
    void setBusy(bool busy);

    SocketClient *m_client;
    QList<QPushButton *> m_presetButtons;
    QLineEdit *m_amountEdit;
    QPushButton *m_payButton;
};
