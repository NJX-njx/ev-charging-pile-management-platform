#pragma once

#include <QDialog>
#include <QJsonObject>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

// 用户管理「修改用户」对话框：以 user_detail 返回的完整资料构造，
// 支持修改手机号/昵称/余额与头像（JPEG/PNG ≤512KiB，可显式清除）。
// updatePayload() 只收集相对初始资料发生变化的字段（user_update 要求至少提供其一）。
class UserEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserEditDialog(const QJsonObject &user, QWidget *parent = nullptr);

    QString phone() const;
    QString nickname() const;
    // 仅含改动字段的 user_update payload（不含 userId）；未做任何修改时为空对象
    QJsonObject updatePayload() const;

private:
    // 从图片文件载入头像（JPEG/PNG，≤512KiB）。
    bool chooseAvatarFromFile(const QString &path);
    void refreshAvatarPreview();

    QLineEdit *m_phoneEdit;
    QLineEdit *m_nicknameEdit;
    QDoubleSpinBox *m_balanceBox;
    QLabel *m_avatarPreview;
    QPushButton *m_chooseAvatarBtn;
    QPushButton *m_clearAvatarBtn;

    const QString m_initialPhone;
    const QString m_initialNickname;
    const double m_initialBalance;
    const QJsonObject m_initialAvatar; // 无头像时为空对象
    // 头像三态：0=不修改，1=替换为 m_newAvatar，2=显式清除（协议 avatar 传 null）
    int m_avatarMode = 0;
    QJsonObject m_newAvatar;
};
