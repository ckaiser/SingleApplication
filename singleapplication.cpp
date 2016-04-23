#include <QtCore/QDataStream>
#include <QtCore/QSharedMemory>
#include <QtNetwork/QLocalSocket>
#include <QtNetwork/QLocalServer>

#ifdef Q_OS_UNIX
    #include <QtCore/QMutex>

    #include <cstdlib>
    #include <signal.h>
    #include <unistd.h>
#endif

#ifdef Q_OS_WIN
    #include <windows.h>
#endif

#include "singleapplication.h"

class SingleApplicationPrivate
{
    Q_DECLARE_PUBLIC(SingleApplication)

public:
    explicit SingleApplicationPrivate(SingleApplication *q_ptr) : q_ptr(q_ptr), memory(Q_NULLPTR), server(Q_NULLPTR), socket(Q_NULLPTR) {}

    void startServer(QString &serverName)
    {
        Q_Q(SingleApplication);

        // Start a QLocalServer to listen for connections
        server = new QLocalServer();
        QLocalServer::removeServer(serverName);
        server->listen(serverName);
        QObject::connect(server, SIGNAL(newConnection()), q, SLOT(slotConnectionEstablished()));
    }

    void crashHandler()
    {
#ifdef Q_OS_UNIX
        // This guarantees the program will work even with multiple
        // instances of SingleApplication in different threads.
        // Which in my opinion is idiotic, but lets handle that too.
        {
            sharedMemMutex.lock();
            sharedMem.append(memory);
            sharedMemMutex.unlock();
        }
        // Handle any further termination signals to ensure the
        // QSharedMemory block is deleted even if the process crashes
        signal(SIGSEGV, SingleApplicationPrivate::terminate);
        signal(SIGABRT, SingleApplicationPrivate::terminate);
        signal(SIGFPE,  SingleApplicationPrivate::terminate);
        signal(SIGILL,  SingleApplicationPrivate::terminate);
        signal(SIGINT,  SingleApplicationPrivate::terminate);
        signal(SIGTERM, SingleApplicationPrivate::terminate);
#endif
    }

#ifdef Q_OS_UNIX
    static void terminate(int signum)
    {
        while (!sharedMem.empty()) {
            delete sharedMem.back();
            sharedMem.pop_back();
        }
        ::exit(128 + signum);
    }

    static QList<QSharedMemory *> sharedMem;
    static QMutex sharedMemMutex;
#endif

    bool createMutex(const QString& mutexName)
    {
#ifdef Q_OS_WIN
        HANDLE mutexResult = CreateMutexA(NULL, FALSE, mutexName.toStdString().c_str());

        if (mutexResult == NULL) {
            qCritical() << "Couldn't create the application mutex - ERROR:" << GetLastError();
            return false;
        } else {
            return true;
        }
#else
        return true;
#endif
    }

    QSharedMemory *memory;
    SingleApplication *q_ptr;
    QLocalServer  *server;
    QLocalSocket  *socket;
};

#ifdef Q_OS_UNIX
    QList<QSharedMemory *> SingleApplicationPrivate::sharedMem;
    QMutex SingleApplicationPrivate::sharedMemMutex;
#endif

/**
 * @brief Constructor. Checks and fires up LocalServer or closes the program
 * if another instance already exists
 * @param argc
 * @param argv
 */
SingleApplication::SingleApplication(int &argc, char *argv[])
    : app_t(argc, argv), d_ptr(new SingleApplicationPrivate(this))
{
    Q_D(SingleApplication);

    QString serverName = app_t::organizationName() + app_t::applicationName();
    serverName.replace(QRegExp("[^\\w\\-. ]"), "");

    // Guarantee thread safe behaviour with a shared memory block
    d->memory = new QSharedMemory(serverName);

    // Create a shared memory block with a minimum size of 1 byte
    if (d->memory->create(1, QSharedMemory::ReadOnly)) {
        qDebug() << "Created the shared memory:" << serverName;

        // Handle any further termination signals to ensure the
        // QSharedMemory block is deleted even if the process crashes
        d->crashHandler();

        // Successful creation means that no main process exists
        // So we start a Local Server to listen for connections
        d->startServer(serverName);

        // Creating a Windows Mutex, mostly so that other apps (like Inno Installer) can also know about the application's single instance
        bool mutexResult = d->createMutex("Global\\" + serverName.replace(" ", ""));

        if (mutexResult == false) {
            // Quit if we couldn't create the mutex.
            delete d->memory;
            ::exit(EXIT_SUCCESS);
        }
    } else {
        qDebug() << "Couldn't create the shared memory:"  << serverName;

        // Connect to the Local Server of the main process
        // and send the current arguments
        d->socket = new QLocalSocket();
        d->socket->connectToServer(serverName);

        // Even though a shared memory block exists, the original application
        // might have crashed.
        // So only after a successful connection is the second instance
        // terminated.
        if (d->socket->waitForConnected(100)) {
            QByteArray argumentData;

            // Serialize the application arguments
            QDataStream ds(&argumentData, QIODevice::WriteOnly);
            ds << arguments();

            d->socket->write(argumentData);
            d->socket->waitForBytesWritten(200); // Make sure our data is written

            qDebug() << "Terminating after sending data";
            ::exit(EXIT_SUCCESS); // Terminate the program using STDLib's exit function
        } else {
            delete d->memory;
            ::exit(EXIT_SUCCESS);
        }
    }
}

/**
 * @brief Destructor
 */
SingleApplication::~SingleApplication()
{
    Q_D(SingleApplication);

    delete d->memory;
    d->server->close();
}

bool SingleApplication::createMutex(const QString &mutexName)
{
    Q_D(SingleApplication);
    return d->createMutex(mutexName);
}

/**
 * @brief Executed when the new instance connects with the LocalServer
 */
void SingleApplication::slotConnectionEstablished()
{
    Q_D(SingleApplication);

    QLocalSocket *socket = d->server->nextPendingConnection();
    Q_EMIT showUp();

    // Connect the socket's readyRead signal to a lambda that is in charge of
    // grabbing the arguments and emitting the signal that they arrived.
    connect(socket, &QLocalSocket::readyRead, [&, socket] {
        // Grab all the data from the socket
        const QByteArray argumentData = socket->readAll();

        // Deserialize it
        QStringList arguments;
        QDataStream ds(argumentData);
        ds >> arguments;

        Q_EMIT instanceArguments(arguments);

        socket->close();
    });

    // Makes sure we delete the socket object even if we receive no data
    connect(socket, &QLocalSocket::aboutToClose, socket, &QLocalSocket::deleteLater);
}



