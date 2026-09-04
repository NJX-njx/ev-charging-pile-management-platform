#pragma once

// 腾讯地图 Key 集中配置项。
// 占位说明：请在 lbs.qq.com 控制台创建应用并勾选 WebServiceAPI 的地址解析（Geocoder）能力，
// 把 Key 填入下方字符串后重新构建即可启用「找站」页的地址解析；留空时该入口不可用，
// 界面会提示改用手动经纬度输入。
#include <QString>

namespace mapconfig {
const QString kTencentMapKey = QStringLiteral("T25BZ-5TCK7-J56XJ-PSLJJ-WFY6Q-3BBXZ");
}
