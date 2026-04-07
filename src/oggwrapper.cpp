#include "oggwrapper.h"
#include <QDebug>
#include <QRegularExpression>
#include <QFile>

OggWrapper::OggWrapper(QObject* parent)
    : CodecWrapper(parent)
{
    m_execPath = findExecutable("oggenc");

    if (m_execPath.isEmpty()) {
        qWarning() << "Ogg Vorbis encoder not found in PATH";
    }
}

bool OggWrapper::isAvailable() const
{
    return !m_execPath.isEmpty();
}

QString OggWrapper::version() const
{
    if (!isAvailable()) {
        return "Not found";
    }

    QProcess process;
    process.start(m_execPath, {"--version"});
    process.waitForFinished();

    QString output = process.readAllStandardOutput();
    QRegularExpression versionRegex("oggenc.*vorbis-tools ([0-9.]+)");
    QRegularExpressionMatch match = versionRegex.match(output);

    if (match.hasMatch()) {
        return match.captured(1);
    }

    return "Unknown";
}

QStringList OggWrapper::buildArguments(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options)
{
    QStringList args;

    // Quality or bitrate
    if (options.quality >= 0) {
        // VBR quality mode (-1 to 10, where 10 is highest)
        args << "-q" << QString::number(options.quality);
    } else {
        // ABR (average bitrate) mode
        args << "-b" << QString::number(options.bitrate);
    }

    // Sample rate
    if (options.sampleRate > 0) {
        args << "--resample" << QString::number(options.sampleRate);
    }

    // Channels (downmix to mono)
    if (options.channels == 1) {
        args << "--downmix";
    }

    // Write tags as Vorbis comments
    for (auto it = options.tags.constBegin(); it != options.tags.constEnd(); ++it) {
        args << "-c" << QString("%1=%2").arg(it.key(), it.value());
    }

    // Don't use --quiet as it suppresses progress output
    // oggenc shows progress by default

    // Output file
    args << "-o" << outputPath;

    // Input file
    args << inputPath;

    return args;
}

bool OggWrapper::convert(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options)
{
    if (!isAvailable()) {
        qWarning() << "Ogg Vorbis encoder not available";
        return false;
    }

    QStringList args = buildArguments(inputPath, outputPath, options);

    QProcess process;
    process.start(m_execPath, args);

    if (!process.waitForStarted()) {
        qWarning() << "Failed to start Ogg Vorbis encoder";
        return false;
    }

    if (!process.waitForFinished(-1)) {
        qWarning() << "Ogg Vorbis encoding process error";
        return false;
    }

    if (process.exitCode() != 0) {
        QString error = process.readAllStandardError();
        qWarning() << "Ogg Vorbis encoding failed:" << error;
        return false;
    }

    return true;
}

void OggWrapper::convertAsync(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options)
{
    if (m_process) {
        qWarning() << "Conversion already in progress";
        return;
    }

    m_outputPath = outputPath;
    m_process = new QProcess(this);

    // Connect progress monitoring
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        QString output = m_process->readAllStandardError();
        parseProgress(output);
    });

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        QString output = m_process->readAllStandardOutput();
        parseProgress(output);
    });

    // Connect completion
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        bool success = (exitCode == 0 && status == QProcess::NormalExit);
        QString error;

        if (!success) {
            error = m_process->readAllStandardError();
        }

        m_process->deleteLater();
        m_process = nullptr;
        m_outputPath.clear();

        emit conversionFinished(success, error);
    });

    // Start conversion
    QStringList args = buildArguments(inputPath, outputPath, options);
    m_process->start(m_execPath, args);
}

void OggWrapper::cancel()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        // Disconnect signals to avoid spurious callbacks
        m_process->disconnect();

        // Try graceful termination first
        m_process->terminate();

        // Wait up to 1 second for graceful shutdown
        if (!m_process->waitForFinished(1000)) {
            // Force kill if termination didn't work
            m_process->kill();
            m_process->waitForFinished(100);
        }

        m_process->deleteLater();
        m_process = nullptr;
    }

    // Delete partially converted file if it exists
    if (!m_outputPath.isEmpty()) {
        QFile::remove(m_outputPath);
        m_outputPath.clear();
    }
}

void OggWrapper::parseProgress(const QString& output)
{
    // oggenc outputs progress like: "\t[ 45.0%]"
    static QRegularExpression progressRegex("\\[\\s*([0-9.]+)%\\]");

    QRegularExpressionMatch match = progressRegex.match(output);
    if (match.hasMatch()) {
        int percent = static_cast<int>(match.captured(1).toDouble());
        emit progressChanged(qBound(0, percent, 100));
    }
}
