#pragma once

// Requires QApplication and a desktop OpenGL 3.3 context driver.
bool runWaterScreenPassTests();

class QString;
bool runWaterScreenPassBenchmark(const QString& outputPrefix);
