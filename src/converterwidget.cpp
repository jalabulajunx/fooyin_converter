#include "converterwidget.h"
#include "conversionmanager.h"
#include "convertersettings.h"

#include <utils/settings/settingsmanager.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

static QMap<QString, QString> readTagsFromFile(const QString& inputPath)
{
    QMap<QString, QString> tags;

    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty()) {
        qWarning() << "ffprobe not found — tags will not be transferred";
        return tags;
    }

    QProcess process;
    process.start(ffprobe, {
        QStringLiteral("-v"),            QStringLiteral("quiet"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_format"),
        inputPath
    });
    process.waitForFinished(10000);

    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
    if (doc.isNull())
        return tags;

    const QJsonObject formatTags = doc.object()
        .value(QStringLiteral("format")).toObject()
        .value(QStringLiteral("tags")).toObject();

    // ffprobe uses some non-standard key names; normalize to Vorbis comment conventions
    static const QMap<QString, QString> keyNorm = {
        {QStringLiteral("album_artist"), QStringLiteral("ALBUMARTIST")},
        {QStringLiteral("track"),        QStringLiteral("TRACKNUMBER")},
        {QStringLiteral("disc"),         QStringLiteral("DISCNUMBER")},
    };

    for (auto it = formatTags.constBegin(); it != formatTags.constEnd(); ++it) {
        const QString key = it.key().toLower();
        const QString normalized = keyNorm.value(key, it.key().toUpper());
        tags.insert(normalized, it.value().toString());
    }

    return tags;
}

static QString extractCoverArt(const QString& inputPath)
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) {
        qWarning() << "ffmpeg not found — cover art will not be transferred";
        return {};
    }

    const QString tempPath = QDir::tempPath() + QStringLiteral("/fooyin_converter_cover.jpg");

    QProcess process;
    process.start(ffmpeg, {
        QStringLiteral("-v"),      QStringLiteral("quiet"),
        QStringLiteral("-i"),      inputPath,
        QStringLiteral("-an"),
        QStringLiteral("-vframes"), QStringLiteral("1"),
        QStringLiteral("-y"),
        tempPath
    });
    process.waitForFinished(10000);

    const QFileInfo info(tempPath);
    if (process.exitCode() == 0 && info.exists() && info.size() > 0)
        return tempPath;

    return {};
}

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QSpinBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QCloseEvent>
#include <QKeyEvent>

ConverterWidget::ConverterWidget(ConversionManager* manager, Fooyin::SettingsManager* settings, QWidget* parent)
    : FyWidget(parent)
    , m_manager(manager)
    , m_settings(settings)
{
    setupUI();

    // Apply default codec if settings available
    if (m_settings) {
        applyDefaultCodec();
    }

    // Connect manager signals
    connect(m_manager, &ConversionManager::progressChanged,
            this, &ConverterWidget::onProgress);
    connect(m_manager, &ConversionManager::conversionFinished,
            this, &ConverterWidget::onFinished);
    connect(m_manager, &ConversionManager::conversionStarted,
            this, &ConverterWidget::onStarted);
}

void ConverterWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // ===== Input Section =====
    auto* inputGroup = new QGroupBox("Source");
    auto* inputLayout = new QHBoxLayout(inputGroup);

    m_inputEdit = new QLineEdit();
    m_inputEdit->setPlaceholderText("Select audio file to convert...");
    auto* inputButton = new QPushButton("Browse...");
    inputButton->setMaximumWidth(100);

    inputLayout->addWidget(m_inputEdit);
    inputLayout->addWidget(inputButton);

    connect(inputButton, &QPushButton::clicked, this, &ConverterWidget::browseInput);
    connect(m_inputEdit, &QLineEdit::textChanged, this, [this]() {
        updateOutputPath();
    });

    // ===== Output Section =====
    auto* outputGroup = new QGroupBox("Destination");
    auto* outputLayout = new QHBoxLayout(outputGroup);

    m_outputEdit = new QLineEdit();
    m_outputEdit->setPlaceholderText("Output file or folder path...");
    auto* outputButton = new QPushButton("Browse...");
    outputButton->setMaximumWidth(100);

    outputLayout->addWidget(m_outputEdit);
    outputLayout->addWidget(outputButton);

    connect(outputButton, &QPushButton::clicked, this, &ConverterWidget::browseOutput);

    // ===== Format Settings =====
    auto* formatGroup = new QGroupBox("Format Settings");
    auto* formatLayout = new QFormLayout(formatGroup);

    // Format selector
    m_formatCombo = new QComboBox();
    QStringList availableCodecs = m_manager->availableCodecs();

    // Add formats with availability info
    if (availableCodecs.contains("mp3")) {
        m_formatCombo->addItem("MP3", "mp3");
    }
    if (availableCodecs.contains("flac")) {
        m_formatCombo->addItem("FLAC (Lossless)", "flac");
    }
    if (availableCodecs.contains("opus")) {
        m_formatCombo->addItem("Opus", "opus");
    }
    if (availableCodecs.contains("ogg")) {
        m_formatCombo->addItem("Ogg Vorbis", "ogg");
    }

    if (m_formatCombo->count() == 0) {
        m_formatCombo->addItem("No codecs available!", "");
    }

    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConverterWidget::onFormatChanged);

    formatLayout->addRow("Output Format:", m_formatCombo);

    // Quality/Bitrate selector
    m_qualityCombo = new QComboBox();
    formatLayout->addRow("Quality:", m_qualityCombo);

    // Sample rate
    m_sampleRateSpin = new QSpinBox();
    m_sampleRateSpin->setMinimum(0);
    m_sampleRateSpin->setMaximum(192000);
    m_sampleRateSpin->setSingleStep(1000);
    m_sampleRateSpin->setValue(0);
    m_sampleRateSpin->setSpecialValueText("Original");
    m_sampleRateSpin->setSuffix(" Hz");
    formatLayout->addRow("Sample Rate:", m_sampleRateSpin);

    // Channels
    m_channelsCombo = new QComboBox();
    m_channelsCombo->addItem("Original", 0);
    m_channelsCombo->addItem("Mono", 1);
    m_channelsCombo->addItem("Stereo", 2);
    formatLayout->addRow("Channels:", m_channelsCombo);

    // ===== Progress Section =====
    auto* progressGroup = new QGroupBox("Progress");
    auto* progressLayout = new QVBoxLayout(progressGroup);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);

    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setWordWrap(true);

    progressLayout->addWidget(m_progressBar);
    progressLayout->addWidget(m_statusLabel);

    // ===== Control Buttons =====
    auto* buttonLayout = new QHBoxLayout();

    m_convertButton = new QPushButton("Convert");
    m_convertButton->setEnabled(false);
    m_convertButton->setMinimumHeight(35);

    m_cancelButton = new QPushButton("Cancel");
    m_cancelButton->setEnabled(false);
    m_cancelButton->setMinimumHeight(35);

    buttonLayout->addWidget(m_convertButton);
    buttonLayout->addWidget(m_cancelButton);

    connect(m_convertButton, &QPushButton::clicked, this, &ConverterWidget::startConversion);
    connect(m_cancelButton, &QPushButton::clicked, this, &ConverterWidget::cancelConversion);

    // ===== Codec Info =====
    m_codecInfoLabel = new QLabel();
    m_codecInfoLabel->setWordWrap(true);
    m_codecInfoLabel->setStyleSheet("QLabel { color: gray; font-size: 10pt; }");
    updateCodecInfo();

    // ===== Assemble Layout =====
    mainLayout->addWidget(inputGroup);
    mainLayout->addWidget(outputGroup);
    mainLayout->addWidget(formatGroup);
    mainLayout->addWidget(progressGroup);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(m_codecInfoLabel);
    mainLayout->addStretch();

    // Initialize quality options
    updateQualityOptions();
}

void ConverterWidget::updateCodecInfo()
{
    QStringList available = m_manager->availableCodecs();
    QString info = "Available codecs: ";

    if (available.isEmpty()) {
        info += "None (please install flac, lame, opusenc, oggenc)";
    } else {
        QStringList codecInfo;
        for (const QString& codec : available) {
            codecInfo << QString("%1 (%2)").arg(codec, m_manager->codecVersion(codec));
        }
        info += codecInfo.join(", ");
    }

    m_codecInfoLabel->setText(info);
}

