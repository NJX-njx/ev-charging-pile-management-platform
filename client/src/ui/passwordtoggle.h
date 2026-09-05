#pragma once

#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QWidget>

namespace ui {

// 密码框「显示/隐藏」切换：把密码框包进一行，右侧放纯文本切换按钮，
// 点击在 Password/Normal 回显间切换（样式见 style.qss 的 QToolButton#passwordToggle）
inline QWidget *withPasswordToggle(QLineEdit *edit, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    edit->setParent(row);
    auto *toggle = new QToolButton(row);
    toggle->setObjectName(QStringLiteral("passwordToggle"));
    toggle->setCheckable(true);
    toggle->setText(QStringLiteral("显示"));
    toggle->setCursor(Qt::PointingHandCursor);
    toggle->setFocusPolicy(Qt::NoFocus);
    QObject::connect(toggle, &QToolButton::toggled, edit, [edit, toggle](bool visible) {
        edit->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        toggle->setText(visible ? QStringLiteral("隐藏") : QStringLiteral("显示"));
    });

    layout->addWidget(edit, 1);
    layout->addWidget(toggle, 0);
    return row;
}

} // namespace ui
