#pragma once

#include "image_card.h"

#include <corelib_global.h>


/**
 * @brief Aula 122 — stub mínimo de CharacterImageCard.
 *
 * En STARC oficial esta clase extiende ImageCard con generación automática
 * de avatar a partir de un seed. En este fork la mantenemos como esqueleto
 * mínimo para satisfacer los símbolos que esperan los plugins closed source
 * `character_information`, `characters_relations` y `screenplay_breakdown_structure`
 * (copiados del oficial).
 *
 * Limitación: el botón "generar foto automática" no hace nada. El usuario
 * sigue pudiendo arrastrar/pegar su propia imagen. Todo lo demás del panel
 * Character Info (bio, edad, descripción, relaciones) funciona normal.
 */
class CORE_LIBRARY_EXPORT CharacterImageCard : public ImageCard
{
    Q_OBJECT

public:
    explicit CharacterImageCard(QWidget* _parent = nullptr);
    ~CharacterImageCard() override;

    /**
     * @brief Stub: en el oficial generaba un avatar a partir de un seed (int).
     *        En el fork es no-op. Se preserva la signatura para mantener ABI.
     */
    void generatePhoto(int _seed);

signals:
    /**
     * @brief Stub: signal emitido cuando el usuario pide generar foto.
     *        En el fork nadie la conecta a generación real, pero el plugin
     *        closed source la espera en su staticMetaObject.
     */
    void generatePhotoPressed();
};
