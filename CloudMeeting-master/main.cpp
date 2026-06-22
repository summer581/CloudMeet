#include <QApplication>
#include "widget.h"
#include "screen.h"

int main(int argc, char* argv[])
{
    // 禁用 Qt RHI GPU 后端，强制视频帧走 CPU 内存路径
    qputenv("QT_MEDIA_PLAYER_USE_RHI", "0");
    qputenv("QSG_RHI_BACKEND", "null");
    // 尝试切换 Qt 多媒体后端为 ffmpeg，某些摄像头在 ffmpeg 下能枚举到更多彩色格式
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");

    QApplication app(argc, argv);
    Screen::init();

    Widget w;
    w.show();
    return app.exec();
}
