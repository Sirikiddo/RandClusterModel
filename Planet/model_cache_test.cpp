#include <QCoreApplication>
#include <QDebug>
#include <unistd.h>
#include <fstream>
#include "ModelHandler.h"

static size_t currentRSSBytes() {
    std::ifstream statm("/proc/self/statm");
    size_t size = 0, resident = 0;
    if (statm >> size >> resident) {
        const long pageSize = sysconf(_SC_PAGESIZE);
        return static_cast<size_t>(resident) * static_cast<size_t>(pageSize);
    }
    return 0;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString modelPath = (argc > 1) ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("Planet/tree.obj");

    const size_t rssBefore = currentRSSBytes();
    auto first = ModelHandler::loadShared(modelPath);
    const size_t rssAfterFirst = currentRSSBytes();
    auto second = ModelHandler::loadShared(modelPath);
    const size_t rssAfterSecond = currentRSSBytes();

    if (!first || !second) {
        qWarning() << "Failed to load model from" << modelPath;
        return 1;
    }

    qDebug() << "Model loaded" << modelPath;
    qDebug() << "Pointers equal:" << (first == second);
    qDebug() << "RSS KB before:" << rssBefore / 1024
             << "after first:" << rssAfterFirst / 1024
             << "after second:" << rssAfterSecond / 1024;

    return 0;
}
