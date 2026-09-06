#include "usereditdialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

// 协议 3.1/7.18：头像仅 JPEG 或 PNG，Base64 解码后不超过 512 KiB
constexpr qint64 kMaxAvatarBytes = 512 * 1024;

QPixmap avatarPixmap(const QJsonObject &avatar)
{
    QPixmap pm;
    pm.loadFromData(QByteArray::fromBase64(avatar[QStringLiteral("base64")].toString().toUtf8()));
    return pm;
}

} // namespace

UserEditDialog::UserEditDialog(const QJsonObject &user, QWidget *parent)
    : QDialog(parent),
      m_initialPhone(user[QStringLiteral("phone")].toString()),
      m_initialNickname(user[QStringLiteral("nickname")].toString()),
      m_initialBalance(user[QStringLiteral("balance")].toDouble()),
      m_initialAvatar(user[QStringLiteral("avatar")].toObject())
{
    setObjectName(QStringLiteral("userEditDialog"));
    setWindowTitle(QStringLiteral("修改用户"));
    QFormLayout *form = new QFormLayout(this);

    QLineEdit *idEdit = new QLineEdit(QString::number(user[QStringLiteral("userId")].toInt()));
    idEdit->setEnabled(false);
    m_phoneEdit = new QLineEdit(m_initialPhone);
    m_phoneEdit->setObjectName(QStringLiteral("editPhone"));
    m_nicknameEdit = new QLineEdit(m_initialNickname);
    m_nicknameEdit->setObjectName(QStringLiteral("editNickname"));
    m_nicknameEdit->setMaxLength(20);
    m_balanceBox = new QDoubleSpinBox;
    m_balanceBox->setObjectName(QStringLiteral("spinBalance"));
    m_balanceBox->setRange(0.0, 1000000.0);
    m_balanceBox->setDecimals(2);
    m_balanceBox->setSingleStep(10.0);
    m_balanceBox->setValue(m_initialBalance);

    // 头像：预览（无头像显示占位文字）+ 选择/清除按钮
    m_avatarPreview = new QLabel;
    m_avatarPreview->setObjectName(QStringLiteral("avatarPreview"));
    m_avatarPreview->setFixedSize(96, 96);
    m_avatarPreview->setAlignment(Qt::AlignCenter);
    m_avatarPreview->setFrameShape(QFrame::StyledPanel);
    m_chooseAvatarBtn = new QPushButton(QStringLiteral("选择图片…"));
    m_chooseAvatarBtn->setObjectName(QStringLiteral("btnChooseAvatar"));
    m_clearAvatarBtn = new QPushButton(QStringLiteral("清除头像"));
    m_clearAvatarBtn->setObjectName(QStringLiteral("btnClearAvatar"));
    QVBoxLayout *avatarBtns = new QVBoxLayout;
    avatarBtns->addWidget(m_chooseAvatarBtn);
    avatarBtns->addWidget(m_clearAvatarBtn);
    avatarBtns->addStretch();
    QHBoxLayout *avatarRow = new QHBoxLayout;
    avatarRow->addWidget(m_avatarPreview);
    avatarRow->addLayout(avatarBtns);
    avatarRow->addStretch();

    form->addRow(QStringLiteral("用户ID"), idEdit);
    form->addRow(QStringLiteral("手机号"), m_phoneEdit);
    form->addRow(QStringLiteral("昵称"), m_nicknameEdit);
    form->addRow(QStringLiteral("余额(元)"), m_balanceBox);
    form->addRow(QStringLiteral("头像"), avatarRow);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    connect(m_chooseAvatarBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择头像图片"),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            QStringLiteral("图片文件 (*.jpg *.jpeg *.png)"));
        if (!path.isEmpty())
            chooseAvatarFromFile(path);
    });
    connect(m_clearAvatarBtn, &QPushButton::clicked, this, [this]() {
        m_avatarMode = 2;
        m_newAvatar = QJsonObject();
        refreshAvatarPreview();
    });

    refreshAvatarPreview();
}

QString UserEditDialog::phone() const
{
    return m_phoneEdit->text().trimmed();
}

QString UserEditDialog::nickname() const
{
    return m_nicknameEdit->text().trimmed();
}

bool UserEditDialog::chooseAvatarFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("选择头像"), QStringLiteral("无法打开文件：%1").arg(path));
        return false;
    }
    if (file.size() > kMaxAvatarBytes) {
        QMessageBox::warning(this, QStringLiteral("选择头像"),
                             QStringLiteral("图片大小不能超过 512 KiB（当前 %1 KiB）").arg(file.size() / 1024));
        return false;
    }
    const QString suffix = QFileInfo(file).suffix().toLower();
    QString mime;
    if (suffix == QStringLiteral("png"))
        mime = QStringLiteral("image/png");
    else if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        mime = QStringLiteral("image/jpeg");
    else {
        QMessageBox::warning(this, QStringLiteral("选择头像"), QStringLiteral("仅支持 JPEG 或 PNG 图片"));
        return false;
    }
    const QByteArray data = file.readAll();
    QPixmap pm;
    if (!pm.loadFromData(data)) {
        QMessageBox::warning(this, QStringLiteral("选择头像"), QStringLiteral("无法读取图片内容，文件可能已损坏"));
        return false;
    }
    m_newAvatar = QJsonObject{{QStringLiteral("mime"), mime},
                              {QStringLiteral("base64"), QString::fromLatin1(data.toBase64())}};
    m_avatarMode = 1;
    refreshAvatarPreview();
    return true;
}

void UserEditDialog::refreshAvatarPreview()
{
    QPixmap pm;
    if (m_avatarMode == 1)
        pm = avatarPixmap(m_newAvatar);
    else if (m_avatarMode == 0)
        pm = avatarPixmap(m_initialAvatar);
    if (pm.isNull()) {
        m_avatarPreview->setPixmap(QPixmap());
        m_avatarPreview->setText(m_avatarMode == 2 ? QStringLiteral("（已清除）")
                                                   : QStringLiteral("（无头像）"));
    } else {
        m_avatarPreview->setText(QString());
        m_avatarPreview->setPixmap(pm.scaled(m_avatarPreview->size(), Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
    }
    // 仅在当前存在头像（原有未改或新选）时可清除
    m_clearAvatarBtn->setEnabled(m_avatarMode == 1
                                 || (m_avatarMode == 0 && !m_initialAvatar.isEmpty()));
}

QJsonObject UserEditDialog::updatePayload() const
{
    QJsonObject payload;
    if (phone() != m_initialPhone)
        payload[QStringLiteral("phone")] = phone();
    if (nickname() != m_initialNickname)
        payload[QStringLiteral("nickname")] = nickname();
    // 余额以分为单位比较，避免浮点误差把原值误判为改动
    if (qRound64(m_balanceBox->value() * 100.0) != qRound64(m_initialBalance * 100.0))
        payload[QStringLiteral("balance")] = m_balanceBox->value();
    if (m_avatarMode == 1)
        payload[QStringLiteral("avatar")] = m_newAvatar;
    else if (m_avatarMode == 2)
        payload[QStringLiteral("avatar")] = QJsonValue::Null; // 显式清除头像
    return payload;
}
