#include "completion_offer.h"

#include "bytes.h"
#include "game_watch.h"
#include "generator/generate.h"
#include "library.h"
#include "tell.h"

#include <QCheckBox>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

constexpr char CompletionAddress[] = "https://api.kaepora.org/v1/seeds/%1/completion";
// Qt's transfer timeout: the request stops when no data moves for this long.
constexpr std::chrono::seconds RequestTimeout{ 30 };

constexpr char OfferKey[] = "offerToRecordCompletedSeeds";
constexpr char OfferedKey[] = "completedSeedsOffered";
constexpr char UnrecordedKey[] = "unrecordedCompletions";

// The quest's step turns Complete as Ganon dies, and no save holds Complete.
const SavedValue Step{ "Step_Defeat_Ganon", ValueType::Enum };
const SavedValue Seed{ "Kaepora_Seed", ValueType::Int };
const SavedValue PlayTime{ "0xe573f564", ValueType::UInt };

struct LibrarySeed {
    QString folder;
    nlohmann::json log;
};

static std::optional<LibrarySeed> SeedOf(const QString& library, quint32 id) {
    for (const QFileInfo& folder : QDir(library).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        try {
            nlohmann::json log = LogOf(folder.filePath());
            if (SeedId(log.at("kaepora").get<std::string>(), log.at("seed").get<std::string>(), log.at("settings")) ==
                id) {
                return LibrarySeed{ folder.filePath(), std::move(log) };
            }
        } catch (const std::exception&) {
            // A log that cannot be read, or one of a release that wrote no version, gives no id.
        }
    }
    return std::nullopt;
}

static QString Duration(quint32 seconds) {
    return QString("%1:%2:%3")
        .arg(seconds / 3600)
        .arg(seconds / 60 % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}

static bool Recorded(const QNetworkReply* reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return status == 201 || status == 200;
}

namespace {
class Box : public QCheckBox {
public:
    Box(quint16 gamePort, const QString& library)
        : QCheckBox("Offer to record completed seeds"), gamePort(gamePort), library(library) {
        setChecked(QSettings().value(OfferKey, true).toBool());
        connect(this, &QCheckBox::toggled, this, [this](bool on) {
            QSettings().setValue(OfferKey, on);
            watch();
        });
        watch();
        QSettings unrecorded;
        unrecorded.beginGroup(UnrecordedKey);
        for (const QString& id : unrecorded.childKeys()) {
            send(id, unrecorded.value(id).toString().toUtf8());
        }
    }

private:
    void watch() {
        game.reset();
        completedAtLastRead.reset();
        if (isChecked()) {
            game = std::make_unique<GameWatch>(gamePort, std::vector{ Step, Seed, PlayTime });
            connect(game.get(), &GameWatch::changed, this, &Box::take);
            game->start();
        }
    }

    void take(const std::vector<quint32>& read) {
        const quint32 step = read[0], seed = read[1], playSeconds = read[2];
        const std::optional<quint32> completed = step == Murmur3("Complete") ? std::optional(seed) : std::nullopt;
        if (completed && completed != completedAtLastRead) {
            offer(*completed, playSeconds);
        }
        completedAtLastRead = completed;
    }

    void offer(quint32 id, quint32 playSeconds) {
        QSettings settings;
        QStringList offered = settings.value(OfferedKey).toStringList();
        if (offered.contains(QString::number(id))) {
            return;
        }
        const std::optional<LibrarySeed> found = SeedOf(library, id);
        if (!found) {
            return;
        }
        settings.setValue(OfferedKey, offered << QString::number(id));

        const nlohmann::json& log = found->log;
        const std::string seed = log.at("seed").get<std::string>();
        QString text = QString("You completed seed %1 in %2.").arg(QString::fromStdString(seed), Duration(playSeconds));
        if (!std::ranges::all_of(seed, [](char c) { return c >= '0' && c <= '9'; })) {
            text += "\nYou typed this seed's name, so the site will not show it.";
        }
        text += "\n\nRecord it on kaepora.org? The site will list completed seeds once there are enough of them.\n\n"
                "Kaepora sends the seed, its settings, your play time and the version of Kaepora that made it. It "
                "sends nothing about you or your computer.";
        auto* box = new QMessageBox(QMessageBox::Question, "Seed completed", text, QMessageBox::NoButton, window());
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setCheckBox(new QCheckBox("Do not offer this again"));
        const nlohmann::json body = { { "kaepora", log.at("kaepora") },
                                      { "seed", seed },
                                      { "settings", log.at("settings") },
                                      { "playSeconds", playSeconds } };
        connect(box->addButton("Record it", QMessageBox::AcceptRole), &QPushButton::clicked, this,
                [this, folder = found->folder, body = QString::fromStdString(body.dump())] { record(folder, body); });
        box->setDefaultButton(box->addButton("Not now", QMessageBox::RejectRole));
        connect(box, &QMessageBox::finished, this, [this, box] {
            if (box->checkBox()->isChecked()) {
                setChecked(false);
            }
        });
        box->open();
    }

    void record(const QString& folder, const QString& body) {
        const QString id = LibraryId(folder).toString(QUuid::WithoutBraces);
        QSettings().setValue(QString(UnrecordedKey) + "/" + id, body);
        QNetworkReply* reply = send(id, body.toUtf8());
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            if (Recorded(reply)) {
                Tell(window(), QMessageBox::Information, "Recorded on kaepora.org.");
            } else {
                Tell(window(), QMessageBox::Warning,
                     "Kaepora could not reach kaepora.org. It will try again the next time it opens.");
            }
        });
    }

    QNetworkReply* send(const QString& id, const QByteArray& body) {
        QNetworkRequest request{ QUrl(QString(CompletionAddress).arg(id)) };
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        // Qt would send the languages the computer is set to.
        request.setRawHeader("Accept-Language", "*");
        request.setTransferTimeout(RequestTimeout);
        request.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, true);
        QNetworkReply* reply = network->put(request, body);
        connect(reply, &QNetworkReply::finished, this, [reply, id] {
            if (Recorded(reply)) {
                QSettings().remove(QString(UnrecordedKey) + "/" + id);
            }
        });
        return reply;
    }

    const quint16 gamePort;
    const QString library;
    QNetworkAccessManager* network = new QNetworkAccessManager(this);
    std::unique_ptr<GameWatch> game;
    std::optional<quint32> completedAtLastRead;
};
}

QWidget* CompletionOfferBox(quint16 gamePort, const QString& library) {
    return new Box(gamePort, library);
}
