#pragma once

// 头像处理公共辅助：圆形裁剪预览、JPEG/PNG 魔数校验、文件读取（≤512 KiB）。
// 供 MyPage 展示与 ProfileEditDialog 编辑共用，规则与 protocol 6.3 一致。

#include <QByteArray>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QString>

namespace avatar {

constexpr qint64 kMaxAvatarBytes = 512 * 1024;

inline QPixmap roundedAvatar(const QPixmap &src, int size)
{
    const QPixmap scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, 8, 8);
    painter.setClipPath(path);
    const int x = (size - scaled.width()) / 2;
    const int y = (size - scaled.height()) / 2;
    painter.drawPixmap(x, y, scaled);
    return out;
}

inline bool looksLikeJpegOrPng(const QByteArray &bytes, QString *mime)
{
    if (bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xFF
        && static_cast<unsigned char>(bytes[1]) == 0xD8
        && static_cast<unsigned char>(bytes[2]) == 0xFF) {
        *mime = QStringLiteral("image/jpeg");
        return true;
    }
    if (bytes.size() >= 8
        && static_cast<unsigned char>(bytes[0]) == 0x89
        && bytes.mid(1, 3) == "PNG"
        && static_cast<unsigned char>(bytes[4]) == 0x0D
        && static_cast<unsigned char>(bytes[5]) == 0x0A) {
        *mime = QStringLiteral("image/png");
        return true;
    }
    return false;
}

// 读取并校验头像文件；失败时 error 为可展示文案
inline bool loadAvatarFile(const QString &path, QByteArray *bytes, QString *mime, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取所选文件");
        return false;
    }
    const QByteArray data = file.readAll();
    if (data.size() > kMaxAvatarBytes) {
        *error = QStringLiteral("头像文件不得超过 512 KiB");
        return false;
    }
    if (!looksLikeJpegOrPng(data, mime)) {
        *error = QStringLiteral("头像仅支持 JPEG 或 PNG 格式");
        return false;
    }
    *bytes = data;
    return true;
}

} // namespace avatar
