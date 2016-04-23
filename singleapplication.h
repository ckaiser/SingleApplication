#ifndef SINGLE_APPLICATION_H
#define SINGLE_APPLICATION_H
#include <QtCore/QtGlobal>

#ifndef QAPPLICATION_CLASS
  #define QAPPLICATION_CLASS QApplication
#endif

#include QT_STRINGIFY(QAPPLICATION_CLASS)

class SingleApplicationPrivate;

/**
 * @brief The SingleApplication class handles multipe instances of the same Application
 * @see QApplication
 */
class SingleApplication : public QAPPLICATION_CLASS
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(SingleApplication)

    typedef QAPPLICATION_CLASS app_t;

public:
    explicit SingleApplication(int &, char *[]);
    ~SingleApplication();

    bool createMutex(const QString& mutexName);

Q_SIGNALS:
    void showUp();
    void instanceArguments(const QStringList& arguments);

private Q_SLOTS:
    void slotConnectionEstablished();

private:
    SingleApplicationPrivate *d_ptr;

};

#endif // SINGLE_APPLICATION_H
