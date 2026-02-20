#include <QCoreApplication>
#include <QDebug>
#include <windows.h>
#include <psapi.h>
#include <fstream>
#include "model/ModelHandler.h"

// --- ????????? RSS ??? Windows ---
static size_t currentRSSBytes() {
    PROCESS_MEMORY_COUNTERS info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
        return static_cast<size_t>(info.WorkingSetSize); // RSS ??????
    }
    return 0;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const QString modelPath =
        (argc > 1) ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("Planet/tree.obj");

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
