#pragma once

#include <gui/fywidget.h>

namespace Fooyin {
class SettingsManager;
}

class ConversionManager;
class QLineEdit;
class QComboBox;
class QProgressBar;
class QPushButton;
class QLabel;
class QSpinBox;

class ConverterWidget : public Fooyin::FyWidget
{
    Q_OBJECT

public:
    explicit ConverterWidget(ConversionManager* manager, Fooyin::SettingsManager* settings = nullptr, QWidget* parent = nullptr);

    [[nodiscard]] QString name() const override { return QStringLiteral("Audio Converter"); }
    [[nodiscard]] QString layoutName() const override { return QStringLiteral("AudioConverter"); }

    void loadTrack(const QString& filepath);
    void loadTracks(const QStringList& filepaths);

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void browseInput();
    void browseOutput();
    void startConversion();
    void cancelConversion();
    void onFormatChanged(int index);
    void onProgress(int percent);
    void onFinished(bool success, const QString& error);
    void onStarted();

private:
    void setupUI();
    QString getOutputExtension() const;
    void updateOutputPath();
    void updateQualityOptions();
    void updateCodecInfo();
    bool validateInput();
    void processNextTrack();
    void applyDefaultCodec();

    QString m_tempCoverArtPath;

    ConversionManager* m_manager;
    Fooyin::SettingsManager* m_settings;

    // Conversion state
    bool m_isConverting{false};

    // Batch conversion
    QStringList m_trackQueue;
    int m_currentTrackIndex{-1};
    int m_totalTracks{0};
    QString m_currentFilename;

    // UI elements
    QLineEdit* m_inputEdit;
    QLineEdit* m_outputEdit;
    QComboBox* m_formatCombo;
    QComboBox* m_qualityCombo;
    QSpinBox* m_sampleRateSpin;
    QComboBox* m_channelsCombo;
    QProgressBar* m_progressBar;
    QPushButton* m_convertButton;
    QPushButton* m_cancelButton;
    QLabel* m_statusLabel;
    QLabel* m_codecInfoLabel;
};
