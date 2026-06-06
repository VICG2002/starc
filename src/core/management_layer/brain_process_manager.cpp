#include "brain_process_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>


namespace ManagementLayer {

namespace {
// Puertos en loopback. llama-server usa uno propio (no 11434) para convivir con
// un Ollama externo durante el desarrollo sin chocar.
const int kOdysseusPort = 7860; // 7860, no 7000 — macOS AirPlay Receiver toma 7000.
const int kLlamaPort = 8533;
const int kHermesPort = 8765; // Hermes gateway (api_server OpenAI-compat), loopback libre.
// (ChromaDB ya no usa puerto: corre EMBEBIDO dentro de odysseus — decisión Fase 2.)

const int kHealthIntervalMs = 1000;
const int kHealthTimeoutMs = 90000; // el primer arranque carga el modelo (varios s)
} // namespace


struct BrainProcessManager::Implementation {
    explicit Implementation(BrainProcessManager* _q)
        : q(_q)
    {
        resolvePaths();
    }

    /** Raíz del bundle del cerebro (binarios read-only). */
    QString brainRoot() const
    {
        //
        // 1) Override explícito (desarrollo): apunta a un brain/ ensamblado a mano.
        //
        const QString fromEnv = qEnvironmentVariable("AULA122_BRAIN_DIR");
        if (!fromEnv.isEmpty()) {
            return fromEnv;
        }
        //
        // 2) Release: <ejecutable>/../Resources/brain dentro del .app.
        //
        QDir dir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MAC
        dir.cdUp(); // Contents/MacOS -> Contents
        dir.cd(QStringLiteral("Resources")); // Contents/Resources
#endif
        return dir.absoluteFilePath(QStringLiteral("brain"));
    }

    /** Raíz de los datos mutables del cerebro (modelos, chromadb). */
    QString dataRoot() const
    {
        // …/Application Support/Diez50/Aula 122/brain
        const QString base
            = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return QDir(base).absoluteFilePath(QStringLiteral("brain"));
    }

    /** Resuelve las rutas y decide si el cerebro está disponible. */
    bool resolvePaths()
    {
        const QString brain = brainRoot();
        pythonBin = QDir(brain).absoluteFilePath(QStringLiteral("python/bin/python3"));
        chromaBin = QDir(brain).absoluteFilePath(QStringLiteral("python/bin/chroma"));
        llamaServerBin
            = QDir(brain).absoluteFilePath(QStringLiteral("llama/bin/llama-server"));
        odysseusDir = QDir(brain).absoluteFilePath(QStringLiteral("odysseus"));

        const QString data = dataRoot();
        chromaDataDir = QDir(data).absoluteFilePath(QStringLiteral("chromadb"));
        odysseusRunDir = QDir(data).absoluteFilePath(QStringLiteral("odysseus-runtime"));
        // Hermes (4º servicio): venv aislado + HERMES_HOME en zona mutable.
        hermesRunDir = QDir(data).absoluteFilePath(QStringLiteral("hermes-runtime"));
        hermesBin
            = QDir(hermesRunDir).absoluteFilePath(QStringLiteral(".venv/bin/hermes"));

        // Modelo: cerebro COMPARTIDO Hermes-3-8B (decisión del test de RAM); si no
        // está, el qwen2.5 previo; si no, el primer .gguf de …/brain/models.
        QDir modelsDir(QDir(data).absoluteFilePath(QStringLiteral("models")));
        const QStringList preferredModels{
            QStringLiteral("Hermes-3-Llama-3.1-8B-Q4_K_M.gguf"),
            QStringLiteral("qwen2.5_14b.gguf")
        };
        modelPath.clear();
        for (const QString& candidate : preferredModels) {
            const QString p = modelsDir.absoluteFilePath(candidate);
            if (QFileInfo::exists(p)) {
                modelPath = p;
                break;
            }
        }
        if (modelPath.isEmpty()) {
            const QStringList ggufs
                = modelsDir.entryList({ QStringLiteral("*.gguf") }, QDir::Files);
            modelPath = ggufs.isEmpty()
                ? modelsDir.absoluteFilePath(
                      QStringLiteral("Hermes-3-Llama-3.1-8B-Q4_K_M.gguf"))
                : modelsDir.absoluteFilePath(ggufs.first());
        }

        // Habilitado si el bundle trae python + llama-server + el código de odysseus.
        // (El modelo puede faltar aún: se verifica al arrancar.)
        enabled = QFileInfo(pythonBin).isExecutable()
            && QFileInfo(llamaServerBin).isExecutable()
            && QFileInfo::exists(odysseusDir + QStringLiteral("/app.py"));
        return enabled;
    }

    /** Lanza un proceso hijo, encaminando su salida a log(). */
    QProcess* spawn(const QString& _label, const QString& _program,
                    const QStringList& _args, const QString& _workdir,
                    const QProcessEnvironment& _env)
    {
        auto* process = new QProcess(q);
        process->setProgram(_program);
        process->setArguments(_args);
        if (!_workdir.isEmpty()) {
            process->setWorkingDirectory(_workdir);
        }
        process->setProcessEnvironment(_env);
        process->setStandardInputFile(QProcess::nullDevice());
        process->setProcessChannelMode(QProcess::MergedChannels);

        QObject::connect(
            process, &QProcess::readyReadStandardOutput, q, [this, process, _label] {
                const QString out
                    = QString::fromUtf8(process->readAllStandardOutput());
                const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString& line : lines) {
                    emit q->log(QStringLiteral("[%1] %2").arg(_label, line.trimmed()));
                }
            });
        QObject::connect(process, &QProcess::errorOccurred, q,
                         [this, _label](QProcess::ProcessError _error) {
                             emit q->log(QStringLiteral("[%1] error de proceso (%2)")
                                             .arg(_label)
                                             .arg(static_cast<int>(_error)));
                         });

        process->start();
        return process;
    }

    /**
     * B3: el cliente C++ lee odysseus_model.json para saber a qué LLM apuntar.
     * Lo fijamos al llama-server local (OpenAI-compatible) que acabamos de lanzar,
     * reemplazando a Ollama.
     */
    void writeOdysseusEndpoint()
    {
        QJsonObject obj;
        obj[QStringLiteral("endpoint_url")]
            = QStringLiteral("http://127.0.0.1:%1/v1/chat/completions").arg(kLlamaPort);
        obj[QStringLiteral("model")] = QStringLiteral("qwen2.5");

        const QString cfgPath
            = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                  .absoluteFilePath(QStringLiteral("odysseus_model.json"));
        QFile file(cfgPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            file.close();
        }
    }

