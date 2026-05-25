#pragma once

#include "production_models.h"

#include <corelib_global.h>

#include <QString>


namespace BusinessLayer {

/**
 * @brief Carga/guarda el estado de pre-producción de un proyecto en JSON.
 *
 * El archivo vive en
 *   ~/Documents/Aula 122/projects/<projectName>/production.json
 *
 * <projectName> se deriva del path del archivo .starc del proyecto (sin
 * extensión). Si el .starc está en otra carpeta, se usa solo el nombre
 * del archivo. Aula 122 crea la carpeta production si no existe al
 * primer save.
 *
 * Patrón usado por toda la capa de producción (Bloques 6-10 del plan).
 * Si en el futuro se migra a persistencia dentro del .starc, este es
 * el único punto a refactorizar.
 */
class CORE_LIBRARY_EXPORT ProductionStorage
{
public:
    /**
     * @brief Path del archivo production.json para un proyecto dado.
     *        _starcFilePath puede ser absoluto o solo el nombre.
     *        Si está vacío, retorna un path bajo "Untitled/".
     */
    static QString productionJsonPath(const QString& _starcFilePath);

    /**
     * @brief Cargar el estado desde disco. Si no existe el archivo,
     *        retorna un ProductionState vacío (no es error).
     */
    static ProductionState load(const QString& _starcFilePath);

    /**
     * @brief Guardar el estado a disco. Crea la carpeta si no existe.
     *        Atomicidad mínima: escribe a .tmp y rename, evita
     *        archivo corrupto si la app crashea a mitad de write.
     */
    static bool save(const QString& _starcFilePath, const ProductionState& _state);
};

} // namespace BusinessLayer
