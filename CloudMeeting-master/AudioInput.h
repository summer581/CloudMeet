#pragma once

#include <QObject>
#include <QAudioSource>
#include <QIODevice>
class AudioInput : public QObject
{
	Q_OBJECT
private:
	QAudioSource *audio = nullptr;
	QIODevice* inputdevice = nullptr;
	char* recvbuf;
public:
	AudioInput(QObject *par = 0);
	~AudioInput();
private slots:
	void onreadyRead();
	void handleStateChanged(QAudio::State);
	QString errorString();
	void setVolumn(int);
public slots:
	void startCollect();
	void stopCollect();
signals:
	void audioinputerror(QString);
};

