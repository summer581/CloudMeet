#include "myvideosurface.h"
#include <QImage>
#include <QVideoFrameFormat>

MyVideoSurface::MyVideoSurface(QObject *parent)
    : QObject(parent)
    , _sink(new QVideoSink(this))
{
    connect(_sink, &QVideoSink::videoFrameChanged, this, &MyVideoSurface::onVideoFrameChanged);
}

QVideoSink* MyVideoSurface::videoSink() const
{
    return _sink;
}

void MyVideoSurface::onVideoFrameChanged(const QVideoFrame &frame)
{
    if (!frame.isValid()) {
        return;
    }

    QVideoFrame mapped(frame);
    if (!mapped.map(QVideoFrame::ReadOnly)) {
        return;
    }

    QImage img = mapped.toImage();
    if (!img.isNull()) {
        QImage copy = img.copy();
        mapped.unmap();
        emit frameAvailable(copy);
    } else {
        mapped.unmap();
        QVideoFrame fallback(frame);
        if (fallback.map(QVideoFrame::ReadOnly)) {
            QVideoFrameFormat fallbackFmt = fallback.surfaceFormat();
            int w = fallback.width();
            int h = fallback.height();
            QImage converted;
            switch (fallbackFmt.pixelFormat()) {
                case QVideoFrameFormat::Format_ARGB8888:
                case QVideoFrameFormat::Format_XRGB8888:
                    converted = QImage(fallback.bits(0), w, h,
                                       fallback.bytesPerLine(0),
                                       QImage::Format_ARGB32).copy();
                    break;
                case QVideoFrameFormat::Format_RGBX8888:
                    converted = QImage(fallback.bits(0), w, h,
                                       fallback.bytesPerLine(0),
                                       QImage::Format_RGBX8888).copy();
                    break;
                case QVideoFrameFormat::Format_BGRA8888:
                case QVideoFrameFormat::Format_BGRX8888:
                    converted = QImage(fallback.bits(0), w, h,
                                       fallback.bytesPerLine(0),
                                       QImage::Format_ARGB32).copy();
                    break;
                case QVideoFrameFormat::Format_YUV420P:
                case QVideoFrameFormat::Format_YV12:
                case QVideoFrameFormat::Format_NV12:
                case QVideoFrameFormat::Format_NV21:
                case QVideoFrameFormat::Format_UYVY:
                case QVideoFrameFormat::Format_YUYV: {
                    if (fallbackFmt.pixelFormat() == QVideoFrameFormat::Format_YUV420P) {
                        converted = QImage(w, h, QImage::Format_RGB32);
                        const uchar *yPlane = fallback.bits(0);
                        const uchar *uPlane = fallback.bits(1);
                        const uchar *vPlane = fallback.bits(2);
                        int yStride = fallback.bytesPerLine(0);
                        int uStride = fallback.bytesPerLine(1);
                        int vStride = fallback.bytesPerLine(2);
                        for (int y = 0; y < h; ++y) {
                            QRgb *rgbLine = reinterpret_cast<QRgb*>(converted.scanLine(y));
                            for (int x = 0; x < w; ++x) {
                                int yv = yPlane[y * yStride + x];
                                int uv = uPlane[(y/2) * uStride + (x/2)] - 128;
                                int vv = vPlane[(y/2) * vStride + (x/2)] - 128;
                                int r = qBound(0, yv + (int)(1.402f * vv), 255);
                                int g = qBound(0, yv - (int)(0.344f * uv) - (int)(0.714f * vv), 255);
                                int b = qBound(0, yv + (int)(1.772f * uv), 255);
                                rgbLine[x] = qRgb(r, g, b);
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            fallback.unmap();
            if (!converted.isNull()) {
                emit frameAvailable(converted);
            }
        }
    }
}
