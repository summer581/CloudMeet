#include "AudioInput.h"
#include "netheader.h"
#include <QAudioFormat>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QDebug>
#include <QThread>

extern QUEUE_DATA<MESG> queue_send;
extern QUEUE_DATA<MESG> queue_recv;

AudioInput::AudioInput(QObject *parent)
	: QObject(parent)
{
	recvbuf = (char*)malloc(MB * 2);
	QAudioFormat format;
	format.setSampleRate(8000);
	format.setChannelCount(1);
	format.setSampleFormat(QAudioFormat::Int16);

	QAudioDevice device = QMediaDevices::defaultAudioInput();
	if (!device.isFormatSupported(format))
	{
		qWarning() << "Default format not supported, trying to use the nearest.";
		format = device.preferredFormat();
	}
	audio = new QAudioSource(device, format, this);
}

AudioInput::~AudioInput()
{
	if (audio)
		delete audio;
	if (recvbuf)
		free(recvbuf);
}

void AudioInput::startCollect()
{
	WRITE_LOG("start collecting audio");
	if (audio == nullptr)
	{
		qWarning() << "AudioInput::startCollect: audio source not initialized";
		return;
	}
	inputdevice = audio->start();
	if (inputdevice)
		connect(inputdevice, SIGNAL(readyRead()), this, SLOT(onreadyRead()));
}

void AudioInput::stopCollect()
{
    if (inputdevice)
    {
        disconnect(inputdevice, SIGNAL(readyRead()), this, SLOT(onreadyRead()));
        inputdevice = nullptr;
    }
    if (audio)
        audio->stop();
    WRITE_LOG("stop collecting audio");
}

void AudioInput::onreadyRead()
{
	static int num = 0, totallen  = 0;
	if (inputdevice == nullptr) return;
	int len = inputdevice->read(recvbuf + totallen, 2 * MB - totallen);
	if (num < 2)
	{
		totallen += len;
		num++;
		return;
	}
	totallen += len;
	qDebug() << "totallen = " << totallen;
	MESG* msg = (MESG*)malloc(sizeof(MESG));
	if (msg == nullptr)
	{
		qWarning() << __LINE__ << "malloc fail";
	}
	else
	{
		memset(msg, 0, sizeof(MESG));
		msg->msg_type = AUDIO_SEND;
		//ѹ�����ݣ�תbase64
		QByteArray rr(recvbuf, totallen);
		QByteArray cc = qCompress(rr).toBase64();
		msg->len = cc.size();

		msg->data = (uchar*)malloc(msg->len);
		if (msg->data == nullptr)
		{
			qWarning() << "malloc mesg.data fail";
		}
		else
		{
			memset(msg->data, 0, msg->len);
			memcpy_s(msg->data, msg->len, cc.data(), cc.size());
			queue_send.push_msg(msg);
		}
	}
	totallen = 0;
	num = 0;
}

QString AudioInput::errorString()
{
	return QString("AudioInput error occurred").toUtf8();
}

void AudioInput::handleStateChanged(QAudio::State newState)
{
	Q_UNUSED(newState)
}

void AudioInput::setVolumn(int v)
{
	qDebug() << v;
	if (audio)
		audio->setVolume(v / 100.0);
}