void ConverterWidget::browseInput()
{
    QString filter = "Audio Files (*.mp3 *.flac *.wav *.ogg *.opus *.m4a *.aac *.wv *.ape);;All Files (*)";
    QString path = QFileDialog::getOpenFileName(this, "Select Audio File", QString(), filter);

    if (!path.isEmpty()) {
        m_inputEdit->setText(path);
    }
}

void ConverterWidget::browseOutput()
{
    // Check if in batch mode
    if (!m_trackQueue.isEmpty()) {
        // Batch mode: select output folder
        QString defaultPath = m_outputEdit->text();
        if (defaultPath == QStringLiteral("Same as source folder")) {
            defaultPath = QString();
        }

        QString path = QFileDialog::getExistingDirectory(this, "Select Output Folder", defaultPath);

        if (!path.isEmpty()) {
            m_outputEdit->setText(path);
        }
    } else {
        // Single file mode: select output file
        QString ext = getOutputExtension();
        QString filter = QString("%1 Files (*.%2);;All Files (*)").arg(ext.toUpper(), ext);
        QString defaultPath = m_outputEdit->text();

        if (defaultPath.isEmpty() && !m_inputEdit->text().isEmpty()) {
            updateOutputPath();
            defaultPath = m_outputEdit->text();
        }

        QString path = QFileDialog::getSaveFileName(this, "Save Output File", defaultPath, filter);

        if (!path.isEmpty()) {
            m_outputEdit->setText(path);
        }
    }
}

QString ConverterWidget::getOutputExtension() const
{
    return m_formatCombo->currentData().toString();
}

void ConverterWidget::updateOutputPath()
{
    // Skip output path updates when in batch mode
    if (!m_trackQueue.isEmpty()) {
        m_convertButton->setEnabled(!m_trackQueue.isEmpty() && !m_outputEdit->text().isEmpty());
        return;
    }

    QString input = m_inputEdit->text();
    if (input.isEmpty()) {
        m_convertButton->setEnabled(false);
        return;
    }

    // Always update the extension based on current format
    QFileInfo inputInfo(input);
    QString currentOutput = m_outputEdit->text();

    if (currentOutput.isEmpty()) {
        // No output set yet, generate it
        QString outputPath = inputInfo.absolutePath() + "/"
                           + inputInfo.completeBaseName()
                           + "." + getOutputExtension();
        m_outputEdit->setText(outputPath);
    } else {
        // Update existing output path with new extension
        QFileInfo outputInfo(currentOutput);
        QString outputPath = outputInfo.absolutePath() + "/"
                           + outputInfo.completeBaseName()
                           + "." + getOutputExtension();
        m_outputEdit->setText(outputPath);
    }

    m_convertButton->setEnabled(!input.isEmpty() && !m_outputEdit->text().isEmpty());
}

void ConverterWidget::onFormatChanged(int index)
{
    Q_UNUSED(index);
    updateQualityOptions();
    updateOutputPath();
}

void ConverterWidget::updateQualityOptions()
{
    m_qualityCombo->clear();

    QString format = getOutputExtension();

    if (format == "mp3") {
        m_qualityCombo->addItem("320 kbps (Highest)", 320);
        m_qualityCombo->addItem("256 kbps", 256);
        m_qualityCombo->addItem("192 kbps (Good)", 192);
        m_qualityCombo->addItem("128 kbps (Medium)", 128);
        m_qualityCombo->addItem("96 kbps (Low)", 96);
        m_qualityCombo->addItem("V0 (VBR ~245 kbps)", -100);  // Negative = VBR quality 0
        m_qualityCombo->addItem("V2 (VBR ~190 kbps)", -102);  // Negative = VBR quality 2
        m_qualityCombo->setCurrentIndex(0);  // Default: 320 kbps
    }
    else if (format == "flac") {
        m_qualityCombo->addItem("Compression Level 8 (Best)", 8);
        m_qualityCombo->addItem("Compression Level 5 (Default)", 5);
        m_qualityCombo->addItem("Compression Level 0 (Fastest)", 0);
        m_qualityCombo->setCurrentIndex(0);  // Default: Level 8
    }
    else if (format == "opus") {
        m_qualityCombo->addItem("256 kbps (Highest)", 256);
        m_qualityCombo->addItem("192 kbps", 192);
        m_qualityCombo->addItem("128 kbps (Good)", 128);
        m_qualityCombo->addItem("96 kbps (Medium)", 96);
        m_qualityCombo->addItem("64 kbps (Low)", 64);
        m_qualityCombo->setCurrentIndex(2);  // Default: 128 kbps
    }
    else if (format == "ogg") {
        m_qualityCombo->addItem("Quality 10 (Highest ~500 kbps)", 10);
        m_qualityCombo->addItem("Quality 8 (~256 kbps)", 8);
        m_qualityCombo->addItem("Quality 6 (~192 kbps)", 6);
        m_qualityCombo->addItem("Quality 4 (~128 kbps)", 4);
        m_qualityCombo->addItem("Quality 2 (~96 kbps)", 2);
        m_qualityCombo->setCurrentIndex(1);  // Default: Quality 8
    }
}

