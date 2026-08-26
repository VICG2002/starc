#include "production_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>


namespace BusinessLayer {

QSet<QUuid> ProductionState::assignedScenes() const
{
    QSet<QUuid> result;
    for (const auto& day : shootingDays) {
        for (const auto& sceneUuid : day.sceneUuids) {
            result.insert(sceneUuid);
        }
    }
    return result;
}


// ****


namespace {

QString aula122ProjectsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/Aula 122/projects");
}

QString projectNameFromStarcPath(const QString& _starcFilePath)
{
    if (_starcFilePath.isEmpty()) {
        return QStringLiteral("Untitled");
    }
    QFileInfo info(_starcFilePath);
    return info.completeBaseName().isEmpty() ? QStringLiteral("Untitled")
                                             : info.completeBaseName();
}

// ===== JSON helpers =====

QJsonObject crewToJson(const CrewMember& _m)
{
    QJsonObject o;
    o["uuid"] = _m.uuid.toString();
    o["name"] = _m.name;
    o["department"] = _m.department;
    o["role"] = _m.role;
    o["contact"] = _m.contact;
    o["isDaily"] = _m.isDaily;
    o["dailyRate"] = _m.dailyRate;
    o["notes"] = _m.notes;
    return o;
}
CrewMember crewFromJson(const QJsonObject& _o)
{
    CrewMember m;
    m.uuid = QUuid(_o.value("uuid").toString());
    m.name = _o.value("name").toString();
    m.department = _o.value("department").toString();
    m.role = _o.value("role").toString();
    m.contact = _o.value("contact").toString();
    m.isDaily = _o.value("isDaily").toBool();
    m.dailyRate = _o.value("dailyRate").toDouble();
    m.notes = _o.value("notes").toString();
    return m;
}

QJsonObject equipmentToJson(const EquipmentItem& _e)
{
    QJsonObject o;
    o["uuid"] = _e.uuid.toString();
    o["name"] = _e.name;
    o["category"] = _e.category;
    o["owner"] = _e.owner;
    o["dailyRate"] = _e.dailyRate;
    o["notes"] = _e.notes;
    return o;
}
EquipmentItem equipmentFromJson(const QJsonObject& _o)
{
    EquipmentItem e;
    e.uuid = QUuid(_o.value("uuid").toString());
    e.name = _o.value("name").toString();
    e.category = _o.value("category").toString();
    e.owner = _o.value("owner").toString();
    e.dailyRate = _o.value("dailyRate").toDouble();
    e.notes = _o.value("notes").toString();
    return e;
}

QJsonObject shootingDayToJson(const ShootingDay& _d)
{
    QJsonObject o;
    o["uuid"] = _d.uuid.toString();
    o["date"] = _d.date.toString(Qt::ISODate);
    o["callTime"] = _d.callTime.toString(Qt::ISODate);
    o["wrapTime"] = _d.wrapTime.toString(Qt::ISODate);
    o["primaryLocation"] = _d.primaryLocation;
    QJsonArray scenes;
    for (const auto& u : _d.sceneUuids) {
        scenes.append(u.toString());
    }
    o["sceneUuids"] = scenes;
    QJsonArray crew;
    for (const auto& u : _d.crewUuids) {
        crew.append(u.toString());
    }
    o["crewUuids"] = crew;
    o["sunriseSunset"] = _d.sunriseSunset;
    o["weatherNote"] = _d.weatherNote;
    o["notes"] = _d.notes;
    return o;
}
ShootingDay shootingDayFromJson(const QJsonObject& _o)
{
    ShootingDay d;
    d.uuid = QUuid(_o.value("uuid").toString());
    d.date = QDate::fromString(_o.value("date").toString(), Qt::ISODate);
    d.callTime = QTime::fromString(_o.value("callTime").toString(), Qt::ISODate);
    d.wrapTime = QTime::fromString(_o.value("wrapTime").toString(), Qt::ISODate);
    d.primaryLocation = _o.value("primaryLocation").toString();
    for (const auto& v : _o.value("sceneUuids").toArray()) {
        d.sceneUuids.append(QUuid(v.toString()));
    }
    for (const auto& v : _o.value("crewUuids").toArray()) {
        d.crewUuids.append(QUuid(v.toString()));
    }
    d.sunriseSunset = _o.value("sunriseSunset").toString();
    d.weatherNote = _o.value("weatherNote").toString();
    d.notes = _o.value("notes").toString();
    return d;
}

QJsonObject callSheetToJson(const CallSheet& _c)
{
    QJsonObject o;
    o["uuid"] = _c.uuid.toString();
    o["shootingDayUuid"] = _c.shootingDayUuid.toString();
    o["headerNote"] = _c.headerNote;
    o["parkingNote"] = _c.parkingNote;
    o["cateringNote"] = _c.cateringNote;
    o["safetyNote"] = _c.safetyNote;
    o["customMessage"] = _c.customMessage;
    return o;
}
CallSheet callSheetFromJson(const QJsonObject& _o)
{
    CallSheet c;
    c.uuid = QUuid(_o.value("uuid").toString());
    c.shootingDayUuid = QUuid(_o.value("shootingDayUuid").toString());
    c.headerNote = _o.value("headerNote").toString();
    c.parkingNote = _o.value("parkingNote").toString();
    c.cateringNote = _o.value("cateringNote").toString();
    c.safetyNote = _o.value("safetyNote").toString();
    c.customMessage = _o.value("customMessage").toString();
    return c;
}

QJsonObject budgetLineToJson(const BudgetLineItem& _b)
{
    QJsonObject o;
    o["uuid"] = _b.uuid.toString();
    o["category"] = _b.category;
    o["description"] = _b.description;
    o["quantity"] = _b.quantity;
    o["unitCost"] = _b.unitCost;
    o["unit"] = _b.unit;
    o["notes"] = _b.notes;
    return o;
}
BudgetLineItem budgetLineFromJson(const QJsonObject& _o)
{
    BudgetLineItem b;
    b.uuid = QUuid(_o.value("uuid").toString());
    b.category = _o.value("category").toString();
    b.description = _o.value("description").toString();
    b.quantity = _o.value("quantity").toInt(1);
    b.unitCost = _o.value("unitCost").toDouble();
    b.unit = _o.value("unit").toString();
    b.notes = _o.value("notes").toString();
    return b;
}

QJsonObject budgetToJson(const ProductionBudget& _b)
{
    QJsonObject o;
    o["currency"] = _b.currency;
    o["contingencyPercent"] = _b.contingencyPercent;
    QJsonArray items;
    for (const auto& li : _b.lineItems) {
        items.append(budgetLineToJson(li));
    }
    o["lineItems"] = items;
    return o;
}
ProductionBudget budgetFromJson(const QJsonObject& _o)
{
    ProductionBudget b;
    b.currency = _o.value("currency").toString(QStringLiteral("MXN"));
    b.contingencyPercent = _o.value("contingencyPercent").toDouble(10.0);
    for (const auto& v : _o.value("lineItems").toArray()) {
        b.lineItems.append(budgetLineFromJson(v.toObject()));
    }
    return b;
}

} // anonymous namespace

