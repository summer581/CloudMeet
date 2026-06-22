#ifndef MYVIDEOSURFACE_H
#define MYVIDEOSURFACE_H

#include <QObject>
#include <QVideoFrame>
#include <QVideoSink>

class MyVideoSurface : public QObject
{
    Q_OBJECT
public:
    explicit MyVideoSurface(QObject *parent = nullptr);
    QVideoSink* videoSink() const;

signals:
    void frameAvailable(const QImage &);

private slots:
    void onVideoFrameChanged(const QVideoFrame &frame);

private:
    QVideoSink *_sink;
};

#endif // MYVIDEOSURFACE_H
