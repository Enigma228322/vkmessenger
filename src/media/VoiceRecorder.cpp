#include "VoiceRecorder.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMediaDevices>
#include <QPermissions>
#include <QStandardPaths>
#include <QUuid>

VoiceRecorder::VoiceRecorder(QObject *parent) : QObject(parent), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start(); m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, [this] { emit changed(); if (m_duration >= 20 * 60 * 1000) stop(); });
}
VoiceRecorder::~VoiceRecorder()
{
    if (m_source) { m_source->disconnect(this); m_source->stop(); }
    QMetaObject::invokeMethod(m_worker, [this] { m_writer.reset(); }, Qt::BlockingQueuedConnection);
    m_thread.quit(); m_thread.wait();
    if (!m_file.isEmpty()) QFile::remove(m_file.toLocalFile());
}
void VoiceRecorder::start()
{
    if (recording() || m_requesting || m_finalizing) return;
    discard(); m_error.clear();
    const auto generation = m_generation;
    QMicrophonePermission permission;
    const auto status = QCoreApplication::instance()->checkPermission(permission);
    if (status == Qt::PermissionStatus::Granted) { begin(); return; }
    if (status == Qt::PermissionStatus::Denied) { m_error = "Разрешите доступ к микрофону в настройках macOS."; emit changed(); return; }
    m_requesting = true; emit changed();
    QCoreApplication::instance()->requestPermission(permission, this, [this, generation](const QPermission &result) {
        if (generation != m_generation) return;
        m_requesting = false;
        if (result.status() != Qt::PermissionStatus::Granted) { m_error = "Без доступа к микрофону записать сообщение не получится."; emit changed(); return; }
        begin();
    });
}
void VoiceRecorder::begin()
{
    const auto device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) { m_error = "Микрофон не найден."; emit changed(); return; }
    QAudioFormat format; format.setSampleRate(48000); format.setChannelCount(1); format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) { m_error = "Микрофон не поддерживает запись 48 кГц моно."; emit changed(); return; }
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recordings";
    if (!QDir().mkpath(root)) { m_error = "Не удалось сохранить запись."; emit changed(); return; }
    QFile::setPermissions(root, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    m_file = QUrl::fromLocalFile(root + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".ogg");
    m_finalizing = true; emit changed();
    const auto generation = m_generation; const auto path = m_file.toLocalFile();
    QMetaObject::invokeMethod(m_worker, [this, generation, path, device, format] {
        m_writer = std::make_unique<OggOpusWriter>(); const bool opened = m_writer->open(path); const auto error = m_writer->error();
        QMetaObject::invokeMethod(this, [this, generation, opened, error, device, format] {
            if (generation != m_generation) return;
            m_finalizing = false;
            if (!opened) { m_error = error; emit changed(); return; }
            m_source = std::make_unique<QAudioSource>(device, format); m_duration = m_pcmBytes = 0; m_recording = true;
            connect(m_source.get(), &QAudioSource::stateChanged, this, [this](QtAudio::State state) {
                if (state == QtAudio::StoppedState && m_source->error() != QtAudio::NoError && m_recording) { m_error = "Не удалось получить звук с микрофона."; stop(); }
            });
            m_input = m_source->start();
            if (!m_recording) return;
            if (!m_input) { m_error = "Не удалось открыть микрофон."; stop(); return; }
            connect(m_input, &QIODevice::readyRead, this, &VoiceRecorder::consume);
            m_tick.start(); emit changed();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
void VoiceRecorder::consume()
{
    if (!m_input || !m_recording) return;
    const auto pcm = m_input->readAll(); if (pcm.isEmpty()) return;
    m_pcmBytes += pcm.size(); m_duration = m_pcmBytes * 1000 / 96000;
    const auto generation = m_generation;
    QMetaObject::invokeMethod(m_worker, [this, pcm, generation] {
        if (m_writer && !m_writer->append(pcm)) {
            const auto error = m_writer->error();
            QMetaObject::invokeMethod(this, [this, generation, error] { if (generation == m_generation && m_recording) { m_error = error; stop(); } }, Qt::QueuedConnection);
        }
    }, Qt::QueuedConnection);
}
void VoiceRecorder::stop()
{
    if (!m_recording) return;
    consume(); m_recording = false; m_tick.stop(); m_finalizing = true;
    m_input = nullptr; m_source->disconnect(this); m_source->stop();
    const auto generation = m_generation; const auto path = m_file.toLocalFile(); const auto previousError = m_error;
    QMetaObject::invokeMethod(m_worker, [this, generation, path, previousError] {
        const bool success = m_writer && previousError.isEmpty() && m_writer->finish(); const auto error = m_writer ? m_writer->error() : QString();
        m_writer.reset(); if (!success) QFile::remove(path);
        QMetaObject::invokeMethod(this, [this, generation, success, error] { if (generation != m_generation) return; m_finalizing = false; m_ready = success; if (m_error.isEmpty()) m_error = error; emit changed(); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
    emit changed();
}
void VoiceRecorder::discard()
{
    ++m_generation; m_tick.stop(); m_recording = m_requesting = m_ready = m_finalizing = false;
    if (m_source) { m_source->disconnect(this); m_source->stop(); m_source.reset(); } m_input = nullptr;
    const auto path = m_file.toLocalFile(); m_file = QUrl(); m_duration = m_pcmBytes = 0;
    QMetaObject::invokeMethod(m_worker, [this, path] { m_writer.reset(); if (!path.isEmpty()) QFile::remove(path); }, Qt::QueuedConnection);
    emit changed();
}
QUrl VoiceRecorder::release()
{
    if (!m_ready) return {};
    const auto result = m_file; m_file = QUrl(); m_ready = false; emit changed(); return result;
}
