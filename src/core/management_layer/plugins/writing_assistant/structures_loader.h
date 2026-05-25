#pragma once

#include <QString>
#include <QVector>


namespace ManagementLayer {

/**
 * @brief Beat individual dentro de una estructura narrativa.
 */
struct StructureBeat {
    QString id;
    QString name;
    QString role;
};

/**
 * @brief Estructura narrativa curada (una de las 25 del playbook del usuario).
 *
 * Fuente: ~/memoria-asistente-escritura/referencias/_estructuras.json
 * Curada manualmente por el usuario. Aula 122 la lee al arrancar el plugin.
 */
struct NarrativeStructure {
    QString id;       // ej. "aristoteles-poetica"
    QString name;     // ej. "Poética"
    QString author;   // ej. "Aristóteles"
    QString tradition;
    QString type;
    QString popularity;
    QStringList keyConcepts;
    QVector<StructureBeat> beats;

    /**
     * @brief Línea descriptiva corta para combo box: "Poética (Aristóteles)".
     */
    QString displayLabel() const;

    /**
     * @brief Texto que se envía a Claude para que analice el guion contra
     *        esta estructura. Incluye nombre, autor, conceptos clave, beats.
     */
    QString promptDescription() const;
};

/**
 * @brief Carga las estructuras narrativas desde el JSON del usuario.
 *
 * El path por defecto es ~/memoria-asistente-escritura/referencias/_estructuras.json.
 * Si el archivo no existe, retorna una lista vacía (el plugin sigue funcionando
 * como chat normal sin selector de estructura).
 */
class StructuresLoader
{
public:
    /**
     * @brief Cargar desde el path por defecto.
     */
    static QVector<NarrativeStructure> load();

    /**
     * @brief Cargar desde un path específico (útil para tests).
     */
    static QVector<NarrativeStructure> loadFrom(const QString& _filePath);

    /**
     * @brief Path por defecto resuelto contra $HOME.
     */
    static QString defaultPath();
};

} // namespace ManagementLayer
