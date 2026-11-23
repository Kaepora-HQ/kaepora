#pragma once

#include <QtGlobal>

class QString;
class QWidget;

// The box "Offer to record completed seeds". While it is ticked, it follows the game through Lichii's debug server on
// the port. When the game shows a seed of the library completed, it offers once to record the completion on
// kaepora.org, and sends it only when the player says yes. When made, it sends again each completion kaepora.org has
// not confirmed.
QWidget* CompletionOfferBox(quint16 gamePort, const QString& library);
