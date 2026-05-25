#pragma once

#include <corelib_global.h>

#include <QDate>
#include <QString>
#include <QTime>
#include <QUuid>
#include <QVector>


namespace BusinessLayer {

/**
 * @brief Modelos de pre-producción cinematográfica para Aula 122.
 *
 * Estos NO son AbstractModel de STARC (que requieren persistencia XML al
 * .starc, ~1000 líneas cada uno). Son structs simples con serialización
 * JSON paralela en `~/Documents/Aula 122/projects/<proyecto>/production/`.
 * Decisión tomada en Bloque 6 del plan: cambiar persistencia XML→JSON para
 * entregar el ecosistema 4x más rápido sin perder funcionalidad. Si en el
 * futuro se decide integrarlos al .starc, migración trivial.
 *
 * Mantienen UUIDs para referencias cruzadas (ShootingDay → sceneUuids,
 * CallSheet → ShootingDay). Las escenas referenciadas viven en el
 * ScreenplayTextModel del .starc; producción es un overlay.
 */

/**
 * @brief Un personaje del crew (camera, sound, art, etc.)
 */
struct CORE_LIBRARY_EXPORT CrewMember {
    QUuid uuid;
    QString name;
    QString department; // Camera, Sound, Art, Production, etc.
    QString role;       // DP, Sound Mixer, Production Designer...
    QString contact;    // email o teléfono
    bool isDaily = false; // contratado por día (vs. permanente todo el rodaje)
    double dailyRate = 0.0;
    QString notes;
};

/**
 * @brief Equipo (cámara, lentes, luces, audio, transporte, etc.)
 */
struct CORE_LIBRARY_EXPORT EquipmentItem {
    QUuid uuid;
    QString name;       // "Sony FX6", "Sennheiser MKH 416", "Aputure 600d"
    QString category;   // Cámara, Lente, Audio, Iluminación, Grip, etc.
    QString owner;      // dueño/proveedor (estudio, rental, propio)
    double dailyRate = 0.0;
    QString notes;
};

/**
 * @brief Un día de rodaje. Agrupa escenas + crew asignado + cast del día.
 *
 * sceneUuids referencia ScreenplayTextModelSceneItem::uuid del modelo del
 * guion. crewUuids referencia CrewMember::uuid de la lista de crew del
 * proyecto. cast del día se deriva automáticamente de los personajes que
 * hablan/aparecen en las sceneUuids asignadas.
 */
struct CORE_LIBRARY_EXPORT ShootingDay {
    QUuid uuid;
    QDate date;
    QTime callTime;
    QTime wrapTime;
    QString primaryLocation; // texto libre, ej. "Casa de Vero — Sala"
    QVector<QUuid> sceneUuids; // escenas a grabar este día
    QVector<QUuid> crewUuids;  // crew específico del día (los Daily=true)
    QString sunriseSunset;     // opcional, "06:42 / 19:18"
    QString weatherNote;       // opcional, "Posible lluvia tarde"
    QString notes;
};

/**
 * @brief Call sheet generado a partir de un ShootingDay.
 *        Es solo una vista — los datos vienen de ShootingDay + Crew + Cast.
 *        Esta struct existe para futuras notas específicas del call sheet
 *        (mensajes a cast, instrucciones de parking, etc.) que no
 *        corresponden al shooting day en sí.
 */
struct CORE_LIBRARY_EXPORT CallSheet {
    QUuid uuid;
    QUuid shootingDayUuid;
    QString headerNote;     // texto al inicio del call sheet
    QString parkingNote;
    QString cateringNote;
    QString safetyNote;
    QString customMessage;  // cualquier mensaje libre al cast/crew
};

/**
 * @brief Item del presupuesto. Una línea como "Cámara Sony FX6: 5 días × $250 = $1250".
 */
struct CORE_LIBRARY_EXPORT BudgetLineItem {
    QUuid uuid;
    QString category;   // Cast, Crew, Equipment, Locations, Post, etc.
    QString description;
    int quantity = 1;
    double unitCost = 0.0;
    QString unit; // "días", "jornadas", "viajes", "unidades"
    QString notes;

    double total() const { return quantity * unitCost; }
};

/**
 * @brief Presupuesto completo del proyecto.
 */
struct CORE_LIBRARY_EXPORT ProductionBudget {
    QString currency = QStringLiteral("MXN"); // default pesos mexicanos para Aula 122
    QVector<BudgetLineItem> lineItems;
    double contingencyPercent = 10.0; // contingencia estándar 10-15%

    double subtotal() const
    {
        double sum = 0.0;
        for (const auto& li : lineItems) {
            sum += li.total();
        }
        return sum;
    }
    double contingencyAmount() const { return subtotal() * contingencyPercent / 100.0; }
    double total() const { return subtotal() + contingencyAmount(); }
};

/**
 * @brief Estado completo de producción de un proyecto.
 *        Esto es lo que se serializa al JSON paralelo.
 */
struct CORE_LIBRARY_EXPORT ProductionState {
    QVector<CrewMember> crew;
    QVector<EquipmentItem> equipment;
    QVector<ShootingDay> shootingDays;
    QVector<CallSheet> callSheets;
    ProductionBudget budget;

    /**
     * @brief Escenas asignadas a cualquier shooting day (excluyendo el "boneyard").
     */
    QSet<QUuid> assignedScenes() const;
};

} // namespace BusinessLayer
