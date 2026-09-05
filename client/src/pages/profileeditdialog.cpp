#include "profileeditdialog.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "net/socketclient.h"
#include "ui/avatarutils.h"

ProfileEditDialog::ProfileEditDialog(SocketClient *client, const UserInfo &user, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
    , m_user(user)
{
    setWindowTitle(QStringLiteral("编辑资料"));
    setModal(true);
    setMinimumWidth(320);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("编辑资料"), this);
    title->setObjectName(QStringLiteral("pageTitle"));
    root->addWidget(title);

    auto *avatarRow = new QHBoxLayout();
    m_avatarPreview = new QLabel(this);
    m_avatarPreview->setObjectName(QStringLiteral("avatar"));
    m_avatarPreview->setFixedSize(64, 64);
    m_avatarPreview->setAlignment(Qt::AlignCenter);
    bool hasPreview = false;
    if (m_user.hasAvatar) {
        QPixmap pix;
        if (pix.loadFromData(m_user.avatarBytes)) {
            m_avatarPreview->setPixmap(avatar::roundedAvatar(pix, 64));
            hasPreview = true;
        }
    }
    if (!hasPreview)
        m_avatarPreview->setText(QStringLiteral("头像"));
    avatarRow->addWidget(m_avatarPreview, 0);
    auto *avatarColumn = new QVBoxLayout();
    auto *chooseButton = new QPushButton(QStringLiteral("选择头像"), this);
    chooseButton->setProperty("class", QStringLiteral("small"));
    auto *avatarHint = new QLabel(QStringLiteral("支持 JPEG / PNG，不超过 512 KiB"), this);
    avatarHint->setObjectName(QStringLiteral("hint"));
    avatarColumn->addWidget(chooseButton);
    avatarColumn->addWidget(avatarHint);
    avatarRow->addLayout(avatarColumn, 1);
    root->addLayout(avatarRow);

    m_nicknameEdit = new QLineEdit(this);
    m_nicknameEdit->setMaxLength(20);
    m_nicknameEdit->setPlaceholderText(QStringLiteral("昵称（1-20 个字符）"));
    m_nicknameEdit->setText(m_user.nickname);
    root->addWidget(m_nicknameEdit);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    buttonRow->addWidget(cancelButton, 0);
    m_saveButton = new QPushButton(QStringLiteral("保存"), this);
    m_saveButton->setProperty("class", QStringLiteral("smallPrimary"));
    m_saveButton->setDefault(true);
    buttonRow->addWidget(m_saveButton, 0);
    root->addLayout(buttonRow);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(chooseButton, &QPushButton::clicked, this, &ProfileEditDialog::onChooseAvatar);
    connect(m_saveButton, &QPushButton::clicked, this, &ProfileEditDialog::onSave);
}

void ProfileEditDialog::onChooseAvatar()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择头像"), QString(),
        QStringLiteral("图片文件 (*.jpg *.jpeg *.png)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!avatar::loadAvatarFile(path, &m_avatarBytes, &m_avatarMime, &error)) {
        QMessageBox::warning(this, QStringLiteral("选择头像"), error);
        return;
    }
    QPixmap pix;
    if (pix.loadFromData(m_avatarBytes)) {
        m_avatarPreview->setPixmap(avatar::roundedAvatar(pix, 64));
        m_avatarDirty = true;
    } else {
        QMessageBox::warning(this, QStringLiteral("选择头像"), QStringLiteral("图片内容无法解析"));
    }
}

void ProfileEditDialog::onSave()
{
    const QString nickname = m_nicknameEdit->text().trimmed();
    if (nickname.isEmpty() || nickname.size() > 20) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("昵称长度须为 1-20 个字符"));
        return;
    }

    // 协议 6.3：至少提供 nickname 或 avatar 之一，未修改的字段省略
    QJsonObject payload;
    if (nickname != m_user.nickname)
        payload.insert(QStringLiteral("nickname"), nickname);
    if (m_avatarDirty) {
        payload.insert(QStringLiteral("avatar"),
                       QJsonObject{{QStringLiteral("mime"), m_avatarMime},
                                   {QStringLiteral("base64"), QString::fromLatin1(m_avatarBytes.toBase64())}});
    }
    if (payload.isEmpty()) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("资料未做任何修改"));
        accept();
        return;
    }

    m_saveButton->setEnabled(false);
    m_client->sendRequest(QStringLiteral("user_profile_update"), payload,
                          [this](int code, const QString &msg, const QJsonObject &data) {
                              m_saveButton->setEnabled(true);
                              if (code != 0) {
                                  QMessageBox::warning(this, windowTitle(),
                                                       msg.isEmpty() ? QStringLiteral("保存失败，请稍后重试") : msg);
                                  return;
                              }
                              m_user = UserInfo::fromJson(data.value(QStringLiteral("user")).toObject());
                              emit profileUpdated(m_user);
                              QMessageBox::information(this, windowTitle(), QStringLiteral("资料已保存"));
                              accept();
                          });
}
