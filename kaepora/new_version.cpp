#include "new_version.h"

#include "version.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVersionNumber>

#include <chrono>
#include <optional>

constexpr char LatestAddress[] = "https://github.com/Kaepora-HQ/kaepora/releases/latest/download/latest.json";
constexpr char ReleasePagePattern[] = R"(https://github\.com/Kaepora-HQ/kaepora/releases/tag/[A-Za-z0-9._-]+)";
constexpr std::chrono::hours AskEvery{ 24 };
// Qt's transfer timeout: the request stops when no data comes for this long.
constexpr std::chrono::seconds RequestTimeout{ 30 };

constexpr char CheckKey[] = "checkForNewVersions";
constexpr char LastCheckKey[] = "lastVersionCheck";
constexpr char LatestKey[] = "latestRelease";

namespace {
struct Release {
    QVersionNumber version;
    QUrl page;
};

std::optional<Release> ParseRelease(const QByteArray& json) {
    static const QRegularExpression releasePage(QRegularExpression::anchoredPattern(ReleasePagePattern));
    const QJsonObject file = QJsonDocument::fromJson(json).object();
    const QVersionNumber version = QVersionNumber::fromString(file["version"].toString());
    const QString page = file["url"].toString();
    if (version.isNull() || !releasePage.match(page).hasMatch()) {
        return std::nullopt;
    }
    return Release{ version, QUrl(page) };
}

class Row : public QWidget {
public:
    Row() {
        check->setChecked(QSettings().value(CheckKey, true).toBool());
        auto* download = new QPushButton("Download");
        auto* noticeLayout = new QHBoxLayout(notice);
        noticeLayout->setContentsMargins({});
        noticeLayout->addWidget(text);
        noticeLayout->addWidget(download);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins({});
        layout->addWidget(new QLabel("Version " KAEPORA_VERSION));
        layout->addWidget(check);
        layout->addStretch();
        layout->addWidget(notice);

        connect(check, &QCheckBox::toggled, this, [this](bool on) {
            QSettings().setValue(CheckKey, on);
            updateNotice();
            askIfDue();
        });
        connect(download, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(latest->page); });
        updateNotice();
        askIfDue();
    }

private:
    void updateNotice() {
        static const QVersionNumber running = QVersionNumber::fromString(KAEPORA_VERSION);
        const bool shown = check->isChecked() && latest && latest->version > running;
        if (shown) {
            text->setText(QString("Kaepora %1 is out.").arg(latest->version.toString()));
        }
        notice->setVisible(shown);
    }

    void askIfDue() {
        QSettings settings;
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const QDateTime last = settings.value(LastCheckKey).toDateTime();
        if (!check->isChecked() || (last.isValid() && now < last.addDuration(AskEvery))) {
            return;
        }
        settings.setValue(LastCheckKey, now);
        QNetworkRequest request{ QUrl(LatestAddress) };
        request.setTransferTimeout(RequestTimeout);
        request.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, true);
        QNetworkReply* reply = network->get(request);
        connect(check, &QCheckBox::toggled, reply, &QNetworkReply::abort);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            if (reply->error() != QNetworkReply::NoError) {
                return;
            }
            const QByteArray json = reply->readAll();
            if (const std::optional<Release> release = ParseRelease(json)) {
                QSettings().setValue(LatestKey, QString::fromUtf8(json));
                latest = release;
                updateNotice();
            }
        });
    }

    QCheckBox* check = new QCheckBox("Check for new versions");
    QWidget* notice = new QWidget;
    QLabel* text = new QLabel;
    QNetworkAccessManager* network = new QNetworkAccessManager(this);
    std::optional<Release> latest = ParseRelease(QSettings().value(LatestKey).toByteArray());
};
}

QWidget* NewVersionRow() {
    return new Row;
}
