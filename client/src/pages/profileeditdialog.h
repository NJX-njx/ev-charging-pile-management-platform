#pragma once

#include <QDialog>

#include "model/models.h"

class QLabel;
class QLineEdit;
class QPushButton;
class SocketClient;

// 编辑资料独立对话框：头像选择（JPEG/PNG ≤512 KiB，带预览）与昵称（1~20 字符）
// 合并提交 user_profile_update；无字段变更时不发请求，提示后直接关闭。
class ProfileEditDialog : public QDialog
{
    Q_OBJECT
public:
    ProfileEditDialog(SocketClient *client, const UserInfo &user, QWidget *parent = nullptr);

signals:
    void profileUpdated(const UserInfo &user);

private:
    void onChooseAvatar();
    void onSave();

    SocketClient *m_client;
    UserInfo m_user; // 打开时的资料快照，用于变更对比
    QLabel *m_avatarPreview;
    QLineEdit *m_nicknameEdit;
    QPushButton *m_saveButton;
    bool m_avatarDirty = false;
    QByteArray m_avatarBytes;
    QString m_avatarMime;
};
