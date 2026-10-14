#include "../core/debuginfo.hpp"
#include <QProcessEnvironment>
#include <QSysInfo>
#include <QtVersion>
#include "build.hpp"

namespace qs::debuginfo {
QString qsVersion() { return QS_VERSION " (revision " GIT_REVISION ", Windows experimental)"; }
QString qtVersion() { return QString::fromLatin1(qVersion()) + " (built against " QT_VERSION_STR ")"; }
QString gpuInfo() { return "GPU diagnostics are not implemented on Windows."; }
QString systemInfo() { return QSysInfo::prettyProductName() + " " + QSysInfo::currentCpuArchitecture(); }
QString envInfo() {
	QString result;
	const auto environment = QProcessEnvironment::systemEnvironment();
	for (const auto& key: environment.keys()) {
		if (key.startsWith("QS_") || key.startsWith("QT_") || key.startsWith("QML_") || key.startsWith("QSG_"))
			result += key + '=' + environment.value(key) + '\n';
	}
	return result;
}
QString combinedInfo() { return qsVersion() + '\n' + qtVersion() + '\n' + systemInfo() + '\n' + envInfo(); }
}