bool ConverterWidget::validateInput()
{
    // For batch mode, only validate output folder and codec
    if (!m_trackQueue.isEmpty()) {
        QString output = m_outputEdit->text();
        
        if (output.isEmpty()) {
            QMessageBox::warning(this, "Invalid Input", "Please select output folder.");
            return false;
        }

        // If output is not "Same as source folder", validate it's a valid directory
        if (output != QStringLiteral("Same as source folder") && !QDir(output).exists()) {
            QMessageBox::warning(this, "Invalid Output", "Selected output folder does not exist.");
            return false;
        }

        QString format = getOutputExtension();
        if (!m_manager->isCodecAvailable(format)) {
            QMessageBox::critical(this, "Codec Not Available",
                QString("The codec for %1 format is not available.\n"
                        "Please install the required encoder.").arg(format.toUpper()));
            return false;
        }

        return true;
    }

    // Single file mode validation
    QString input = m_inputEdit->text();
    QString output = m_outputEdit->text();

    if (input.isEmpty() || output.isEmpty()) {
        QMessageBox::warning(this, "Invalid Input", "Please select input and output files.");
        return false;
    }

    if (!QFile::exists(input)) {
        QMessageBox::warning(this, "File Not Found", "Input file does not exist.");
        return false;
    }

    if (input == output) {
        QMessageBox::warning(this, "Invalid Output", "Input and output files cannot be the same.");
        return false;
    }

    QString format = getOutputExtension();
    if (!m_manager->isCodecAvailable(format)) {
        QMessageBox::critical(this, "Codec Not Available",
            QString("The codec for %1 format is not available.\n"
                    "Please install the required encoder.").arg(format.toUpper()));
        return false;
    }

    return true;
}

void ConverterWidget::startConversion()
{
    // Check if batch mode
    if (!m_trackQueue.isEmpty()) {
        // Start batch conversion
        m_currentTrackIndex = -1;
        processNextTrack();
        return;
    }

    // Single file conversion
    if (!validateInput()) {
        return;
    }

    // Build conversion options
    ConversionOptions options;
    options.format = getOutputExtension();

    int qualityValue = m_qualityCombo->currentData().toInt();

    if (options.format == "mp3") {
        if (qualityValue < 0) {
            // VBR mode: negative values encode VBR quality
            options.quality = -qualityValue - 100;  // -100 -> 0, -102 -> 2
            options.bitrate = 0;
        } else {
            // CBR mode
            options.bitrate = qualityValue;
            options.quality = -1;
        }
    }
    else if (options.format == "flac") {
        options.compressionLevel = qualityValue;
        options.bitrate = 0;
        options.quality = -1;
    }
    else if (options.format == "opus") {
        options.bitrate = qualityValue;
        options.quality = -1;
    }
    else if (options.format == "ogg") {
        options.quality = qualityValue;
        options.bitrate = 0;
    }

    options.sampleRate = m_sampleRateSpin->value();
    options.channels = m_channelsCombo->currentData().toInt();

    // Start conversion
    QString input = m_inputEdit->text();
    QString output = m_outputEdit->text();

    options.tags = readTagsFromFile(input);
    m_tempCoverArtPath = extractCoverArt(input);
    options.coverArtPath = m_tempCoverArtPath;
    m_manager->convertAsync(input, output, options);
}

