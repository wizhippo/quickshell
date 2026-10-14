#include "endpointvolume.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScopeGuard>

namespace {
// PKEY_Device_FriendlyName, kept local rather than relying on the uuid
// library exporting it.
constexpr PROPERTYKEY DeviceFriendlyNameKey{
	{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14
};
std::atomic<UINT_PTR> nextCallbackToken{1};
QString errorText(const QString& operation, HRESULT result) {
	return QStringLiteral("%1 failed (HRESULT 0x%2).")
	    .arg(operation, QString::number(static_cast<quint32>(result), 16).rightJustified(8, '0'));
}
} // namespace

// Core Audio calls these interfaces from system threads. Only post a message;
// never touch QObject state, wait, or unregister COM callbacks here.
class AudioCallbacks final: public IMMNotificationClient, public IAudioEndpointVolumeCallback {
public:
	AudioCallbacks(HWND window, UINT message, UINT_PTR token, EDataFlow flow, ERole role)
	    : window(window), message(message), token(token), flow(flow), role(role) {}
	void disable() { window.store(nullptr); }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
		if (!object) return E_POINTER;
		*object = nullptr;
		if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(IMMNotificationClient)))
			*object = static_cast<IMMNotificationClient*>(this);
		else if (IsEqualIID(iid, __uuidof(IAudioEndpointVolumeCallback)))
			*object = static_cast<IAudioEndpointVolumeCallback*>(this);
		else return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
	ULONG STDMETHODCALLTYPE Release() override {
		const auto count = --references;
		if (!count) delete this;
		return count;
	}
	HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
		if (!data) return E_POINTER;
		notify();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow changed, ERole changedRole, LPCWSTR) override {
		// Each instance owns one endpoint, so a change to any other default
		// is not its business.
		if (changed == flow && changedRole == role) notify();
		return S_OK;
	}
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { notify(); return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { notify(); return S_OK; }
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { notify(); return S_OK; }
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { notify(); return S_OK; }

private:
	void notify() {
		if (const auto target = window.load()) PostMessageW(target, message, token, 0);
	}
	std::atomic<ULONG> references{1};
	std::atomic<HWND> window;
	UINT message;
	UINT_PTR token;
	EDataFlow flow;
	ERole role;
};

EndpointVolume::EndpointVolume(EDataFlow flow, ERole role, QObject* parent)
    : QObject(parent)
    , mFlow(flow)
    , mRole(role) {
	callbackToken = nextCallbackToken.fetch_add(1);
	callbackMessage = RegisterWindowMessageW(L"Quickshell.Windows.AudioChanged");
	notificationWindow.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
	notificationWindow.setTitle(QStringLiteral("Quickshell Volume Notifications"));
	notificationHandle = reinterpret_cast<HWND>(notificationWindow.winId());
	callbacks = new AudioCallbacks(notificationHandle, callbackMessage, callbackToken, mFlow, mRole);
	qGuiApp->installNativeEventFilter(this);
	connect(&timer, &QTimer::timeout, this, &EndpointVolume::refresh);
	timer.setInterval(5000); // Recovery if the audio service/device disappears.
	timer.setTimerType(Qt::CoarseTimer);
	timer.start();
	refresh();
}

EndpointVolume::~EndpointVolume() {
	timer.stop();
	callbacks->disable();
	qGuiApp->removeNativeEventFilter(this);
	releaseEndpoint();
	if (enumerator) {
		if (deviceNotificationsRegistered) enumerator->UnregisterEndpointNotificationCallback(callbacks);
		enumerator->Release();
	}
	callbacks->Release();
	notificationWindow.destroy();
	if (ownsApartment) CoUninitialize();
}

int EndpointVolume::volumePercent() const {
	return state.available ? static_cast<int>(std::lround(state.volume * 100)) : -1;
}

QString EndpointVolume::flowDescription() const {
	return mFlow == eCapture ? QStringLiteral("recording") : QStringLiteral("playback");
}

void EndpointVolume::publish(State next) {
	if (state == next) return;
	state = std::move(next);
	emit statusChanged();
}

void EndpointVolume::fail(const QString& operation, HRESULT result) {
	releaseEndpoint();
	State next;
	next.error = errorText(operation, result);
	publish(std::move(next));
}

bool EndpointVolume::ensureEnumerator() {
	if (!apartmentReady) {
		const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
			fail("Initialize Core Audio COM apartment", hr);
			return false;
		}
		// An existing MTA also supports Core Audio. Balance only successful
		// CoInitializeEx calls, including S_FALSE, not RPC_E_CHANGED_MODE.
		ownsApartment = SUCCEEDED(hr);
		apartmentReady = true;
	}
	if (!enumerator) {
		const auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
		                                 __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
		if (FAILED(hr)) {
			fail("Create audio device enumerator", hr);
			return false;
		}
		const auto registration = enumerator->RegisterEndpointNotificationCallback(callbacks);
		deviceNotificationsRegistered = SUCCEEDED(registration);
		if (!deviceNotificationsRegistered)
			qWarning().noquote() << errorText("Register audio device notifications", registration);
	}
	return true;
}

