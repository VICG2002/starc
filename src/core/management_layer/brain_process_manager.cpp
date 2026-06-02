#include "brain_process_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
const int kChromaPort = 8100;

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

        // Modelo: el default materializado por fetch-model.sh, o el primer .gguf
        // que haya en …/brain/models.
        QDir modelsDir(QDir(data).absoluteFilePath(QStringLiteral("models")));
        modelPath = modelsDir.absoluteFilePath(QStringLiteral("qwen2.5_7b.gguf"));
        if (!QFileInfo::exists(modelPath)) {
            const QStringList ggufs
                = modelsDir.entryList({ QStringLiteral("*.gguf") }, QDir::Files);
            if (!ggufs.isEmpty()) {
                modelPath = modelsDir.absoluteFilePath(ggufs.first());
            }
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
    QString modelPath;
    QString chromaDataDir;

    // Procesos gestionados.
    QProcess* chroma = nullptr;
    QProcess* llama = nullptr;
    QProcess* odysseus = nullptr;

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

    const QProcessEnvironment baseEnv = QProcessEnvironment::systemEnvironment();

    //
    // 1) ChromaDB — memoria vectorial, con el python bundleado. Opcional: si el
    //    runtime no trae el binario `chroma` (Fase A instaló chromadb-client),
    //    seguimos sin memoria vectorial en vez de abortar.
    //
    if (QFileInfo(d->chromaBin).isExecutable()) {
        QDir().mkpath(d->chromaDataDir);
        const QStringList args{ QStringLiteral("run"),
                                QStringLiteral("--host"),
                                QStringLiteral("127.0.0.1"),
                                QStringLiteral("--port"),
                                QString::number(kChromaPort),
                                QStringLiteral("--path"),
                                d->chromaDataDir };
        d->chroma = d->spawn(QStringLiteral("chroma"), d->chromaBin, args,
                             d->odysseusRunDir, baseEnv);
    } else {
        emit log(tr("chromadb no está en el runtime; el cerebro corre sin memoria "
                    "vectorial (añadir chromadb a runtime-python, ver PACKAGING Fase C)."));
    }

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
                                QStringLiteral("8192") };
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
    // Orden inverso al arranque: primero el agente, luego sus dependencias.
    d->killProcess(d->odysseus, QStringLiteral("odysseus"));
    d->killProcess(d->llama, QStringLiteral("llama-server"));
    d->killProcess(d->chroma, QStringLiteral("chroma"));
}

} // namespace ManagementLayer