void ConverterWidget::cancelConversion()
{
    m_manager->cancel();
    m_isConverting = false;
    m_statusLabel->setText("Conversion canceled");
    m_progressBar->setValue(0);
    m_convertButton->setEnabled(true);
    m_cancelButton->setEnabled(false);

    // Clear batch queue if in batch mode
    if (!m_trackQueue.isEmpty()) {
        m_trackQueue.clear();
        m_currentTrackIndex = -1;
        m_totalTracks = 0;
        
        // Restore UI to single file mode
        m_inputEdit->clear();
        m_inputEdit->setEnabled(true);
        m_outputEdit->clear();
        m_outputEdit->setEnabled(true);
    }
}

void ConverterWidget::onStarted()
{
    m_isConverting = true;
    m_convertButton->setEnabled(false);
    m_cancelButton->setEnabled(true);
    m_statusLabel->setText("Converting...");
    m_progressBar->setValue(0);
}

void ConverterWidget::onProgress(int percent)
{
    m_progressBar->setValue(percent);

    // In batch mode, show the filename with progress
    if (!m_trackQueue.isEmpty() && m_currentTrackIndex >= 0) {
        m_statusLabel->setText(QString("Converting %1 of %2: %3 (%4%)")
            .arg(m_currentTrackIndex + 1)
            .arg(m_totalTracks)
            .arg(m_currentFilename)
            .arg(percent));
    } else {
        // Single file mode
        m_statusLabel->setText(QString("Converting... %1%").arg(percent));
    }
}

void ConverterWidget::onFinished(bool success, const QString& error)
{
    // Clean up cover art temp file from this conversion
    if (!m_tempCoverArtPath.isEmpty()) {
        QFile::remove(m_tempCoverArtPath);
        m_tempCoverArtPath.clear();
    }

    // Check if batch mode
    if (!m_trackQueue.isEmpty() && m_currentTrackIndex >= 0) {
        if (!success) {
            // Show error but continue with next
            qWarning() << "Track conversion failed:" << error;
        }

        // Process next track
        processNextTrack();
        return;
    }

    // Single file mode - conversion complete
    m_isConverting = false;
    m_convertButton->setEnabled(true);
    m_cancelButton->setEnabled(false);

    if (success) {
        m_statusLabel->setText("Conversion completed successfully!");
        m_progressBar->setValue(100);
        QMessageBox::information(this, "Success",
            QString("File converted successfully!\n\nOutput: %1").arg(m_outputEdit->text()));
    } else {
        m_statusLabel->setText("Conversion failed!");
        m_progressBar->setValue(0);
        QMessageBox::critical(this, "Conversion Error",
            QString("Conversion failed:\n\n%1").arg(error));
    }
}

void ConverterWidget::loadTrack(const QString& filepath)
{
    // Clear batch queue
    m_trackQueue.clear();
    m_currentTrackIndex = -1;
    m_totalTracks = 0;

    // Set the input file and re-enable editing
    m_inputEdit->setText(filepath);
    m_inputEdit->setEnabled(true);

    // Auto-generate output path in same directory with new extension and re-enable editing
    QFileInfo info(filepath);
    QString outputPath = info.absolutePath() + "/"
                       + info.completeBaseName()
                       + "." + getOutputExtension();
    m_outputEdit->setText(outputPath);
    m_outputEdit->setEnabled(true);

    // Reset progress and status
    m_progressBar->setValue(0);
    m_statusLabel->setText("Ready");

    // Enable convert button
    m_convertButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
}

void ConverterWidget::loadTracks(const QStringList& filepaths)
{
    if (filepaths.isEmpty()) {
        return;
    }

    // Store tracks for batch processing
    m_trackQueue = filepaths;
    m_currentTrackIndex = -1;
    m_totalTracks = filepaths.size();

    // Set input to show batch info and disable editing
    m_inputEdit->setText(QString("%1 files selected").arg(m_totalTracks));
    m_inputEdit->setEnabled(false);

    // Show "Same as source folder" but keep it enabled for folder selection
    m_outputEdit->setText(QStringLiteral("Same as source folder"));
    m_outputEdit->setEnabled(true);

    // Reset progress and status
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString("Ready to convert %1 files").arg(m_totalTracks));

    // Enable convert button
    m_convertButton->setEnabled(true);
    m_cancelButton->setEnabled(false);
}

