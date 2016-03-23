#include "singleapplication.h"
#include <QSharedMemory>
#include <QLocalSocket>
#include <QLocalServer>
#include <QDataStream>

#ifdef Q_OS_UNIX
    #include <QMutex>
    #include <cstdlib>

    #include <signal.h>
    #include <unistd.h>
#endif

#ifdef Q_OS_WIN
    #include <windows.h>
#endif

class SingleApplicationPrivate
{
public:
    SingleApplicationPrivate(SingleApplication *q_ptr) : q_ptr(q_ptr) { }

    void startServer(QString &serverName)
    {
        // Start a QLocalServer to listen for connections
        server = new QLocalServer();
        server->removeServer(serverName);
        server->listen(serverName);
        QObject::connect(server, SIGNAL(newConnection()), q_ptr, SLOT(slotConnectionEstablished()));
    }

#ifdef Q_OS_UNIX
    void crashHandler()
    {
        // This guarantees the program will work even with multiple
        // instances of SingleApplication in different threads
        // Which in my opinion is idiotic, but lets handle that too
        {
            sharedMemMutex.lock();
            sharedMem.append(memory);
            sharedMemMutex.unlock();
        }
        // Handle any further termination signals to ensure the
        // QSharedMemory block is deleted even if the process crashes
        signal(SIGSEGV, SingleApplicationPrivate::terminate);
        signal(SIGABRT, SingleApplicationPrivate::terminate);
        signal(SIGFPE, SingleApplicationPrivate::terminate);
        signal(SIGILL, SingleApplicationPrivate::terminate);
        signal(SIGINT, SingleApplicationPrivate::terminate);
        signal(SIGTERM, SingleApplicationPrivate::terminate);
    }

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
    : QAPPLICATION_CLASS(argc, argv), d_ptr(new SingleApplicationPrivate(this))
{
    QString serverName = QAPPLICATION_CLASS::organizationName() + QAPPLICATION_CLASS::applicationName();
    serverName.replace(QRegExp("[^\\w\\-. ]"), "");

    // Garantee thread safe behaviour with a shared memory block
    d_ptr->memory = new QSharedMemory(serverName);

    // Create a shared memory block with a minimum size of 1 byte
    if (d_ptr->memory->create(1, QSharedMemory::ReadOnly)) {
#ifdef Q_OS_UNIX
        // Handle any further termination signals to ensure the
        // QSharedMemory block is deleted even if the process crashes
        d_ptr->crashHandler();
#endif
        // Successful creation means that no main process exists
        // So we start a Local Server to listen for connections
        d_ptr->startServer(serverName);
    } else {
        // Connect to the Local Server of the main process
        // and send the current arguments
        d_ptr->socket = new QLocalSocket();
        d_ptr->socket->connectToServer(serverName);

        // Even though a shared memory block exists, the original application might have crashed
        // So only after a successful connection is the second instance terminated
        if (d_ptr->socket->waitForConnected(100)) {
            // Before closing, we send the arguments that this application was called with
            // to the old instance
            QByteArray argumentData;

            // Serialize the application arguments
            QDataStream ds(&argumentData, QIODevice::WriteOnly);
            ds << arguments();

            d_ptr->socket->write(argumentData);
            d_ptr->socket->waitForBytesWritten(200); // Make sure our data is written

            ::exit(EXIT_SUCCESS); // Terminate the program using STDLib's exit function
        } else {
            delete d_ptr->memory;
            ::exit(EXIT_SUCCESS);
        }
    }

#ifdef Q_OS_WIN
    // Creating a Windows Mutex, mostly so that other apps (like Inno Installer) can also know about the application's single instance
    QString mutexName = "Global\\" + serverName.replace(" ", "");
    HANDLE mutexResult = CreateMutexA(NULL, FALSE, mutexName.toStdString().c_str());

    if (mutexResult == NULL) {
        // Quit if we couldn't create the mutext.
        delete d_ptr->memory;
        ::exit(EXIT_SUCCESS);
    }
#endif
}

/**
 * @brief Destructor
 */
SingleApplication::~SingleApplication()
{
    delete d_ptr->memory;
    d_ptr->server->close();
}

/**
 * @brief Executed when the new instance connects with the LocalServer
 */
void SingleApplication::slotConnectionEstablished()
{
    QLocalSocket *socket = d_ptr->server->nextPendingConnection();

    // Emit showUp as soon as the connection is established
    emit showUp();

    // Connect the socket's readyRead signal to a lambda that is in charge of
    // grabbing the arguments and emitting the signal that they arrived.
    connect(socket, &QLocalSocket::readyRead, [&, socket] {
        // Grab all the data from the socket
        QByteArray argumentData = socket->readAll();

        // Deserialize it
        QStringList arguments;
        QDataStream ds(argumentData);
        ds >> arguments;

        emit instanceArguments(arguments);

        socket->close();
    });

    // Makes sure we delete the socket object even if we receive no data
    connect(socket, &QLocalSocket::aboutToClose, socket, &QLocalSocket::deleteLater);
}
