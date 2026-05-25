#include "character_image_card.h"


CharacterImageCard::CharacterImageCard(QWidget* _parent)
    : ImageCard(_parent)
{
}

CharacterImageCard::~CharacterImageCard() = default;

void CharacterImageCard::generatePhoto(int _seed)
{
    Q_UNUSED(_seed)
    //
    // Aula 122 stub: el oficial implementaba generación procedural de avatar.
    // Nuestro fork solo expone la signatura para que los plugins closed source
    // que la esperan puedan cargar. Si se quisiera implementación real:
    // generar un QPixmap aleatorio determinístico desde _seed y llamar setImage().
    //
}
