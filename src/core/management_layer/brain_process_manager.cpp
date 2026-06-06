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
     * Escribe config.yaml en HERMES_HOME: provider OpenAI local (:8533, el
     * llama-server compartido), contexto 64K (Hermes lo exige) y la plataforma
     * api_server habilitada (su servidor OpenAI en kHermesPort).
     */
    void writeHermesConfig()
    {
        const QString home = hermesHome();
        QDir().mkpath(home);
        const QString model = QFileInfo(modelPath).completeBaseName();
        const QString cfg
            = QStringLiteral("model:\n"
                             "  default: \"%1\"\n"
                             "  provider: \"custom\"\n"
                             "  base_url: \"http://127.0.0.1:%2/v1\"\n"
                             "  api_key: \"sk-local-noauth\"\n"
                             "  context_length: 65536\n"
                             "platforms:\n"
                             "  api_server:\n"
                             "    enabled: true\n")
                  .arg(model)
                  .arg(kLlamaPort);
        QFile f(QDir(home).absoluteFilePath(QStringLiteral("config.yaml")));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(cfg.toUtf8());
            f.close();
        }
    }

    /**
     * Cross-wire (Fase C, lado MCP): registra el MCP server de Hermes
     * (stdio: `hermes mcp serve`) en odysseus, idempotente, con la cookie admin.
     * Espera (acotado) a que el gateway de Hermes esté sano antes de registrar.
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

        // Idempotencia: ¿ya existe un server "hermes"?
        QProcess get;
        get.setProgram(QStringLiteral("/usr/bin/curl"));
        get.setArguments({ QStringLiteral("-s"), QStringLiteral("-m"), QStringLiteral("8"),
                           QStringLiteral("-b"), cookieArg,
                           base + QStringLiteral("/api/mcp/servers") });
        get.setStandardInputFile(QProcess::nullDevice());
        get.start();
        get.waitForFinished(10000);
        if (QString::fromUtf8(get.readAllStandardOutput())
                .contains(QStringLiteral("\"hermes\""))) {
            return;
        }

        const QByteArray argsJson
            = QJsonDocument(QJsonArray{ QStringLiteral("mcp"), QStringLiteral("serve") })
                  .toJson(QJsonDocument::Compact);
        QProcess post;
        post.setProgram(QStringLiteral("/usr/bin/curl"));
        post.setArguments({ QStringLiteral("-s"), QStringLiteral("-m"), QStringLiteral("25"),
                            QStringLiteral("-X"), QStringLiteral("POST"),
                            QStringLiteral("-b"), cookieArg,
                            base + QStringLiteral("/api/mcp/servers"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("name=hermes"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("transport=stdio"),
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("command=") + hermesBin,
                            QStringLiteral("--data-urlencode"),
                            QStringLiteral("args=") + QString::fromUtf8(argsJson) });
        post.setStandardInputFile(QProcess::nullDevice());
        post.start();
        post.waitForFinished(30000);
        emit q->log(QObject::tr("Hermes (MCP) registrado en el cerebro."));
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
        const QStringList args{ QStringLiteral("-m"),
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
                                QStringLiteral("-np"), QStringLiteral("1") };
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
        QProcessEnvironment hEnv = baseEnv;
        hEnv.insert(QStringLiteral("HERMES_HOME"), d->hermesHome());
        hEnv.insert(QStringLiteral("API_SERVER_HOST"), QStringLiteral("127.0.0.1"));
        hEnv.insert(QStringLiteral("API_SERVER_PORT"), QString::number(kHermesPort));
        hEnv.insert(QStringLiteral("API_SERVER_KEY"), d->hermesApiKey());
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
