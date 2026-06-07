#ifndef PORTABLEPATHS_H
#define PORTABLEPATHS_H

#include <QString>

class QCoreApplication;

namespace PortablePaths {

/** True when portable.txt exists next to the executable. */
bool isPortableMode();

/** Configure QSettings (IniFormat in ./data). Call once after QCoreApplication exists. */
void install();

/** Data directory: ./data in portable mode, otherwise AppDataLocation. */
QString dataDirectory();

} // namespace PortablePaths

#endif // PORTABLEPATHS_H