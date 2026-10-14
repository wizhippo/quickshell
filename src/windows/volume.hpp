#pragma once

#include "endpointvolume.hpp"

///! Master output volume of the default Windows multimedia playback device.
/// Each QML engine owns one instance and its native registrations, so reloading
/// leaves no stale Core Audio callbacks behind.
class Volume: public EndpointVolume {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Volume(QObject* parent = nullptr);

private:
	[[nodiscard]] QString flowNoun() const override { return QStringLiteral("playback"); }
};