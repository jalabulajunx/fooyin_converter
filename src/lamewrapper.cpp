#include "lamewrapper.h"
#include <QDebug>
#include <QRegularExpression>
#include <QFile>

LameWrapper::LameWrapper(QObject* parent)
    : CodecWrapper(parent)
{
    m_execPath = findExecutable("lame");

    if (m_execPath.isEmpty()) {
        qWarning() << "LAME MP3 encoder not found in PATH";
    }
}

bool LameWrapper::isAvailable() const
{
    return !m_execPath.isEmpty();
}

QString LameWrapper::version() const
{
    if (!isAvailable()) {
        return "Not found";
    }

    QProcess process;
    process.start(m_execPath, {"--version"});
    process.waitForFinished();

    QString output = process.readAllStandardError(); // LAME outputs version to stderr
    QRegularExpression versionRegex("LAME.*version ([0-9.]+)");
    QRegularExpressionMatch match = versionRegex.match(output);

    if (match.hasMatch()) {
        return match.captured(1);
    }

    return "Unknown";
}

QStringList LameWrapper::buildArguments(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options)
{
    QStringList args;

    // Quality/Bitrate
    if (options.quality >= 0) {
        // VBR mode (V0-V9, where V0 is highest quality)
        args << "-V" << QString::number(options.quality);
    } else {
        // CBR mode
        args << "-b" << QString::number(options.bitrate);
    }

    // Sample rate
    if (options.sampleRate > 0) {
        args << "--resample" << QString::number(options.sampleRate / 1000.0);
    }

    // Channels
    if (options.channels == 1) {
        args << "-m" << "m"; // mono
    } else if (options.channels == 2) {
        args << "-m" << "s"; // stereo
    }

    // High quality algorithm
    args << "-q" << "0";

    // ID3v2 tags
    args << "--id3v2-only";

    // Map standard Vorbis comment keys to LAME's named tag flags
    static const QMap<QString, QString> namedFlags = {
        {QStringLiteral("TITLE"),   QStringLiteral("--tt")},
        {QStringLiteral("ARTIST"),  QStringLiteral("--ta")},
        {QStringLiteral("ALBUM"),   QStringLiteral("--tl")},
        {QStringLiteral("DATE"),    QStringLiteral("--ty")},
        {QStringLiteral("YEAR"),    QStringLiteral("--ty")},
        {QStringLiteral("TRACK"),   QStringLiteral("--tn")},
        {QStringLiteral("GENRE"),   QStringLiteral("--tg")},
        {QStringLiteral("COMMENT"), QStringLiteral("--tc")},
    };
    // Map remaining standard tags to ID3v2 frame IDs via --tv
    static const QMap<QString, QString> frameFlags = {
        {QStringLiteral("ALBUMARTIST"),  QStringLiteral("TPE2")},
        {QStringLiteral("ALBUM_ARTIST"), QStringLiteral("TPE2")},
        {QStringLiteral("DISCNUMBER"),   QStringLiteral("TPOS")},
        {QStringLiteral("DISC"),         QStringLiteral("TPOS")},
        {QStringLiteral("COMPOSER"),     QStringLiteral("TCOM")},
        {QStringLiteral("COPYRIGHT"),    QStringLiteral("TCOP")},
    };
    for (auto it = options.tags.constBegin(); it != options.tags.constEnd(); ++it) {
        const QString key = it.key().toUpper();
        if (namedFlags.contains(key)) {
            args << namedFlags[key] << it.value();
        } else if (frameFlags.contains(key)) {
            args << QStringLiteral("--tv") << QString("%1=%2").arg(frameFlags[key], it.value());
        }
    }

    // Embed cover art
    if (!options.coverArtPath.isEmpty()) {
        args << "--ti" << options.coverArtPath;
    }

    // Display progress (don't use --silent as it suppresses progress)
    // --nohist suppresses the bitrate histogram
    args << "--nohist";

    // Input and output
    args << inputPath;
    args << outputPath;

    return args;
}

bool LameWrapper::convert(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options)
{
    if (!isAvailable()) {
        qWarning() << "LAME encoder not available";
        return false;
    }

    QStringList args = buildArguments(inputPath, outputPath, options);

    QProcess process;
    process.start(m_execPath, args);

    if (!process.waitForStarted()) {
        qWarning() << "Failed to start LAME encoder";
        return false;
    }

    if (!process.waitForFinished(-1)) {
        qWarning() << "LAME encoding process error";
        return false;
    }

    if (process.exitCode() != 0) {
        QString error = process.readAllStandardError();
        qWarning() << "LAME encoding failed:" << error;
        return false;
    }

    return true;
}

void LameWrapper::convertAsync(
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

    // Connect progress monitoring (LAME outputs to stderr)
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

void LameWrapper::cancel()
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

void LameWrapper::parseProgress(const QString& output)
{
    // LAME outputs progress like: "  ( 45%)"
    static QRegularExpression progressRegex("\\((\\s*\\d+)%\\)");

    QRegularExpressionMatch match = progressRegex.match(output);
    if (match.hasMatch()) {
        int percent = match.captured(1).trimmed().toInt();
        emit progressChanged(qBound(0, percent, 100));
    }
}
