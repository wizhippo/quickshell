#pragma once

#include "endpointvolume.hpp"

///! Input volume of the default Windows multimedia recording device.
/// The microphone counterpart to @@Volume, independent of it: recording and
/// playback defaults change separately, and neither endpoint's state is derived
/// from the other.
class InputVolume: public EndpointVolume {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit InputVolume(QObject* parent = nullptr);

private:
	[[nodiscard]] QString flowNoun() const override { return QStringLiteral("recording"); }
};