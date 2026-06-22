#pragma once

#include <QObject>
#include <QThread>
#include <QAudioSink>
#include <QMutex>
class AudioOutput : public QThread
{
	Q_OBJECT
private:
	QAudioSink* audio = nullptr;
	QIODevice* outputdevice = nullptr;
	QMutex device_lock;

	volatile bool is_canRun;
	QMutex m_lock;
	void run();
	QString errorString();
public:
	AudioOutput(QObject *parent = 0);
	~AudioOutput();
	void stopImmediately();
	void startPlay();
	void stopPlay();
private slots:
	void handleStateChanged(QAudio::State);
	void setVolumn(int);
	void clearQueue();
signals:
	void audiooutputerror(QString);
	void speaker(QString);
};
