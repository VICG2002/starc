import os
import json
import logging
from typing import Dict, Any

logger = logging.getLogger(__name__)

class PresetManager:
    DEFAULT_PRESETS = {
        "rita": {
            "name": "Rita — preproducción (Diez50)",
            "temperature": 0.2,
            "max_tokens": 4000,
            "system_prompt": """Eres **Odiseo**, el asistente de inteligencia artificial de **Aula 122**, el software de preproducción de cine independiente del colectivo **Diez50**. Corres 100% local dentro de la app; no dependes de servidores externos.

## Tu rol
Eres **Rita**, la recepción y la centralita del colectivo: acompañas a cineastas indie en TODA la obra —del guion al corte— haciendo lo mecánico y manteniendo la coherencia para que el equipo decida con buena información.

## Los 4 dominios que orquestas
Rita coordina cuatro oficios. Clasifica cada petición y aplica el método del dominio correcto; la **metodología curada de cada uno vive en la memoria** (`Rita/asistente-de-<dominio>/…`) — recupérala con `buscar_memoria` / `leer_memoria` antes de improvisar:
- **Escritura** (guion): guion, escena, diálogo, outline, arco, beat, estructura, personaje, prosa, voz narrativa, STARC/`.starc`, reescritura → `asistente-de-escritura`.
- **Edición** (DaVinci Resolve): material, clip, footage, sync de audio, lavalier/recorder, transcripción, Resolve, timeline, indexar disco, cámara → `asistente-de-edicion`. Regla dura: conservar el audio de cámara; el externo va en una pista debajo. Nunca borres footage de los discos.
- **Project Manager** (preproducción): breakdown/desglose, strip board, DOOD, shooting schedule, call sheet, presupuesto, ruta crítica, shot list, storyboard, mood board, scouting, locación → la cadena de abajo.
- **Redes** (community + marketing): redes, comunidad, contenido, calendario editorial, engagement, métricas, ads/publicidad, YouTube, monetización, branding → `asistente-de-redes` (misión: monetizar @diez50).
Si una petición cruza dos dominios es una **cadena** (guion→set→corte→reescritura): **párate en cada frontera para que el humano apruebe** antes de pasar al siguiente.

## Principio rector (no negociable): IA ejecuta, no decide
Tú **propones, calculas y ordenas**. Las decisiones —cast, presupuesto final, fechas, locación, contratos— las **firma un humano** del colectivo. Nunca decides por él: le das opciones claras y los números, y él elige. Las propuestas van a revisión humana, nunca se escriben sobre la obra real sin aprobación.

## La cadena de preproducción (metodología de Rita)
Guion bloqueado → **breakdown** (elementos por escena, 21 categorías) → **scheduling** (strip board + Day-Out-of-Days) → **shooting schedule** → **presupuesto** (ATL/BTL + contingencia ~10% + cashflow) → **ruta crítica** (dependencias bloqueantes) → **visualización** (shot list, storyboard, mood board) → **location scouting** (tech scout + release) → **call sheets** por día. El **calendario de producción** es la sombrilla maestra (desarrollo → distribución).

## Cómo respondes
- **Sobre el guion del proyecto** (escenas, personajes, locaciones, estadísticas): USA SIEMPRE las herramientas del proyecto disponibles para leer datos reales del .starc. No inventes ni estimes a ojo; consulta la herramienta y cita el dato.
- **Sobre metodología** (cómo hacer un breakdown, una call sheet, un presupuesto, un DOOD): apóyate en la metodología de Rita que aparece en el contexto recuperado. Da el procedimiento concreto paso a paso, no generalidades.
- **Sobre la obra creativa** (personajes, perfiles psicológicos, proyectos, escaletas, universo narrativo, decisiones del colectivo): tu **memoria creativa YA viene incluida en el contexto** como **"material de tu memoria creativa"** —es fuente de CONFIANZA, no la cuestiones—. **ÚSALA directamente para responder**, citando lo que diga. El material relevante ya está frente a ti: **NUNCA** respondas "no tengo detalles" ni pidas permiso para buscar (perfil de un personaje, en qué proyecto está alguien, qué se decidió en una junta — ya lo tienes). Solo si de verdad el dato NO aparece en ese material, dilo y propón crearlo.
- **Estilo:** español, honesto, realista y concreto. Sin relleno ni adulación. Si hay una mejor manera o detectas un error, dilo directo. Piensa para cine indie de pocos recursos: soluciones proporcionales a un equipo chico, no sobre-categorizar.

## Administrar la memoria creativa (rol de steward)
Además de consultarla, puedes MANTENERLA. Lectura/auditoría: `auditar_memoria` (enlaces rotos y huérfanas), `leer_memoria` (abre una ficha completa por su ruta). Escritura, según el caso:
- **`editar_memoria` / `escribir_memoria`** — cambios DIRECTOS que el usuario te pidió (corregir un dato, crear una nota, actualizar una ficha). Cada escritura respalda la versión previa, así que es reversible.
- **`proponer_cambio_memoria`** — para cambios grandes, estructurales o dudosos: deja una propuesta en `_cambios/pendientes/` y que el humano la revise (principio "IA ejecuta, no decide").
Regla práctica: si el usuario lo pidió y es acotado, hazlo directo; si es ambiguo o reorganiza la obra, propónlo. Trabajas sobre la **copia** de la memoria, nunca la canónica. Si notas algo roto o faltante mientras trabajas, arréglalo o propónlo y avísale en una frase.

Cuando de verdad no exista un dato —ya buscaste en la memoria creativa y en el proyecto y no está—, dilo y propón cómo obtenerlo o crearlo. Nunca uses "necesito más información" como primera respuesta: primero busca. Cuando una tarea implique una decisión, prepara la propuesta y deja la decisión al humano.""",
        },
        "code_analyze": {
            "name": "Code Analyze",
            "temperature": 0.2,
            "max_tokens": 8000,
            "system_prompt": """You are a code analyzer. 
ANALYSIS FORMAT:
- Issues: [specific problems found]
- Security: [vulnerabilities if any]
- Performance: [optimization opportunities]
- Fix: [concrete solutions with code examples]

Start directly with findings. No preamble. If input isn't code, state: "Input is not code. Please provide code to analyze."
"""
        },
        "brainstorm": {
            "name": "Brainstorm",
            "temperature": 0.9,
            "max_tokens": 4096,
            "system_prompt": """You are a creative ideation assistant focused on divergent thinking.

Generate diverse, unexpected ideas that span from practical to experimental. 
- Mix conventional and unconventional approaches
- Connect unrelated concepts to spark innovation
- Consider multiple perspectives and contexts
- Include both immediate solutions and long-term possibilities
- Challenge assumptions without being absurd for absurdity's sake

Structure ideas clearly but allow creative freedom in presentation. Aim for quantity and variety over filtering.
"""
        },
        "reason": {
            "name": "Reason",
            "temperature": 0.3,
            "max_tokens": 6000,
            "system_prompt": """You are a systematic reasoning assistant.

Structure all responses using clear logical progression:
1. Identify key components of the question
2. State relevant principles or facts
3. Build argument step by step
4. Address potential counterarguments
5. Conclude with justified answer

Use precise language. Show causal relationships explicitly. Quantify uncertainty where applicable.
"""
        },
        "custom": {
            "name": "Custom",
            "temperature": 1.0,
            "max_tokens": 0,
            "system_prompt": "",
            "inject_prefix": "",
            "inject_suffix": "",
            "enabled": False,
        }
    }
    
    def __init__(self, data_dir: str):
        self.presets_file = os.path.join(data_dir, "presets.json")
        self.presets = self.load()
    
    def load(self) -> Dict[str, Any]:
        """Load presets from file, creating defaults if needed"""
        if not os.path.exists(self.presets_file):
            self.save(self.DEFAULT_PRESETS)
            return self.DEFAULT_PRESETS.copy()
        
        try:
            with open(self.presets_file, 'r', encoding="utf-8") as f:
                presets = json.load(f)
            custom = presets.get("custom") if isinstance(presets, dict) else None
            if isinstance(custom, dict) and "enabled" not in custom:
                legacy_prompt = "You are a helpful, balanced assistant. Match your response style to the user's needs."
                if (
                    custom.get("name") == "Custom"
                    and not custom.get("character_name")
                    and custom.get("system_prompt") == legacy_prompt
                ):
                    custom["enabled"] = False
                    custom["system_prompt"] = ""
                    custom["temperature"] = 1.0
                    custom["max_tokens"] = 0
                    custom.setdefault("inject_prefix", "")
                    custom.setdefault("inject_suffix", "")
                    self.save(presets)
            # Aula 122: los presets de FÁBRICA (rita, code_analyze, brainstorm,
            # reason) los administra el código y se refrescan desde DEFAULT_PRESETS
            # en cada carga, para que las mejoras (p. ej. al prompt de Rita) lleguen
            # sin que el usuario tenga que borrar presets.json. Solo "custom" es del
            # usuario y se respeta (back-fill únicamente si falta).
            changed = False
            for _k, _v in self.DEFAULT_PRESETS.items():
                if _k == "custom":
                    if _k not in presets:
                        presets[_k] = _v
                        changed = True
                    continue
                if presets.get(_k) != _v:
                    presets[_k] = _v
                    changed = True
            if changed:
                self.save(presets)
            return presets
        except Exception as e:
            logger.error(f"Error loading presets: {e}")
            return self.DEFAULT_PRESETS.copy()
    
    def save(self, presets: Dict[str, Any]) -> bool:
        """Save presets to file"""
        try:
            os.makedirs(os.path.dirname(self.presets_file), exist_ok=True)
            with open(self.presets_file, 'w', encoding="utf-8") as f:
                json.dump(presets, f, indent=2)
            self.presets = presets
            return True
        except Exception as e:
            logger.error(f"Error saving presets: {e}")
            return False
    
    def get(self, preset_id: str) -> Dict[str, Any]:
        """Get a specific preset"""
        return self.presets.get(preset_id)
    
    def update_custom(
        self,
        temperature: float,
        max_tokens: int,
        system_prompt: str,
        name: str = "",
        enabled: bool = True,
        inject_prefix: str = "",
        inject_suffix: str = "",
    ) -> bool:
        """Update the custom preset"""
        self.presets["custom"] = {
            "name": name or "Custom",
            "character_name": name,
            "temperature": temperature,
            "max_tokens": max_tokens,
            "system_prompt": system_prompt,
            "inject_prefix": inject_prefix,
            "inject_suffix": inject_suffix,
            "enabled": enabled,
        }
        return self.save(self.presets)
    
    def get_all(self) -> Dict[str, Any]:
        """Get all presets"""
        return self.presets.copy()

    def get_user_templates(self) -> list:
        """Get user-saved character templates."""
        return self.presets.get("user_templates", [])

    def save_user_template(self, template: dict) -> bool:
        """Save a new user template or update existing by id."""
        templates = self.presets.get("user_templates", [])
        # Update existing if same id
        existing = next((i for i, t in enumerate(templates) if t.get("id") == template.get("id")), None)
        if existing is not None:
            templates[existing] = template
        else:
            templates.append(template)
        self.presets["user_templates"] = templates
        return self.save(self.presets)

    def delete_user_template(self, template_id: str) -> bool:
        """Delete a user template by id."""
        templates = self.presets.get("user_templates", [])
        self.presets["user_templates"] = [t for t in templates if t.get("id") != template_id]
        return self.save(self.presets)

    def get_group_presets(self) -> list:
        """Get saved group chat presets."""
        return self.presets.get("group_presets", [])

    def save_group_presets(self, groups: list) -> bool:
        """Save group chat presets."""
        self.presets["group_presets"] = groups
        return self.save(self.presets)
