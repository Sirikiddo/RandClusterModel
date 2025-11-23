#include "HexSphereWidget.h"
#include "PathBuilder.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QVector2D>
#include <QOpenGLContext>
#include <algorithm>
#include <limits>
#include <string>
#include <cmath>
#include <QLabel>
#include <functional>
#include <iostream>

// Разделены реализации HexSphereWidget по тематическим файлам для удобства сопровождения
#include "HexSphereWidget_shaders.inc"
#include "HexSphereWidget_window.inc"
#include "HexSphereWidget_loading.inc"
#include "HexSphereWidget_scene.inc"
