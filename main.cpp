#include <QApplication>
#include <QSettings>
#include "ElaApplication.h"
#include "ElaTheme.h"
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    eApp->init();

    MainWindow w;
    w.show();

    QString iniPath = QCoreApplication::applicationDirPath() + "/config.ini";
    QSettings settings(iniPath, QSettings::IniFormat);
    int theme = settings.value("ui/theme", 0).toInt();
    if (theme == 1)
    {
        eTheme->setThemeMode(ElaThemeType::Dark);
    }
    return a.exec();
}