void ConverterWidget::processNextTrack()
{
    m_currentTrackIndex++;

    if (m_currentTrackIndex >= m_trackQueue.size()) {
        // All done - batch conversion complete
        m_isConverting = false;
        m_progressBar->setValue(100);
        m_statusLabel->setText(QString("Batch conversion completed! (%1 files)").arg(m_totalTracks));
        m_convertButton->setEnabled(true);
        m_cancelButton->setEnabled(false);

        QMessageBox::information(this, "Batch Conversion Complete",
            QString("Successfully converted %1 files!").arg(m_totalTracks));
        return;
    }

    // Get current track
    QString inputPath = m_trackQueue[m_currentTrackIndex];
    QFileInfo info(inputPath);

    // Store current filename for progress updates
    m_currentFilename = info.fileName();

    // Generate output path - use selected folder or same as source
    QString outputPath;
    QString outputDir = m_outputEdit->text();
    
    if (outputDir == QStringLiteral("Same as source folder")) {
        // Use same directory as source file
        outputPath = info.absolutePath() + "/" + info.completeBaseName() + "." + getOutputExtension();
    } else {
        // Use selected output folder
        outputPath = outputDir + "/" + info.completeBaseName() + "." + getOutputExtension();
    }

    // Update status (progress updates will add percentage)
    m_statusLabel->setText(QString("Converting %1 of %2: %3")
        .arg(m_currentTrackIndex + 1)
        .arg(m_totalTracks)
        .arg(m_currentFilename));

    // Build options
    ConversionOptions options;
    options.format = getOutputExtension();

    int qualityValue = m_qualityCombo->currentData().toInt();
    if (options.format == "mp3") {
        if (qualityValue < 0) {
            options.quality = -qualityValue - 100;
            options.bitrate = 0;
        } else {
            options.bitrate = qualityValue;
            options.quality = -1;
        }
    }
    else if (options.format == "flac") {
        options.compressionLevel = qualityValue;
        options.bitrate = 0;
        options.quality = -1;
    }
    else if (options.format == "opus") {
        options.bitrate = qualityValue;
        options.quality = -1;
    }
    else if (options.format == "ogg") {
        options.quality = qualityValue;
        options.bitrate = 0;
    }

    options.sampleRate = m_sampleRateSpin->value();
    options.channels = m_channelsCombo->currentData().toInt();

    // Start conversion
    options.tags = readTagsFromFile(inputPath);
    m_tempCoverArtPath = extractCoverArt(inputPath);
    options.coverArtPath = m_tempCoverArtPath;
    m_manager->convertAsync(inputPath, outputPath, options);
}

void ConverterWidget::applyDefaultCodec()
{
    if (!m_settings) {
        return;
    }

    QString defaultCodec = m_settings->value<ConverterSettings::DefaultCodec>();

    // Find and select the default codec in the combo box
    for (int i = 0; i < m_formatCombo->count(); ++i) {
        if (m_formatCombo->itemData(i).toString() == defaultCodec) {
            m_formatCombo->setCurrentIndex(i);
            break;
        }
    }
}

void ConverterWidget::closeEvent(QCloseEvent* event)
{
    // If conversion is in progress, ask for confirmation
    if (m_isConverting) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            "Conversion in Progress",
            "A conversion is currently in progress. Do you want to cancel it and close?",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No  // Default to No for safety
        );

        if (reply == QMessageBox::Yes) {
            // Cancel the conversion
            cancelConversion();
            event->accept();
        } else {
            // Don't close
            event->ignore();
        }
    } else {
        // No conversion in progress, close normally
        event->accept();
    }
}

void ConverterWidget::keyPressEvent(QKeyEvent* event)
{
    // Handle Escape key to close the dialog
    if (event->key() == Qt::Key_Escape) {
        close();  // This will trigger closeEvent()
        event->accept();
    } else {
        // Pass other keys to the base class
        FyWidget::keyPressEvent(event);
    }
}