    /**
     * Contraseña del admin local de odysseus. Se genera una vez y se guarda
     * (chmod 600) para que setup.py cree el admin con ella y el login posterior
     * sea determinista. No es un secreto de red: odysseus corre sólo en loopback.
     */
    QString adminPassword()
    {
        const QString path
            = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                  .absoluteFilePath(QStringLiteral("odysseus_admin"));
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QString pw = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
            if (!pw.isEmpty()) {
                return pw;
            }
        }
        const QString pw = QString::number(QRandomGenerator::system()->generate64(), 36)
            + QString::number(QRandomGenerator::system()->generate64(), 36);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(pw.toUtf8());
            f.close();
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
        return pw;
    }

    /**
     * Inicia sesión como admin en el odysseus local y cachea la cookie
     * `odysseus_session` que lee el cliente del Asistente IA. Así el chat (que
     * exige un usuario dueño de la sesión) funciona sin que el usuario tenga que
     * loguearse a mano. Se llama cuando odysseus ya acepta conexiones.
     */
    void cacheAdminSession()
    {
        QProcess curl;
        curl.setProgram(QStringLiteral("/usr/bin/curl"));
        // /api/auth/login espera JSON (un objeto), no form-urlencoded.
        const QByteArray loginJson
            = QJsonDocument(
                  QJsonObject{ { QStringLiteral("username"), QStringLiteral("admin") },
                               { QStringLiteral("password"), adminPassword() } })
                  .toJson(QJsonDocument::Compact);
        curl.setArguments({ QStringLiteral("-s"), QStringLiteral("-i"),
                            QStringLiteral("-m"), QStringLiteral("10"),
                            QStringLiteral("-X"), QStringLiteral("POST"),
                            QStringLiteral("http://127.0.0.1:%1/api/auth/login")
                                .arg(kOdysseusPort),
                            QStringLiteral("-H"),
                            QStringLiteral("Content-Type: application/json"),
                            QStringLiteral("--data-binary"), QString::fromUtf8(loginJson) });
        curl.setStandardInputFile(QProcess::nullDevice());
        curl.start();
        curl.waitForFinished(12000);
        const QString out = QString::fromUtf8(curl.readAllStandardOutput());
        for (const QString& line : out.split(QLatin1Char('\n'))) {
            if (!line.startsWith(QStringLiteral("set-cookie:"), Qt::CaseInsensitive)
                || !line.contains(QStringLiteral("odysseus_session="))) {
                continue;
            }
            const int i = line.indexOf(QStringLiteral("odysseus_session=")) + 17;
            int j = line.indexOf(QLatin1Char(';'), i);
            if (j < 0) {
                j = line.length();
            }
            const QString cookie = line.mid(i, j - i).trimmed();
            if (cookie.isEmpty()) {
                break;
            }
            const QString cookiePath
                = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                      .absoluteFilePath(QStringLiteral("odysseus_session"));
            QFile cf(cookiePath);
            if (cf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                cf.write(cookie.toUtf8());
                cf.close();
                QFile::setPermissions(cookiePath,
                                      QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            }
            emit q->log(QObject::tr("Sesión admin cacheada para el Asistente IA."));
            return;
        }
        emit q->log(QObject::tr("Aviso: no se pudo cachear la sesión admin de odysseus."));
    }

    /**
     * Registra el servidor MCP del proyecto (aula122-mcp) en odysseus para que el
     * agente "vea" el .starc (escenas, personajes…). Idempotente: no duplica si ya
     * existe. Usa la cookie admin recién cacheada; el server.py corre con el python
     * del bundle. Sin AULA122_PROJECT, abre el .starc más reciente (cambiable en
     * caliente con la tool usar_proyecto).
     */
    void registerProjectMcp()
    {
        const QString serverPy
            = QDir(brainRoot()).absoluteFilePath(QStringLiteral("aula122-mcp/server.py"));
        if (!QFileInfo::exists(serverPy)) {
            return;
        }
        QString cookie;
        QFile cf(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                     .absoluteFilePath(QStringLiteral("odysseus_session")));
        if (cf.open(QIODevice::ReadOnly)) {
            cookie = QString::fromUtf8(cf.readAll()).trimmed();
            cf.close();
        }
        if (cookie.isEmpty()) {
            return;
        }
        const QString cookieArg = QStringLiteral("odysseus_session=") + cookie;
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(kOdysseusPort);

        // Idempotencia: ¿ya existe un server "aula122-mcp"?
        QProcess get;
        get.setProgram(QStringLiteral("/usr/bin/curl"));
        get.setArguments({ QStringLiteral("-s"), QStringLiteral("-m"), QStringLiteral("8"),
                           QStringLiteral("-b"), cookieArg,
                           base + QStringLiteral("/api/mcp/servers") });
        get.setStandardInputFile(QProcess::nullDevice());
        get.start();
        get.waitForFinished(10000);
        if (QString::fromUtf8(get.readAllStandardOutput())
                .contains(QStringLiteral("aula122-mcp"))) {
            return;
        }

        const QByteArray argsJson
            = QJsonDocument(QJsonArray{ serverPy }).toJson(QJsonDocument::Compact);
        QProcess post;
        post.setProgram(QStringLiteral("/usr/bin/curl"));
        post.setArguments({ QStringLiteral("-s"), QStringLiteral("-m"), QStringLiteral("25"),
                            QStringLiteral("-X"), QStringLiteral("POST"),
                            QStringLiteral("-b"), cookieArg,
                            base + QStringLiteral("/api/mcp/servers"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("name=aula122-mcp"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("transport=stdio"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("command=") + pythonBin,
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("args=") + QString::fromUtf8(argsJson) });
        post.setStandardInputFile(QProcess::nullDevice());
        post.start();
        post.waitForFinished(30000);
        emit q->log(QObject::tr("Tools del proyecto (aula122-mcp) registradas en el cerebro."));
    }

    /**
     * Clave del api_server de Hermes. Hermes EXIGE API_SERVER_KEY aunque escuche
     * solo en loopback (se niega a arrancar sin ella). Se genera una vez (chmod
     * 600); la usan el gateway (env) y, en Fase C, el ModelEndpoint en odysseus.
     */
    QString hermesApiKey()
    {
        const QString path
            = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                  .absoluteFilePath(QStringLiteral("hermes_api_key"));
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QString k = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
            if (!k.isEmpty()) {
                return k;
            }
        }
        const QString k = QStringLiteral("hk-")
            + QString::number(QRandomGenerator::system()->generate64(), 36)
            + QString::number(QRandomGenerator::system()->generate64(), 36);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(k.toUtf8());
            f.close();
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
        return k;
    }

    /** HERMES_HOME: estado mutable de Hermes (config, skills, cron, kanban). */
    QString hermesHome() const
    {
        return QDir(hermesRunDir).absoluteFilePath(QStringLiteral("home"));
    }

    /**
     * Header para el Notion MCP de Hermes (Diez50): lee NOTION_TOKEN_DIEZ50 de
     * ~/.config/diez50/notion.env y arma el OPENAPI_MCP_HEADERS que espera
     * @notionhq/notion-mcp-server. El token viaja por el ENTORNO del gateway
     * (nunca se escribe a config.yaml). Vacío si falta el archivo o el token.
     */
    QString notionMcpHeaders() const
    {
        QFile f(QDir::homePath()
                + QStringLiteral("/.config/diez50/notion.env"));
        if (!f.open(QIODevice::ReadOnly)) {
            return QString();
        }
        QString token;
        const QList<QByteArray> lines = f.readAll().split('\n');
        f.close();
        for (const QByteArray& line : lines) {
            QString s = QString::fromUtf8(line).trimmed();
            if (s.startsWith(QStringLiteral("export "))) {
                s = s.mid(7).trimmed();
            }
            if (s.startsWith(QStringLiteral("NOTION_TOKEN_DIEZ50="))) {
                token = s.mid(QStringLiteral("NOTION_TOKEN_DIEZ50=").length())
                            .trimmed();
                token.remove(QLatin1Char('"')).remove(QLatin1Char('\''));
                break;
            }
        }
        if (token.isEmpty()) {
            return QString();
        }
        return QStringLiteral(
                   "{\"Authorization\":\"Bearer %1\",\"Notion-Version\":\"2022-06-28\"}")
            .arg(token);
    }

    /** F2: JSON de overrides EDITABLES por el usuario (lo posee el panel del SPA). */
    QString hermesUserSettingsPath() const
    {
        return QDir(hermesRunDir)
            .absoluteFilePath(QStringLiteral("hermes-user-settings.json"));
    }

    /** F2: JSON de SOLO LECTURA que describe lo gestionado (lo muestra el SPA). */
    QString hermesManagedPath() const
    {
        return QDir(hermesRunDir).absoluteFilePath(QStringLiteral("hermes-managed.json"));
    }

    /**
     * F2: lee los ajustes EDITABLES de Hermes desde hermes-user-settings.json (lo
     * escribe el panel del SPA) y los normaliza con defaults seguros. SOLO expone
     * llaves de bajo riesgo: tool_search (auto|on|off), tool_use_enforcement y los
     * toggles de cada MCP. Las llaves GESTIONADAS (modelo, base_url, api_server,
     * rutas de los comandos MCP) NUNCA salen de aquí — las fija writeHermesConfig,
     * que las re-afirma en cada arranque (así el usuario no puede romper el cerebro
     * y las rutas del bundle quedan siempre frescas). Robusto a archivo ausente o
     * corrupto: ante cualquier duda, vuelve al default.
     */
    QJsonObject readHermesUserSettings() const
    {
        QString toolSearch = QStringLiteral("auto");
        bool toolUse = true;
        bool mcpAula = true, mcpMem = true, mcpNotion = true;
        // Default responsivo: solo terminal+file (lo esencial para el tablero).
        QStringList toolsets{ QStringLiteral("terminal"), QStringLiteral("file") };

        QFile f(hermesUserSettingsPath());
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            f.close();
            const QString ts = o.value(QStringLiteral("tool_search")).toString();
            if (ts == QLatin1String("auto") || ts == QLatin1String("on")
                || ts == QLatin1String("off")) {
                toolSearch = ts;
            }
            if (o.value(QStringLiteral("tool_use_enforcement")).isBool()) {
                toolUse = o.value(QStringLiteral("tool_use_enforcement")).toBool();
            }
            const QJsonObject m = o.value(QStringLiteral("mcp_enabled")).toObject();
            if (m.value(QStringLiteral("aula122-mcp")).isBool()) {
                mcpAula = m.value(QStringLiteral("aula122-mcp")).toBool();
            }
            if (m.value(QStringLiteral("memoria-mcp")).isBool()) {
                mcpMem = m.value(QStringLiteral("memoria-mcp")).toBool();
            }
            if (m.value(QStringLiteral("notion")).isBool()) {
                mcpNotion = m.value(QStringLiteral("notion")).toBool();
            }
            // Toolsets internos: lista validada contra el catálogo curado. Si el
            // usuario manda un array (incluso vacío) lo respetamos; vacío = solo MCP.
            const QJsonValue tsv = o.value(QStringLiteral("toolsets"));
            if (tsv.isArray()) {
                QStringList valid;
                const QJsonArray cat = hermesToolsetCatalog();
                for (const QJsonValue& c : cat) {
                    valid << c.toObject().value(QStringLiteral("key")).toString();
                }
                QStringList picked;
                for (const QJsonValue& v : tsv.toArray()) {
                    const QString k = v.toString();
                    if (valid.contains(k) && !picked.contains(k)) {
                        picked << k;
                    }
                }
                toolsets = picked;
            }
        }
        QJsonObject mcp;
        mcp.insert(QStringLiteral("aula122-mcp"), mcpAula);
        mcp.insert(QStringLiteral("memoria-mcp"), mcpMem);
        mcp.insert(QStringLiteral("notion"), mcpNotion);
        QJsonObject out;
        out.insert(QStringLiteral("tool_search"), toolSearch);
        out.insert(QStringLiteral("tool_use_enforcement"), toolUse);
        out.insert(QStringLiteral("mcp_enabled"), mcp);
        out.insert(QStringLiteral("toolsets"), QJsonArray::fromStringList(toolsets));
        return out;
    }

    /**
     * F2: catálogo CURADO de toolsets internos de Hermes que el panel del SPA puede
     * activar/desactivar. NO es el catálogo completo de Hermes (27): es el subconjunto
     * relevante para Diez50/Aula 122. "heavy" marca los que inflan notablemente el
     * prompt (→ primera respuesta más lenta en el 8B local); skills es el peor (~11K
     * tokens). Fuente ÚNICA: la usan readHermesUserSettings (validar) y
     * writeHermesManaged (mostrar) para no desincronizarse. (Los MCP —Notion, bóveda,
     * .starc— NO son toolsets: van por mcp_servers y se togglean aparte.)
     */
    QJsonArray hermesToolsetCatalog() const
    {
        // heavy = infla el prompt (→ respuesta más lenta). risky = CAPACIDAD sensible
        // (ejecución/automatización con efectos en el sistema o la web); el panel la
        // marca aparte para que el usuario no confunda "caro" con "peligroso".
        struct T {
            const char* key;
            const char* label;
            const char* desc;
            bool heavy;
            bool risky;
        };
        static const T items[] = {
            { "terminal", "Terminal y procesos",
              "Ejecuta comandos de shell ARBITRARIOS (git/gh para el tablero de Diez50, "
              "etc.). Capacidad sensible: el agente puede correr cualquier comando.",
              false, true },
            { "file", "Archivos",
              "Leer, escribir, parchar y buscar archivos.", false, false },
            { "todo", "Planificacion de tareas",
              "Lista de pasos para trabajo multi-etapa.", false, false },
            { "delegation", "Delegacion",
              "Delegar subtareas a sub-agentes.", false, false },
            { "session_search", "Buscar conversaciones",
              "Buscar en charlas pasadas de Hermes.", false, false },
            { "cronjob", "Tareas programadas (cron)",
              "Programar acciones recurrentes (p.ej. actualizar Notion/web periodicamente).",
              false, false },
            { "web", "Busqueda web",
              "web_search + extraccion de contenido de paginas.", true, false },
            { "memory", "Memoria persistente",
              "Notas que persisten entre sesiones (se solapa con la boveda).", true, false },
            { "browser", "Navegador (automatizacion)",
              "Control de navegador (navegar, click, escribir): puede ACTUAR en la web "
              "en tu nombre. Capacidad sensible y pesada.", true, true },
            { "vision", "Vision / analisis de imagen",
              "Requiere un modelo con vision (el 8B actual no la tiene).", true, false },
            { "skills", "Skills (workflows reutilizables)",
              "Aprender y reutilizar procedimientos. MUY CARO: ~11K tokens en cada "
              "prompt -> respuestas notablemente mas lentas.", true, false },
        };
        QJsonArray a;
        for (const auto& it : items) {
            QJsonObject o;
            o.insert(QStringLiteral("key"), QString::fromUtf8(it.key));
            o.insert(QStringLiteral("label"), QString::fromUtf8(it.label));
            o.insert(QStringLiteral("desc"), QString::fromUtf8(it.desc));
            o.insert(QStringLiteral("heavy"), it.heavy);
            o.insert(QStringLiteral("risky"), it.risky);
            a.append(o);
        }
        return a;
    }

    /**
     * F2: escribe hermes-managed.json — descriptor de SOLO LECTURA que el panel del
     * SPA muestra para que el usuario VEA (sin poder romper) lo gestionado por el
     * cerebro: modelo compartido, endpoint local, puerto del api_server, si el token
     * de Notion está presente y el catálogo de MCP con su descripción. El SPA jamás
     * escribe este archivo.
     */
    void writeHermesManaged(const QString& model)
    {
        const auto srv = [](const QString& name, const QString& desc, bool needsToken) {
            QJsonObject o;
            o.insert(QStringLiteral("name"), name);
            o.insert(QStringLiteral("desc"), desc);
            o.insert(QStringLiteral("requires_token"), needsToken);
            return o;
        };
        QJsonArray catalog;
        catalog.append(srv(
            QStringLiteral("aula122-mcp"),
            QStringLiteral("Lee el proyecto .starc abierto: escenas, personajes, "
                           "locaciones, desglose, plan de rodaje."),
            false));
        catalog.append(srv(
            QStringLiteral("memoria-mcp"),
            QStringLiteral("Lee y escribe la boveda de memoria-creativa (copia de "
                           "trabajo bajo git)."),
            false));
        catalog.append(srv(
            QStringLiteral("notion"),
            QStringLiteral("Notion de Diez50 (metricas y miembros). Requiere el token "
                           "en ~/.config/diez50/notion.env."),
            true));

        QJsonObject managed;
        managed.insert(QStringLiteral("available"), true);
        managed.insert(QStringLiteral("model"), model);
        managed.insert(QStringLiteral("base_url"),
                       QStringLiteral("http://127.0.0.1:%1/v1").arg(kLlamaPort));
        managed.insert(QStringLiteral("api_server_port"), kHermesPort);
        managed.insert(QStringLiteral("context_length"), 65536);
        managed.insert(QStringLiteral("notion_token_present"),
                       !notionMcpHeaders().isEmpty());
        managed.insert(QStringLiteral("mcp_catalog"), catalog);
        // F2: catálogo de toolsets internos editables (terminal, file, skills…).
        managed.insert(QStringLiteral("toolset_catalog"), hermesToolsetCatalog());

        QFile f(hermesManagedPath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QJsonDocument(managed).toJson(QJsonDocument::Indented));
            f.close();
        }
    }

    /**
     * Escribe config.yaml en HERMES_HOME: provider OpenAI local (:8533, el
     * llama-server compartido), contexto 64K (Hermes lo exige) y la plataforma
     * api_server habilitada (su servidor OpenAI en kHermesPort).
     *
     * F2: las llaves de BAJO RIESGO (tool_search, tool_use_enforcement, toggles de
     * MCP) se toman de readHermesUserSettings() — editables por el panel del SPA.
     * Las GESTIONADAS se re-afirman aqui en cada arranque. Por eso el config.yaml es
     * un ARTEFACTO DERIVADO (no se edita a mano): la fuente de verdad de lo editable
     * es hermes-user-settings.json. Los cambios del panel se aplican al reiniciar.
     */
    void writeHermesConfig()
    {
        const QString home = hermesHome();
        QDir().mkpath(home);
        const QString model = QFileInfo(modelPath).completeBaseName();
        // C-1 (cross-wire): Hermes recibe aula122-mcp (stdio) → lee el .starc
        // (escenas, personajes, desglose, plan…). El server.py corre con el
        // python del bundle (mismo que usa odysseus).
        const QString serverPy
            = QDir(brainRoot()).absoluteFilePath(QStringLiteral("aula122-mcp/server.py"));
        // D-1 (Fase D): Hermes recibe memoria-mcp → lee/escribe/valida la bóveda
        // (sobre la copia de trabajo bajo git; reusa el motor steward de odysseus).
        const QString serverPyMem
            = QDir(brainRoot()).absoluteFilePath(QStringLiteral("memoria-mcp/server.py"));
        // Notion MCP (Diez50): el token va por OPENAPI_MCP_HEADERS (entorno), no aquí.
        const QString npx = QDir::homePath() + QStringLiteral("/.local/bin/npx");
        // Índice RAG real (poblado) que debe usar memoria-mcp para buscar_memoria.
        const QString chromaMcpPath
            = QDir(odysseusRunDir).absoluteFilePath(QStringLiteral("data/chroma"));

        // F2: ajustes EDITABLES por el usuario (panel del SPA). Solo de bajo riesgo.
        const QJsonObject us = readHermesUserSettings();
        const auto yb = [](bool b) {
            return b ? QStringLiteral("true") : QStringLiteral("false");
        };
        // "auto": solo difiere tools tras un umbral de contexto. Con pocas tools (~19),
        // "on" las ESCONDÍA tras tool_search y el 8B no las encontraba; "auto" las
        // muestra directo → las llama nativo. (El usuario puede cambiarlo en el panel.)
        const QString toolSearch = us.value(QStringLiteral("tool_search")).toString();
        const QString tueEnabled
            = yb(us.value(QStringLiteral("tool_use_enforcement")).toBool());
        const QJsonObject mcpEn = us.value(QStringLiteral("mcp_enabled")).toObject();
        const QString aulaEnabled = yb(mcpEn.value(QStringLiteral("aula122-mcp")).toBool());
        const QString memEnabled = yb(mcpEn.value(QStringLiteral("memoria-mcp")).toBool());
        const QString notionEnabled = yb(mcpEn.value(QStringLiteral("notion")).toBool());
        // platform_toolsets.api_server: la lista (editable en el panel) de toolsets
        // internos. Vacía → "[]" (solo MCP). Las claves ya vienen validadas contra el
        // catálogo en readHermesUserSettings, así que no hay inyección posible.
        const QJsonArray tsArr = us.value(QStringLiteral("toolsets")).toArray();
        QString tsBlock;
        if (tsArr.isEmpty()) {
            tsBlock = QStringLiteral("  api_server: []\n");
        } else {
            tsBlock = QStringLiteral("  api_server:\n");
            for (const QJsonValue& v : tsArr) {
                tsBlock += QStringLiteral("  - \"%1\"\n").arg(v.toString());
            }
        }

        QString cfg
            = QStringLiteral("model:\n"
                             "  default: \"%1\"\n"
                             "  provider: \"custom\"\n"
                             "  base_url: \"http://127.0.0.1:%2/v1\"\n"
                             "  api_key: \"sk-local-noauth\"\n"
                             "  context_length: 65536\n"
                             "platforms:\n"
                             "  api_server:\n"
                             "    enabled: true\n"
                             "agent:\n"
                             "  tool_use_enforcement: %7\n"
                             "tools:\n"
                             "  tool_search:\n"
                             "    enabled: \"%8\"\n"
                             // RENDIMIENTO (causa raíz de "Hermes no responde"): por defecto
                             // api_server carga el toolset compuesto completo (skills, browser,
                             // image_gen, memory, web…) → el system prompt se infla a ~19K tokens
                             // (los skills solos son ~11K) y el 8B local tarda ~130s por turno.
                             // El propio Hermes lo documenta: tools de más = "10x latency penalty
                             // on local models". Recortamos a lo esencial: terminal+file (para el
                             // tablero de Diez50 vía gh) — los MCP (Notion/memoria/aula122) NO se
                             // filtran por esto (van por mcp_servers, include_default_mcp_servers).
                             // La lista la edita el usuario en el panel F2 (ya validada).
                             // Se sustituye por centinela (no %12) para NO acoplar el
                             // bloque dinámico a la cadena posicional de .arg().
                             "platform_toolsets:\n"
                             "__TS_BLOCK__"
                             "mcp_servers:\n"
                             "  aula122-mcp:\n"
                             "    command: \"%3\"\n"
                             "    args:\n"
                             "    - \"%4\"\n"
                             "    enabled: %9\n"
                             "  memoria-mcp:\n"
                             "    command: \"%3\"\n"
                             "    args:\n"
                             "    - \"%5\"\n"
                             // CLAVE para que la MEMORIA funcione: el server.py de memoria-mcp
                             // corre con el código de odysseus DEL BUNDLE → su get_chroma_client()
                             // resolvería el índice a <bundle>/odysseus/data/chroma (VACÍO: data/
                             // se excluye al empaquetar) y buscar_memoria daría "sin coincidencias".
                             // Hermes FILTRA el env de los MCP (solo _SAFE_ENV_KEYS + el 'env' del
                             // server), así que pasamos por aquí: CHROMADB_PATH → índice REAL
                             // poblado (odysseus-runtime, 45 MB con la bóveda); EMBEDDING_URL a un
                             // puerto muerto fuerza el MISMO FastEmbed con que se construyó (otro
                             // modelo de embeddings → espacio vectorial distinto → basura).
                             "    env:\n"
                             "      CHROMADB_PATH: \"%12\"\n"
                             "      EMBEDDING_URL: \"http://127.0.0.1:1/v1/embeddings\"\n"
                             "    enabled: %10\n"
                             "  notion:\n"
                             "    command: \"%6\"\n"
                             "    args:\n"
                             "    - \"-y\"\n"
                             "    - \"@notionhq/notion-mcp-server\"\n"
                             "    timeout: 120\n"
                             "    enabled: %11\n")
                  .arg(model)         // %1
                  .arg(kLlamaPort)    // %2
                  .arg(pythonBin)     // %3
                  .arg(serverPy)      // %4
                  .arg(serverPyMem)   // %5
                  .arg(npx)           // %6
                  .arg(tueEnabled)    // %7
                  .arg(toolSearch)    // %8
                  .arg(aulaEnabled)   // %9
                  .arg(memEnabled)    // %10
                  .arg(notionEnabled) // %11
                  .arg(chromaMcpPath);// %12 (env CHROMADB_PATH de memoria-mcp)
        // El bloque de toolsets (dinámico) va por centinela, no por .arg posicional.
        cfg.replace(QStringLiteral("__TS_BLOCK__"), tsBlock);
        QFile f(QDir(home).absoluteFilePath(QStringLiteral("config.yaml")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(cfg.toUtf8());
            f.close();
        }
        // F2: descriptor de solo-lectura para el panel del SPA (modelo, endpoint,
        // catálogo MCP). Va junto al config; lo lee /api/hermes/settings.
        writeHermesManaged(model);
    }

    /**
     * D-2 (Fase D): "entrenar" a Hermes en el formato de la bóveda SIN fine-tuning.
     * Escribe un AGENTS.md en el CWD de Hermes (hermesRunDir) — Hermes lo auto-inyecta
     * como instrucciones, igual que CLAUDE.md. Codifica identidad + workflow de
     * escritura (leer plantilla → validar → escribir) + reglas (sin-emojis, prefijos,
     * copia-es-git) + cross-links .starc↔ficha↔Rita + el principio rector de Diez50.
     */
    void writeHermesAgentsMd()
    {
        const QString md = QStringLiteral(
            "# Hermes en Aula 122 / Diez50\n\n"
            "Eres Hermes, el agente autónomo del software de cine indie Aula 122 (colectivo\n"
            "Diez50). Compartes el modelo local con Odiseo y tienes dos juegos de tools MCP:\n\n"
            "- **aula122-mcp** (lee el proyecto .starc abierto): listar_escenas, obtener_escena,\n"
            "  listar_personajes, obtener_personaje, listar_locaciones, generar_desglose,\n"
            "  generar_plan_rodaje, etc. Úsalas para VER el guion real antes de escribir.\n"
            "- **memoria-mcp** (la bóveda de memoria-creativa, copia de trabajo bajo git):\n"
            "  buscar_memoria, leer_memoria, escribir_memoria, editar_memoria,\n"
            "  proponer_cambio_memoria, auditar_memoria, validar_formato_memoria.\n\n"
            "## Cómo escribir en la bóveda (formato establecido — OBLIGATORIO)\n\n"
            "1. Antes de crear una ficha de tipo X, LEE su plantilla:\n"
            "   leer_memoria(\"_templates/ficha-X.md\") (existen ficha-proyecto, ficha-personaje,\n"
            "   ficha-lugar, ficha-colaborador, perfil-psicologico, etc.).\n"
            "2. Respeta SIEMPRE:\n"
            "   - Frontmatter YAML con `tipo:` y `slug:` (según la plantilla del tipo).\n"
            "   - Prefijo de naming en el archivo: Tales-, EDLP-, HDUHSP-, Diez50-, Autor-\n"
            "     (según el proyecto/nivel de la ficha).\n"
            "   - NADA de emojis (regla perpetua de la bóveda).\n"
            "   - Lenguaje sencillo; enlaces con ruta relativa (no inventes enlaces rotos).\n"
            "3. VALIDA antes de escribir: validar_formato_memoria(contenido=...). Si hay issues,\n"
            "   corrígelos. Solo entonces escribir_memoria(path, contenido).\n"
            "4. Cada escritura deja auto-backup y queda versionada en git (reversible).\n\n"
            "## Conectar proyecto ↔ memoria ↔ Rita\n\n"
            "Al documentar un proyecto abierto: identifícalo con aula122-mcp, resuelve su ficha\n"
            "de proyecto por slug/prefijo, y enlaza la ficha con el .starc y con el método de\n"
            "dominio de Rita (escritura/edición/PM/redes) que corresponda.\n\n"
            "## Tablero de Diez50 (Notion + web) — REGLAS DE ALCANCE ESTRICTAS\n\n"
            "Tienes un MCP de Notion (tools `mcp_notion_*`) sobre el espacio 'Diez50 - Centro'\n"
            "y acceso a la terminal + `gh` (autenticado como VICG2002) sobre el repo del sitio\n"
            "publicado en `~/rita-tablero`.\n\n"
            "- **SOLO escribe en las bases COLECTIVAS: `metricas` y `miembros`** (foto, proximo\n"
            "  video, metricas). Las demas bases (`proyectos`, `tareas`, `ideas`, `en_progreso`,\n"
            "  `calendario`) son **sync-fed**: su fuente de verdad es la memoria local y el `/sync`\n"
            "  las SOBREESCRIBE -> NO las edites (tu cambio se perderia).\n"
            "- **NUNCA archives ni borres** paginas ni bases de Notion. Solo crear/actualizar\n"
            "  propiedades en las bases permitidas.\n"
            "- **Web del tablero** (`~/rita-tablero`, repo PUBLICO): el sitio se regenera desde\n"
            "  Notion cada 6h (GitHub Actions), asi que para cambios de DATOS edita Notion (se\n"
            "  propaga solo). Toca el repo SOLO para plantilla/HTML; `git push` a main publica\n"
            "  DIRECTO a produccion. Antes de push: revisa el diff y NUNCA commitees secretos.\n\n"
            "## Principio rector (Diez50): la IA ejecuta, no decide\n\n"
            "Tienes autonomía para crear y actualizar fichas directamente. Para reorganización\n"
            "ESTRUCTURAL delicada (mover/renombrar/borrar, o tocar la carpeta Rita), si dudas,\n"
            "usa proponer_cambio_memoria para que Victor lo revise.\n");
        QFile f(QDir(hermesRunDir).absoluteFilePath(QStringLiteral("AGENTS.md")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(md.toUtf8());
            f.close();
        }
    }

    /**
     * Cross-wire (Fase C): registra el api_server de Hermes como ModelEndpoint
     * en odysseus (delegación Odiseo→Hermes), idempotente, con la cookie admin.
     * Espera (acotado) a que el gateway de Hermes esté sano antes de registrar.
     * (Hermes recibe las tools de Odiseo vía mcp_servers en su config: writeHermesConfig.)
     */
    void registerHermes()
    {
        if (hermesBin.isEmpty() || !QFileInfo(hermesBin).isExecutable()) {
            return;
        }
        bool up = false;
        for (int i = 0; i < 16 && !up; ++i) {
            QTcpSocket socket;
            socket.connectToHost(QStringLiteral("127.0.0.1"), kHermesPort);
            up = socket.waitForConnected(500);
            socket.abort();
        }
        if (!up) {
            emit q->log(QObject::tr("Aviso: Hermes no respondió en :%1; sin registrar.")
                            .arg(kHermesPort));
            return;
        }
        QString cookie;
        QFile cf(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                     .absoluteFilePath(QStringLiteral("odysseus_session")));
        if (cf.open(QIODevice::ReadOnly)) {
            cookie = QString::fromUtf8(cf.readAll()).trimmed();
            cf.close();
        }
        if (cookie.isEmpty()) {
            return;
        }
        const QString cookieArg = QStringLiteral("odysseus_session=") + cookie;
        const QString base = QStringLiteral("http://127.0.0.1:%1").arg(kOdysseusPort);

        // Idempotencia: ¿ya existe el endpoint "hermes-agent"?
        QProcess get;
        get.setProgram(QStringLiteral("/usr/bin/curl"));
        get.setArguments({ QStringLiteral("-s"), QStringLiteral("-m"), QStringLiteral("8"),
                           QStringLiteral("-b"), cookieArg,
                           base + QStringLiteral("/api/model-endpoints") });
        get.setStandardInputFile(QProcess::nullDevice());
        get.start();
        get.waitForFinished(10000);
        if (QString::fromUtf8(get.readAllStandardOutput())
                .contains(QStringLiteral("hermes-agent"))) {
            return;
        }

        // C-2b: registrar el api_server de Hermes como ModelEndpoint (delegación
        // Odiseo→Hermes). skip_probe: el endpoint exige Bearer; lo marcamos
        // tool-capable para que un preset de Odiseo pueda delegarle tareas.
        QProcess post;
        post.setProgram(QStringLiteral("/usr/bin/curl"));
        post.setArguments({ QStringLiteral("-s"), QStringLiteral("-m"), QStringLiteral("25"),
                            QStringLiteral("-X"), QStringLiteral("POST"),
                            QStringLiteral("-b"), cookieArg,
                            base + QStringLiteral("/api/model-endpoints"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("name=hermes-agent"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("base_url=http://127.0.0.1:%1/v1")
                                .arg(kHermesPort),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("api_key=") + hermesApiKey(),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("supports_tools=true"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("skip_probe=true"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("shared=true") });
        post.setStandardInputFile(QProcess::nullDevice());
        post.start();
        post.waitForFinished(30000);
        emit q->log(QObject::tr("Hermes registrado como ModelEndpoint (delegación)."));
    }

    /**
     * Sincroniza el código de odysseus desde el bundle (read-only dentro del .app
     * firmado) a una copia MUTABLE en datos de usuario, donde odysseus sí puede
     * escribir su data dir (DB, uploads, chroma…). rsync idempotente; preserva
     * data/. Permite que el cerebro funcione con el .app firmado/read-only.
     */
    void syncOdysseusRuntime()
    {
        QDir().mkpath(odysseusRunDir);
        QProcess rsync;
        rsync.setProgram(QStringLiteral("/usr/bin/rsync"));
        rsync.setArguments({ QStringLiteral("-a"), QStringLiteral("--delete"),
                             QStringLiteral("--exclude=data"),
                             QStringLiteral("--exclude=__pycache__"),
                             odysseusDir + QStringLiteral("/"),
                             odysseusRunDir + QStringLiteral("/") });
        rsync.setStandardInputFile(QProcess::nullDevice());
        rsync.start();
        rsync.waitForFinished(60000);
    }

    /**
     * Primer arranque: crea el data dir de odysseus (DB SQLite, subdirectorios y
     * usuario admin) corriendo su setup.py con el python bundleado. Idempotente:
     * no hace nada si la DB ya existe. Síncrono — setup tarda ~1-2 s y solo ocurre
     * la primera vez.
     */
    void ensureOdysseusData()
    {
        if (QFileInfo::exists(odysseusRunDir + QStringLiteral("/data/app.db"))) {
            return;
        }
        emit q->log(QObject::tr("Primer arranque: inicializando datos del cerebro…"));
        QProcess setup;
        setup.setProgram(pythonBin);
        setup.setArguments({ QStringLiteral("setup.py") });
        setup.setWorkingDirectory(odysseusRunDir);
        QProcessEnvironment setupEnv = QProcessEnvironment::systemEnvironment();
        setupEnv.insert(QStringLiteral("ODYSSEUS_ADMIN_PASSWORD"), adminPassword());
        setup.setProcessEnvironment(setupEnv);
        setup.setStandardInputFile(QProcess::nullDevice());
        setup.start();
        setup.waitForFinished(30000);
    }

    /**
     * Siembra el conocimiento de Rita (Fase 2) en la data mutable: el índice RAG
     * embebido (data/chroma) y el modelo de embeddings local (data/fastembed_cache),
     * copiados desde <brain>/seed/. Copia-si-falta (idempotente): nunca pisa datos
     * ya presentes del usuario. Sin seed (build viejo) no hace nada.
     */
    void seedBrainKnowledge()
    {
        const QString seedDir = QDir(brainRoot()).absoluteFilePath(QStringLiteral("seed"));
        if (!QFileInfo::exists(seedDir)) {
            return;
        }
        const QString dataDir = odysseusRunDir + QStringLiteral("/data");
        QDir().mkpath(dataDir);
        const struct {
            const char* sub;
            const char* marker;
        } items[] = {
            { "chroma", "chroma/chroma.sqlite3" },
            { "fastembed_cache", "fastembed_cache" },
        };
        for (const auto& it : items) {
            const QString sub = QString::fromLatin1(it.sub);
            const QString src = QDir(seedDir).absoluteFilePath(sub);
            if (!QFileInfo::exists(src)) {
                continue;
            }
            if (QFileInfo::exists(dataDir + QStringLiteral("/") + QString::fromLatin1(it.marker))) {
                continue; // ya sembrado o el usuario ya tiene datos propios
            }
            QProcess rsync;
            rsync.setProgram(QStringLiteral("/usr/bin/rsync"));
            rsync.setArguments({ QStringLiteral("-a"), src + QStringLiteral("/"),
                                 QDir(dataDir).absoluteFilePath(sub) + QStringLiteral("/") });
            rsync.setStandardInputFile(QProcess::nullDevice());
            rsync.start();
            rsync.waitForFinished(120000);
            emit q->log(QObject::tr("Conocimiento de Rita sembrado: %1.").arg(sub));
        }
    }

    void startHealthCheck()
    {
        healthElapsedMs = 0;
        if (healthTimer == nullptr) {
            healthTimer = new QTimer(q);
            QObject::connect(healthTimer, &QTimer::timeout, q, [this] {
                healthElapsedMs += kHealthIntervalMs;
                QTcpSocket socket;
                socket.connectToHost(QStringLiteral("127.0.0.1"), kOdysseusPort);
                const bool up = socket.waitForConnected(500);
                socket.abort();
                if (up) {
                    healthTimer->stop();
                    cacheAdminSession();
                    registerProjectMcp();
                    registerHermes();
                    emit q->log(QStringLiteral("Cerebro listo (odysseus en :%1).")
                                    .arg(kOdysseusPort));
                    emit q->ready();
                    return;
                }
                if (healthElapsedMs >= kHealthTimeoutMs) {
                    healthTimer->stop();
                    emit q->failed(QObject::tr("El cerebro no respondió en %1 s.")
                                       .arg(kHealthTimeoutMs / 1000));
                }
            });
        }
        healthTimer->start(kHealthIntervalMs);
    }

    void killProcess(QProcess*& _process, const QString& _label)
    {
        if (_process == nullptr) {
            return;
        }
        if (_process->state() != QProcess::NotRunning) {
            emit q->log(QStringLiteral("Deteniendo %1…").arg(_label));
            _process->terminate();
            if (!_process->waitForFinished(3000)) {
                _process->kill();
                _process->waitForFinished(1000);
            }
        }
        _process->deleteLater();
        _process = nullptr;
    }

    BrainProcessManager* q = nullptr;

    bool enabled = false;

    // Rutas resueltas.
    QString pythonBin;
    QString chromaBin;
    QString llamaServerBin;
    QString odysseusDir;     // código de odysseus en el bundle (read-only en el .app firmado)
    QString odysseusRunDir;  // copia mutable donde odysseus corre y escribe su data
    QString hermesRunDir;    // venv aislado + HERMES_HOME de Hermes (skills/cron/kanban)
    QString hermesBin;       // <hermesRunDir>/.venv/bin/hermes
    QString modelPath;
    QString chromaDataDir;

    // Procesos gestionados.
    QProcess* chroma = nullptr;
    QProcess* llama = nullptr;
    QProcess* odysseus = nullptr;
    QProcess* hermes = nullptr;

    // Health-check.
    QTimer* healthTimer = nullptr;
    int healthElapsedMs = 0;
};


BrainProcessManager::BrainProcessManager(QObject* _parent)
    : QObject(_parent)
    , d(new Implementation(this))
{
}

BrainProcessManager::~BrainProcessManager()
{
    stopAll();
}

bool BrainProcessManager::isEnabled() const
{
    return d->enabled;
}

void BrainProcessManager::startAll()
{
    if (!d->enabled) {
        emit log(tr("Cerebro local no disponible (no se encontró el bundle 'brain'); "
                    "el Asistente IA quedará en modo externo."));
        return;
    }
    if (!QFileInfo::exists(d->modelPath)) {
        emit failed(tr("Modelo no encontrado en %1. Materialízalo con fetch-model.sh.")
                        .arg(d->modelPath));
        return;
    }

    //
    // Copiar el código de odysseus a zona mutable (el .app es read-only tras
    // firmar) y crear su data dir en el primer arranque.
    //
    d->syncOdysseusRuntime();
    d->ensureOdysseusData();
    d->seedBrainKnowledge();

    QProcessEnvironment baseEnv = QProcessEnvironment::systemEnvironment();
    //
    // PATH + caché HF para el Cookbook de odysseus (descarga/serve de modelos).
    // Dentro del .app (abierto desde Finder) el PATH es el mínimo de macOS: NO
    // trae Homebrew ni el bin del python bundleado. El Cookbook descarga modelos
    // corriendo `hf download` dentro de `tmux`, así que necesita resolver AMBOS
    // binarios. Anteponemos:
    //   • <brain>/python/bin            → `hf` / `huggingface-cli` / `python3`
    //   • /opt/homebrew/bin, /usr/local/bin → `tmux`, `git`, etc. del sistema
    // y fijamos HF_HOME al directorio mutable del cerebro para que lo descargado
    // quede autocontenido y lo reusen el llama-server y el servidor de imágenes.
    // (Todo en loopback; el server nunca se expone a la red.)
    {
        const QString venvBin = QFileInfo(d->pythonBin).absolutePath();
        const QString curPath = baseEnv.value(QStringLiteral("PATH"));
        QStringList parts{ venvBin, QStringLiteral("/opt/homebrew/bin"),
                           QStringLiteral("/usr/local/bin") };
        if (!curPath.isEmpty()) {
            parts << curPath;
        }
        baseEnv.insert(QStringLiteral("PATH"), parts.join(QLatin1Char(':')));
        const QString hfHome
            = QDir(d->dataRoot()).absoluteFilePath(QStringLiteral("huggingface"));
        QDir().mkpath(hfHome);
        baseEnv.insert(QStringLiteral("HF_HOME"), hfHome);
    }

    //
    // 1) ChromaDB — memoria vectorial. Aula 122 usa ChromaDB EMBEBIDO dentro de
    //    odysseus (PersistentClient sobre data/chroma): NO se lanza servidor ni se
    //    ocupa el puerto 8100 (decisión Fase 2 — más rápido, menos RAM y 100%
    //    autocontenido). El índice de Rita se siembra en seedBrainKnowledge().
    //
    emit log(tr("ChromaDB embebido (sin servidor): memoria vectorial dentro de odysseus."));

    //
    // 2) llama-server — runtime de modelo en loopback (Metal vía -ngl).
    //
    {
        // Tool-calling FIABLE (causa raíz hallada): sin --jinja, llama.cpp NO activa
        // el function-calling → el modelo emite la llamada como TEXTO y el gateway
        // Hermes la descarta (solo ejecuta tool_calls NATIVOS). --jinja + el template
        // tool_use de Hermes-3 + temp baja (la receta probada de Odiseo) fuerzan
        // tool_calls nativos con JSON válido.
        const QString toolTemplate
            = QDir(d->brainRoot())
                  .absoluteFilePath(QStringLiteral("llama/hermes-tool_use.jinja"));
        QStringList args{ QStringLiteral("-m"),
                                d->modelPath,
                                QStringLiteral("--host"),
                                QStringLiteral("127.0.0.1"),
                                QStringLiteral("--port"),
                                QString::number(kLlamaPort),
                                QStringLiteral("-ngl"),
                                QStringLiteral("99"),
                                QStringLiteral("-c"),
                                // 16384 (antes 8192). CAUSA RAÍZ de "no funciona bien": en modo
                                // agente el prompt de una ronda (preset rita + inyección RAG de
                                // ~8 fragmentos + ~12 esquemas de tools MCP + historial + resultado
                                // de tool) supera fácil los 8192 tokens → llama-server devolvía
                                // HTTP 400 "exceeds context size" → el loop caía al fallback en la
                                // nube (Gemini) que a su vez fallaba por thought_signature → respuesta
                                // vacía. Qwen2.5-7B admite 32k nativo; 16384 da holgura y cabe en 18 GB
                                // (7B≈5.7 GB, 14b≈11.5 GB con KV fp16). -fa on mantiene el KV chico.
                                // 65536 (antes 16384): Hermes EXIGE contexto >=64K y
                                // Odiseo lo comparte. El KV grande se contiene con -np 1
                                // (abajo): un solo slot → KV = 1×64K, no 4×. Cabe en 18 GB
                                // con Hermes-3-8B (~4.6 GB pesos + KV q8_0).
                                QStringLiteral("65536"),
                                //
                                // M3 / 18 GB: -fa on = flash attention en Metal (más rápido + menos
                                // RAM de KV-cache). NOTA: --mlock se QUITÓ — forzar el modelo residente
                                // colgaba la carga cuando la RAM estaba presionada.
                                //
                                QStringLiteral("-fa"),
                                QStringLiteral("on"),
                                // KV-cache en q8_0 = la MITAD de RAM que fp16, con calidad casi
                                // idéntica. Hace viable el Qwen2.5-14b@16384 en 18 GB (~10 GB vs
                                // ~11.6) y deja más holgura al 7B. Requiere -fa on (arriba).
                                QStringLiteral("-ctk"), QStringLiteral("q8_0"),
                                QStringLiteral("-ctv"), QStringLiteral("q8_0"),
                                // -np 1: un solo slot de contexto → KV = 1×64K (no 4×).
                                // Decisivo para que Hermes-3-8B@64K quepa en 18 GB.
                                QStringLiteral("-np"), QStringLiteral("1"),
                                // -ub 1024 (micro-batch; default 512): el agent loop de Hermes
                                // manda prompts ENORMES (esquemas de tools + contexto), p.ej. ~19K
                                // tokens. El cuello de botella es el PROCESAMIENTO de prompt, no la
                                // generación. Subir el micro-batch ~duplica el throughput de prompt
                                // en Metal (M3) → la primera respuesta baja de ~130s a la mitad. -b
                                // 2048 (default) es el batch lógico; el coste extra de RAM del
                                // ubatch mayor es modesto (~cientos de MB) y cabe en 18 GB.
                                QStringLiteral("-b"), QStringLiteral("2048"),
                                QStringLiteral("-ub"), QStringLiteral("1024"),
                                // --jinja: activa el function-calling de llama.cpp (formato
                                // <tools>, parser de <tool_call>, gramática lazy que fuerza
                                // JSON válido) → tool_calls NATIVOS, no texto.
                                QStringLiteral("--jinja"),
                                // temp baja + top-p: tool-calling determinista (receta Odiseo:
                                // a temp alta los tokens de tool_call se corrompen a texto).
                                QStringLiteral("--temp"), QStringLiteral("0.2"),
                                QStringLiteral("--top-p"), QStringLiteral("0.9") };
        if (QFileInfo::exists(toolTemplate)) {
            // El GGUF de Hermes-3 solo embebe ChatML plano; este template trae la
            // lógica <tools>/<tool_call> correcta de Hermes.
            args << QStringLiteral("--chat-template-file") << toolTemplate;
        }
        d->llama = d->spawn(QStringLiteral("llama"), d->llamaServerBin, args, QString(),
                            baseEnv);
    }

    //
    // 3) odysseus — uvicorn app:app con el python bundleado. LOCALHOST_BYPASS deja
    //    que las peticiones de loopback (este mismo host) no requieran login: la
    //    app es single-user y el server nunca se expone a la red (ver app.py).
    //
    {
        QProcessEnvironment env = baseEnv;
        env.insert(QStringLiteral("ODYSSEUS_PORT"), QString::number(kOdysseusPort));
        // Embeddings DETERMINISTAS y autocontenidos (Fase 2): el índice RAG se
        // construyó con FastEmbed local (all-MiniLM-L6-v2). Apuntamos EMBEDDING_URL
        // a un puerto cerrado para que el probe HTTP (Ollama :11434 por defecto)
        // fast-falle y odysseus use SIEMPRE FastEmbed — mismo espacio vectorial que
        // el índice, sin depender de un Ollama externo. (Un endpoint que el usuario
        // configure en Ajustes se persiste aparte y tiene prioridad sobre esto.)
        env.insert(QStringLiteral("EMBEDDING_URL"),
                   QStringLiteral("http://127.0.0.1:1/v1/embeddings"));
        // F2: el panel de ajustes de Hermes (SPA) lee/escribe los dos JSON aquí
        // (hermes-managed.json solo-lectura + hermes-user-settings.json editable).
        env.insert(QStringLiteral("HERMES_RUNTIME_DIR"), d->hermesRunDir);
        const QStringList args{ QStringLiteral("-m"),
                                QStringLiteral("uvicorn"),
                                QStringLiteral("app:app"),
                                QStringLiteral("--host"),
                                QStringLiteral("127.0.0.1"),
                                QStringLiteral("--port"),
                                QString::number(kOdysseusPort) };
        d->odysseus = d->spawn(QStringLiteral("odysseus"), d->pythonBin, args,
                               d->odysseusRunDir, env);
    }

    //
    // 4) Hermes — 4º servicio: su gateway expone el api_server OpenAI en
    //    kHermesPort y corre cron + kanban + skills, todo como cliente del mismo
    //    llama-server :8533. Estado mutable (HERMES_HOME, venv) en hermes-runtime.
    //    Solo si el venv está materializado; si no, el cerebro corre sin Hermes.
    //
    if (QFileInfo(d->hermesBin).isExecutable()) {
        d->writeHermesConfig();
        d->writeHermesAgentsMd();
        QProcessEnvironment hEnv = baseEnv;
        hEnv.insert(QStringLiteral("HERMES_HOME"), d->hermesHome());
        hEnv.insert(QStringLiteral("API_SERVER_HOST"), QStringLiteral("127.0.0.1"));
        hEnv.insert(QStringLiteral("API_SERVER_PORT"), QString::number(kHermesPort));
        hEnv.insert(QStringLiteral("API_SERVER_KEY"), d->hermesApiKey());
        // "Fix profundo" (decisión del usuario): PIN de los MCP esenciales en tool_search
        // → memoria-creativa (mcp_memoria_mcp_*) y lectura del .starc (mcp_aula122_mcp_*)
        // NUNCA se difieren, así el 8B local los usa de forma fiable AUNQUE Notion (~22
        // tools) esté activo y dispare el deferral del resto. Lo lee el patch de
        // ai/hermes/tools/tool_search.py (_pinned_substrings). Notion queda diferido tras
        // tool_search (uso ocasional); memoria + guion siempre visibles (uso diario).
        hEnv.insert(QStringLiteral("HERMES_TOOL_SEARCH_PIN"),
                    QStringLiteral("mcp_memoria_mcp_,mcp_aula122_mcp_"));
        // npx (para el Notion MCP) en el PATH del gateway.
        hEnv.insert(QStringLiteral("PATH"),
                    QDir::homePath() + QStringLiteral("/.local/bin:")
                        + hEnv.value(QStringLiteral("PATH")));
        // Token de Notion (Diez50) por el ENTORNO del gateway → lo hereda el
        // subproceso del Notion MCP. NO se escribe a config.yaml.
        const QString notionHeaders = d->notionMcpHeaders();
        if (!notionHeaders.isEmpty()) {
            hEnv.insert(QStringLiteral("OPENAPI_MCP_HEADERS"), notionHeaders);
        }
        const QStringList hArgs{ QStringLiteral("gateway"), QStringLiteral("run"),
                                 QStringLiteral("-q") };
        d->hermes = d->spawn(QStringLiteral("hermes"), d->hermesBin, hArgs,
                             d->hermesRunDir, hEnv);
        emit log(tr("Arrancando Hermes (4º servicio, gateway api_server :%1)…")
                     .arg(kHermesPort));
    } else {
        emit log(tr("Hermes no materializado (sin venv); el cerebro corre sin el 4º servicio."));
    }

    // B3: apuntar el cliente IA al llama-server local.
    d->writeOdysseusEndpoint();

    // Esperar (sin bloquear) a que odysseus acepte conexiones.
    d->startHealthCheck();
    emit log(tr("Arrancando cerebro local (llama-server :%1, odysseus :%2)…")
                 .arg(kLlamaPort)
                 .arg(kOdysseusPort));
}

void BrainProcessManager::stopAll()
{
    if (d->healthTimer != nullptr) {
        d->healthTimer->stop();
    }
    // Orden inverso al arranque: primero los agentes, luego sus dependencias.
    d->killProcess(d->hermes, QStringLiteral("hermes"));
    d->killProcess(d->odysseus, QStringLiteral("odysseus"));
    d->killProcess(d->llama, QStringLiteral("llama-server"));
    d->killProcess(d->chroma, QStringLiteral("chroma"));
}

} // namespace ManagementLayer
