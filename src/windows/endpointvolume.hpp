#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWindow>
#include <qqmlintegration.h>
// IAudioEndpointVolume lives here rather than in mmdeviceapi.h.
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>

/// Core Audio volume control for one endpoint.
///
/// Not exposed directly. @@Volume binds the default playback endpoint and
/// @@InputVolume the default recording endpoint; both are this class with a
/// different data flow, so the two stay independent while sharing one
/// implementation.
class EndpointVolume: public QObject, public QAbstractNativeEventFilter {
	Q_OBJECT;
	// clang-format off
	/// True while an endpoint was acquired and read successfully.
	Q_PROPERTY(bool available READ available NOTIFY statusChanged);
	/// Master volume, 0-1, or -1 when unavailable. Writes clamp and reject
	/// non-finite values, then read the device back.
	Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY statusChanged);
	/// Rounded volume percentage, 0-100, or -1 when unavailable.
	Q_PROPERTY(int volumePercent READ volumePercent NOTIFY statusChanged);
	/// Whether the endpoint is muted. Muting preserves the stored volume.
	Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY statusChanged);
	/// Endpoint ID, empty when unavailable.
	Q_PROPERTY(QString deviceId READ deviceId NOTIFY statusChanged);
	/// Friendly output name, empty when Windows supplies none.
	Q_PROPERTY(QString deviceName READ deviceName NOTIFY statusChanged);
	/// Last query or control error, empty after a successful reading.
	Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged);
	// clang-format on

public:
	EndpointVolume(EDataFlow flow, ERole role, QObject* parent = nullptr);
	~EndpointVolume() override;

	bool available() const { return state.available; }
	qreal volume() const { return state.volume; }
	int volumePercent() const;
	bool muted() const { return state.muted; }
	QString deviceId() const { return state.deviceId; }
	QString deviceName() const { return state.deviceName; }
	QString errorString() const { return state.error; }
	void setVolume(qreal value);
	void setMuted(bool value);

	/// Re-read the default endpoint and publish changes.
	Q_INVOKABLE void refresh();

	/// Change the master volume by a fraction, clamped to 0-1.
	Q_INVOKABLE void adjustVolume(qreal delta);

	/// Invert the mute state.
	Q_INVOKABLE void toggleMuted();

	bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;

signals:
	void statusChanged();

protected:
	struct State {
		bool available = false;
		qreal volume = -1;
		bool muted = false;
		QString deviceId;
		QString deviceName;
		QString error;
		bool operator==(const State&) const = default;
	};

	/// Nouns for error messages, so a capture endpoint does not claim to be a
	/// playback one.
	[[nodiscard]] virtual QString flowNoun() const = 0;
	[[nodiscard]] QString flowDescription() const;

private:
	void publish(State next);
	void fail(const QString& operation, HRESULT result);
	bool ensureEnumerator();
	void releaseEndpoint();
	void readEndpoint();
	void scheduleRefresh();

	const EDataFlow mFlow;
	const ERole mRole;
	State state;
	QTimer timer;
	QWindow notificationWindow;
	HWND notificationHandle = nullptr;
	UINT callbackMessage = 0;
	UINT_PTR callbackToken = 0;
	IMMDeviceEnumerator* enumerator = nullptr;
	IAudioEndpointVolume* endpoint = nullptr;
	class AudioCallbacks* callbacks = nullptr;
	QString endpointId;
	QString endpointName;
	bool apartmentReady = false;
	bool ownsApartment = false;
	bool deviceNotificationsRegistered = false;
	bool volumeNotificationsRegistered = false;
	bool refreshPending = false;
};