#pragma once
#include <QString>
#include <QPixmap>
#include <QHash>

// auf ~32px skaliert. Leeres QPixmap bei Fehler/ohne Badge.
class BadgeLoader {
public:
    QPixmap get(const QString& badgeName);
private:
    QHash<QString, QPixmap> mem_;
};