void EndpointVolume::releaseEndpoint() {
	if (endpoint) {
		if (volumeNotificationsRegistered) endpoint->UnregisterControlChangeNotify(callbacks);
		endpoint->Release();
		endpoint = nullptr;
	}
	volumeNotificationsRegistered = false;
	endpointId.clear();
	endpointName.clear();
}

void EndpointVolume::refresh() {
	if (!ensureEnumerator()) return;
	IMMDevice* device = nullptr;
	auto hr = enumerator->GetDefaultAudioEndpoint(mFlow, mRole, &device);
	if (FAILED(hr)) {
		fail(QStringLiteral("Get default %1 device").arg(this->flowDescription()), hr);
		return;
	}
	const auto releaseDevice = qScopeGuard([device] { device->Release(); });
	LPWSTR nativeId = nullptr;
	hr = device->GetId(&nativeId);
	if (FAILED(hr)) {
		fail(QStringLiteral("Read %1 device ID").arg(this->flowDescription()), hr);
		return;
	}
	const auto id = QString::fromWCharArray(nativeId);
	CoTaskMemFree(nativeId);
	if (!endpoint || id != endpointId) {
		releaseEndpoint();
		hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
		                      reinterpret_cast<void**>(&endpoint));
		if (FAILED(hr)) {
			fail(QStringLiteral("Activate %1 volume control").arg(this->flowDescription()), hr);
			return;
		}
		endpointId = id;
		const auto registration = endpoint->RegisterControlChangeNotify(callbacks);
		volumeNotificationsRegistered = SUCCEEDED(registration);
		if (!volumeNotificationsRegistered)
			qWarning().noquote() << errorText("Register volume notifications", registration);
	}
	// The friendly name can change without the endpoint ID changing.
	endpointName.clear();
	IPropertyStore* properties = nullptr;
	if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
		PROPVARIANT name{};
		if (SUCCEEDED(properties->GetValue(DeviceFriendlyNameKey, &name))
		    && name.vt == VT_LPWSTR && name.pwszVal) {
			endpointName = QString::fromWCharArray(name.pwszVal);
		}
		PropVariantClear(&name);
		properties->Release();
	}
	readEndpoint();
}

void EndpointVolume::readEndpoint() {
	if (!endpoint) return;
	float volume = 0;
	BOOL muted = FALSE;
	auto hr = endpoint->GetMasterVolumeLevelScalar(&volume);
	if (SUCCEEDED(hr)) hr = endpoint->GetMute(&muted);
	if (FAILED(hr)) {
		fail(QStringLiteral("Read %1 volume").arg(this->flowDescription()), hr);
		return;
	}
	State next;
	next.available = true;
	next.volume = std::clamp<qreal>(volume, 0, 1);
	next.muted = muted != FALSE;
	next.deviceId = endpointId;
	next.deviceName = endpointName;
	publish(std::move(next));
}

void EndpointVolume::setVolume(qreal value) {
	if (!std::isfinite(value)) {
		auto next = state;
		next.error = QStringLiteral("Volume must be a finite number.");
		publish(std::move(next));
		return;
	}
	if (!endpoint) refresh();
	if (!endpoint) return;
	const auto hr = endpoint->SetMasterVolumeLevelScalar(
	    static_cast<float>(std::clamp<qreal>(value, 0, 1)), nullptr
	);
	if (FAILED(hr)) fail(QStringLiteral("Set %1 volume").arg(this->flowDescription()), hr);
	else readEndpoint(); // Report the actual value instead of guessing hardware state.
}

void EndpointVolume::setMuted(bool value) {
	if (!endpoint) refresh();
	if (!endpoint) return;
	const auto hr = endpoint->SetMute(value ? TRUE : FALSE, nullptr);
	if (FAILED(hr)) fail(QStringLiteral("Set %1 mute").arg(this->flowDescription()), hr);
	else readEndpoint();
}

void EndpointVolume::adjustVolume(qreal delta) {
	if (!std::isfinite(delta)) {
		auto next = state;
		next.error = QStringLiteral("Volume adjustment must be a finite number.");
		publish(std::move(next));
		return;
	}
	refresh(); // Include external volume changes that are still queued for delivery.
	if (state.available) setVolume(state.volume + delta);
}

void EndpointVolume::toggleMuted() {
	refresh();
	if (state.available) setMuted(!state.muted);
}

void EndpointVolume::scheduleRefresh() {
	if (refreshPending) return;
	refreshPending = true;
	QTimer::singleShot(0, this, [this] {
		refreshPending = false;
		refresh();
	});
}

bool EndpointVolume::nativeEventFilter(const QByteArray& type, void* message, qintptr* result) {
	if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
	const auto* msg = static_cast<MSG*>(message);
	if (msg->hwnd != notificationHandle) return false;
	if (callbackMessage && msg->message == callbackMessage) {
		// A late callback may post to an HWND reused by a new engine after
		// reload. The per-instance token prevents it targeting the new owner.
		if (msg->wParam == callbackToken) scheduleRefresh();
		if (result) *result = 0;
		return true;
	}
	if (msg->message == WM_POWERBROADCAST
	    && (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND)) {
		scheduleRefresh();
	}
	return false;
}