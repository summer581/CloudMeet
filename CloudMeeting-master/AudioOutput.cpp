#include "AudioOutput.h"
#include <QMutexLocker>
#include "netheader.h"
#include <QDebug>
#include <QHostAddress>
#include <QAudioDevice>
#include <QMediaDevices>

#ifndef FRAME_LEN_125MS
#define FRAME_LEN_125MS 1900
#endif
extern QUEUE_DATA<MESG> audio_recv;

AudioOutput::AudioOutput(QObject *parent)
	: QThread(parent)
{
	QAudioFormat format;
	format.setSampleRate(8000);
	format.setChannelCount(1);
	format.setSampleFormat(QAudioFormat::Int16);

	QAudioDevice device = QMediaDevices::defaultAudioOutput();
	if (!device.isFormatSupported(format))
	{
		qWarning() << "Raw audio format not supported by backend, cannot play audio.";
		return;
	}
	audio = new QAudioSink(device, format, this);
	outputdevice = nullptr;
}

AudioOutput::~AudioOutput()
{
	if (audio)
		delete audio;
}

QString AudioOutput::errorString()
{
	return QString("AudioOutput error occurred").toUtf8();
}

void AudioOutput::handleStateChanged(QAudio::State state)
{
	Q_UNUSED(state)
}

void AudioOutput::startPlay()
{
	WRITE_LOG("start playing audio");
	if (audio == nullptr)
	{
		qWarning() << "AudioOutput::startPlay: audio sink not initialized";
		return;
	}
	outputdevice = audio->start();
}

void AudioOutput::stopPlay()
{
	{
		QMutexLocker lock(&device_lock);
		outputdevice = nullptr;
	}
	if (audio)
		audio->stop();
	WRITE_LOG("stop playing audio");
}

void AudioOutput::run()
{
	is_canRun = true;
	QByteArray m_pcmDataBuffer;

	WRITE_LOG("start playing audio thread 0x%p", QThread::currentThreadId());
	for (;;)
	{
		{
			QMutexLocker lock(&m_lock);
			if (is_canRun == false)
			{
				stopPlay();
				WRITE_LOG("stop playing audio thread 0x%p", QThread::currentThreadId());
				return;
			}
		}
		MESG* msg = audio_recv.pop_msg();
		if (msg == NULL) continue;
		{
			QMutexLocker lock(&device_lock);
			if (outputdevice != nullptr)
			{
				m_pcmDataBuffer.append((char*)msg->data, msg->len);

				if (m_pcmDataBuffer.size() >= FRAME_LEN_125MS)
				{
					//д����Ƶ����
					qint64 ret = outputdevice->write(m_pcmDataBuffer.data(), FRAME_LEN_125MS);
					if (ret < 0)
					{
						qDebug() << outputdevice->errorString();
						return;
					}
					else
					{
						emit speaker(QHostAddress(msg->ip).toString());
						m_pcmDataBuffer = m_pcmDataBuffer.right(m_pcmDataBuffer.size() - ret);
					}
				}
			}
			else
			{
				m_pcmDataBuffer.clear();
			}
		}
		if (msg->data) free(msg->data);
		if (msg) free(msg);
	}
}
void AudioOutput::stopImmediately()
{
	QMutexLocker lock(&m_lock);
	is_canRun = false;
}


void AudioOutput::setVolumn(int val)
{
	if (audio)
		audio->setVolume(val / 100.0);
}

void AudioOutput::clearQueue()
{
	qDebug() << "audio recv clear";
	audio_recv.clear();
}
