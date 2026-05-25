#include "structures_loader.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>


namespace ManagementLayer {

QString NarrativeStructure::displayLabel() const
{
    if (author.isEmpty()) {
        return name;
    }
    return QStringLiteral("%1 (%2)").arg(name, author);
}

QString NarrativeStructure::promptDescription() const
{
    QStringList lines;
    lines << QStringLiteral("ESTRUCTURA: %1").arg(name);
    if (!author.isEmpty()) {
        lines << QStringLiteral("Autor/origen: %1").arg(author);
    }
    if (!tradition.isEmpty()) {
        lines << QStringLiteral("Tradición: %1").arg(tradition);
    }
    if (!keyConcepts.isEmpty()) {
        lines << QStringLiteral("Conceptos clave: %1").arg(keyConcepts.join(QStringLiteral(", ")));
    }
    if (!beats.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("BEATS de la estructura:");
        int idx = 1;
        for (const auto& beat : beats) {
            lines << QStringLiteral("%1. %2 — %3").arg(idx++).arg(beat.name, beat.role);
        }
    }
    return lines.join(QStringLiteral("\n"));
}


// ****


QString StructuresLoader::defaultPath()
{
    return QDir::homePath()
        + QStringLiteral("/memoria-asistente-escritura/referencias/_estructuras.json");
}

QVector<NarrativeStructure> StructuresLoader::load()
{
    return loadFrom(defaultPath());
}

QVector<NarrativeStructure> StructuresLoader::loadFrom(const QString& _filePath)
{
    QVector<NarrativeStructure> result;

    QFile file(_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }

    const QJsonObject root = doc.object();
    const QJsonArray structuresArr = root.value(QStringLiteral("estructuras")).toArray();

    for (const auto& v : structuresArr) {
        const QJsonObject obj = v.toObject();
        NarrativeStructure ns;
        ns.id = obj.value(QStringLiteral("id")).toString();
        ns.name = obj.value(QStringLiteral("nombre")).toString();
        ns.author = obj.value(QStringLiteral("autor")).toString();
        ns.tradition = obj.value(QStringLiteral("tradicion")).toString();
        ns.type = obj.value(QStringLiteral("tipo")).toString();
        ns.popularity = obj.value(QStringLiteral("popularidad")).toString();

        const QJsonArray conceptsArr = obj.value(QStringLiteral("conceptos_clave")).toArray();
        for (const auto& c : conceptsArr) {
            const QString conceptStr = c.toString();
            if (!conceptStr.isEmpty()) {
                ns.keyConcepts << conceptStr;
            }
        }

        const QJsonArray beatsArr = obj.value(QStringLiteral("beats")).toArray();
        for (const auto& b : beatsArr) {
            const QJsonObject bObj = b.toObject();
            StructureBeat beat;
            beat.id = bObj.value(QStringLiteral("id")).toString();
            beat.name = bObj.value(QStringLiteral("nombre")).toString();
            beat.role = bObj.value(QStringLiteral("rol")).toString();
            ns.beats << beat;
        }

        if (!ns.id.isEmpty() && !ns.name.isEmpty()) {
            result << ns;
        }
    }

    return result;
}

} // namespace ManagementLayer
