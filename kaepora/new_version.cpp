#include "new_version.h"

#include "version.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
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
// Qt's transfer timeout: the request stops when no data comes for this long.
constexpr std::chrono::seconds RequestTimeout{ 30 };

constexpr char CheckKey[] = "checkForNewVersions";
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
            ask();
        });
        connect(download, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(latest->page); });
        updateNotice();
        ask();
    }

private:
    bool newer() const {
        static const QVersionNumber running = QVersionNumber::fromString(KAEPORA_VERSION);
        return check->isChecked() && latest && latest->version > running;
    }

    void updateNotice() {
        if (newer()) {
            text->setText(QString("Kaepora %1 is out.").arg(latest->version.toString()));
        }
        notice->setVisible(newer());
    }

    // Once per open, after a request: with the release it fetched, or with the one saved when it fetched none. The box
    // comes unasked, maybe while a seed is typed, so a Return or an Escape meant for the window picks "Later".
    void offerOnce() {
        if (offered || !newer()) {
            return;
        }
        offered = true;
        auto* box = new QMessageBox(QMessageBox::Information, "New version",
                                    QString("Kaepora %1 is out. You have %2.")
                                        .arg(latest->version.toString(), KAEPORA_VERSION),
                                    QMessageBox::NoButton, window());
        box->setAttribute(Qt::WA_DeleteOnClose);
        connect(box->addButton("Download", QMessageBox::AcceptRole), &QPushButton::clicked, this,
                [page = latest->page] { QDesktopServices::openUrl(page); });
        box->setDefaultButton(box->addButton("Later", QMessageBox::RejectRole));
        box->open();
    }

    void ask() {
        if (!check->isChecked()) {
            return;
        }
        QNetworkRequest request{ QUrl(LatestAddress) };
        request.setTransferTimeout(RequestTimeout);
        request.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, true);
        QNetworkReply* reply = network->get(request);
        connect(check, &QCheckBox::toggled, reply, &QNetworkReply::abort);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            const QByteArray json = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
            if (const std::optional<Release> release = ParseRelease(json)) {
                QSettings().setValue(LatestKey, QString::fromUtf8(json));
                latest = release;
                updateNotice();
            }
            offerOnce();
        });
    }

    QCheckBox* check = new QCheckBox("Check for new versions");
    QWidget* notice = new QWidget;
    QLabel* text = new QLabel;
    QNetworkAccessManager* network = new QNetworkAccessManager(this);
    std::optional<Release> latest = ParseRelease(QSettings().value(LatestKey).toByteArray());
    bool offered = false;
};
}

QWidget* NewVersionRow() {
    return new Row;
}
