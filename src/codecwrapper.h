#pragma once

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

struct ConversionOptions {
    QString format;        // "mp3", "flac", "opus", "ogg"
    int bitrate{320};     // kbps (for lossy formats)
    int quality{-1};      // VBR quality (-1 = use bitrate)
    int sampleRate{0};    // 0 = preserve original
    int channels{0};      // 0 = preserve original
    int compressionLevel{8}; // For FLAC (0-8)
    QMap<QString, QString> tags; // Metadata tags read from source file
    QString coverArtPath;        // Path to extracted cover art temp file (empty if none)
};

class CodecWrapper : public QObject
{
    Q_OBJECT

public:
    explicit CodecWrapper(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~CodecWrapper() = default;

    // Check if codec tool is available
    virtual bool isAvailable() const = 0;
    virtual QString version() const = 0;
    virtual QString executableName() const = 0;

    // Conversion
    virtual bool convert(
        const QString& inputPath,
        const QString& outputPath,
        const ConversionOptions& options
    ) = 0;

    // Async conversion with progress
    virtual void convertAsync(
        const QString& inputPath,
        const QString& outputPath,
        const ConversionOptions& options
    ) = 0;

    virtual void cancel() = 0;

signals:
    void progressChanged(int percent);
    void conversionFinished(bool success, const QString& error);

protected:
    QString findExecutable(const QString& name) const;
    QProcess* m_process{nullptr};
    QString m_outputPath; // Track output path for cancellation
};