QString ProductionStorage::productionJsonPath(const QString& _starcFilePath)
{
    return aula122ProjectsRoot() + '/' + projectNameFromStarcPath(_starcFilePath)
        + QStringLiteral("/production.json");
}

ProductionState ProductionStorage::load(const QString& _starcFilePath)
{
    ProductionState state;
    const QString path = productionJsonPath(_starcFilePath);
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return state; // estado vacío, no es error
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return state;
    }
    const QJsonObject root = doc.object();

    for (const auto& v : root.value("crew").toArray()) {
        state.crew.append(crewFromJson(v.toObject()));
    }
    for (const auto& v : root.value("equipment").toArray()) {
        state.equipment.append(equipmentFromJson(v.toObject()));
    }
    for (const auto& v : root.value("shootingDays").toArray()) {
        state.shootingDays.append(shootingDayFromJson(v.toObject()));
    }
    for (const auto& v : root.value("callSheets").toArray()) {
        state.callSheets.append(callSheetFromJson(v.toObject()));
    }
    if (root.contains("budget")) {
        state.budget = budgetFromJson(root.value("budget").toObject());
    }
    return state;
}

bool ProductionStorage::save(const QString& _starcFilePath, const ProductionState& _state)
{
    const QString path = productionJsonPath(_starcFilePath);
    QFileInfo info(path);
    QDir dir;
    if (!dir.mkpath(info.absolutePath())) {
        return false;
    }

    QJsonObject root;
    QJsonArray crewArr;
    for (const auto& c : _state.crew) {
        crewArr.append(crewToJson(c));
    }
    root["crew"] = crewArr;

    QJsonArray equipmentArr;
    for (const auto& e : _state.equipment) {
        equipmentArr.append(equipmentToJson(e));
    }
    root["equipment"] = equipmentArr;

    QJsonArray daysArr;
    for (const auto& d : _state.shootingDays) {
        daysArr.append(shootingDayToJson(d));
    }
    root["shootingDays"] = daysArr;

    QJsonArray callSheetsArr;
    for (const auto& c : _state.callSheets) {
        callSheetsArr.append(callSheetToJson(c));
    }
    root["callSheets"] = callSheetsArr;

    root["budget"] = budgetToJson(_state.budget);

    //
    // Escritura atómica: .tmp + rename. Evita corrupción si la app crashea
    // a mitad de write (recomendado por CC-Switch research previo).
    //
    const QString tmpPath = path + QStringLiteral(".tmp");
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QJsonDocument doc(root);
    tmp.write(doc.toJson(QJsonDocument::Indented));
    tmp.close();

    QFile::remove(path); // remove old if exists (Windows compat)
    return QFile::rename(tmpPath, path);
}

} // namespace BusinessLayer
