#include "ui/qt_runtime.h"

#include <QtGlobal>

namespace tmw::ui {

std::string compiledQtVersion() {
    return QT_VERSION_STR;
}

std::string runtimeQtVersion() {
    return qVersion();
}

}  // namespace tmw::ui
