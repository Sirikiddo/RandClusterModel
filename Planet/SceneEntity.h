#pragma once
#include <QString>
#include <QVector3D>

struct SceneEntity {
    int id = -1;              // уникальный идентификатор
    QString name;             // имя объекта
    QString meshId = "pyramid"; // тип меша для отрисовки
    QVector3D position;       // мировая позиция
    int currentCell = -1;     // id ячейки планеты
    bool selected = false;    // выделен ли объект
};