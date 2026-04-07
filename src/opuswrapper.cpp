#include "opuswrapper.h"
#include <QDebug>
#include <QRegularExpression>
#include <QFile>

OpusWrapper::OpusWrapper(QObject* parent)
    : CodecWrapper(parent)
{
    m_execPath = findExecutable("opusenc");

    if (m_execPath.isEmpty()) {
        qWarning() << "Opus encoder not found in PATH";
    }
}

bool OpusWrapper::isAvailable() const
{
    return !m_execPath.isEmpty();
}

QString OpusWrapper::version() const
{
    if (!isAvailable()) {
        return "Not found";
    }

    QProcess process;
    process.start(m_execPath, {"--version"});
    process.waitForFinished();

    QString output = process.readAllStandardError();
    QRegularExpression versionRegex("opusenc.*opus-tools ([0-9.]+)");
    QRegularExpressionMatch match = versionRegex.match(output);

    if (match.hasMatch()) {
        return match.captured(1);
    }

    return "Unknown";
}

QStringList OpusWrapper::buildArguments(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options)
{
    QStringList args;

    // Bitrate (in kbps)
    args << "--bitrate" << QString::number(options.bitrate);

    // VBR mode (default, more efficient)
    args << "--vbr";

    // Quality (complexity, 0-10, default 10)
    args << "--comp" << "10";

    // Sample rate (Opus internally uses 48kHz but can accept different inputs)
    // opusenc handles resampling automatically

    // Downmix to mono if requested
    if (options.channels == 1) {
        args << "--downmix-mono";
    }

    // Write tags as Opus comments
    for (auto it = options.tags.constBegin(); it != options.tags.constEnd(); ++it) {
        args << "--comment" << QString("%1=%2").arg(it.key(), it.value());
    }

    // Embed cover art
    if (!options.coverArtPath.isEmpty()) {
        args << "--picture" << options.coverArtPath;
    }

    // Don't use --quiet as it suppresses progress output
    // opusenc shows progress by default

    // Input and output
    args << inputPath;
    args << outputPath;

    return args;
}

bool OpusWrapper::convert(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options)
{
    if (!isAvailable()) {
        qWarning() << "Opus encoder not available";
        return false;
    }

    QStringList args = buildArguments(inputPath, outputPath, options);

    QProcess process;
    process.start(m_execPath, args);

    if (!process.waitForStarted()) {
        qWarning() << "Failed to start Opus encoder";
        return false;
    }

    if (!process.waitForFinished(-1)) {
        qWarning() << "Opus encoding process error";
        return false;
    }

    if (process.exitCode() != 0) {
        QString error = process.readAllStandardError();
        qWarning() << "Opus encoding failed:" << error;
        return false;
    }

    return true;
}

void OpusWrapper::convertAsync(
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

void OpusWrapper::cancel()
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

void OpusWrapper::parseProgress(const QString& output)
{
    // opusenc outputs progress like: "[ 45%]"
    static QRegularExpression progressRegex("\\[\\s*(\\d+)%\\]");

    QRegularExpressionMatch match = progressRegex.match(output);
    if (match.hasMatch()) {
        int percent = match.captured(1).toInt();
        emit progressChanged(qBound(0, percent, 100));
    }
}
