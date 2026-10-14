#include <QtTest>
#include <QScopeGuard>
#include <cmath>
#include <limits>
#include "../volume.hpp"

namespace {
// Independent Core Audio client to verify writes and simulate another app.
class NativeEndpoint {
public:
	NativeEndpoint() {
		result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
		                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
		if (SUCCEEDED(result)) result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
		if (SUCCEEDED(result)) result = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER,
		                                               nullptr, reinterpret_cast<void**>(&endpoint));
	}
	~NativeEndpoint() {
		if (endpoint) endpoint->Release();
		if (device) device->Release();
		if (enumerator) enumerator->Release();
	}
	HRESULT result = E_FAIL;
	IMMDeviceEnumerator* enumerator = nullptr;
	IMMDevice* device = nullptr;
	IAudioEndpointVolume* endpoint = nullptr;
};

HWND notificationWindow() {
	struct Search { HWND window = nullptr; } search;
	EnumWindows([](HWND window, LPARAM argument) -> BOOL {
		DWORD process = 0;
		GetWindowThreadProcessId(window, &process);
		if (process == GetCurrentProcessId()) {
			wchar_t title[128]{};
			GetWindowTextW(window, title, 128);
			if (QString::fromWCharArray(title) == "Quickshell Volume Notifications") {
				reinterpret_cast<Search*>(argument)->window = window;
				return FALSE;
			}
		}
		return TRUE;
	}, reinterpret_cast<LPARAM>(&search));
	return search.window;
}
}

class VolumeTest: public QObject {
	Q_OBJECT;
private slots:
	void readsDefaultDevice() {
		Volume service;
		if (!service.available()) QSKIP(qPrintable(service.errorString()));
		NativeEndpoint native;
		QVERIFY(SUCCEEDED(native.result));
		float value = -1;
		BOOL muted = FALSE;
		QVERIFY(SUCCEEDED(native.endpoint->GetMasterVolumeLevelScalar(&value)));
		QVERIFY(SUCCEEDED(native.endpoint->GetMute(&muted)));
		QVERIFY(std::abs(service.volume() - value) < 0.000001);
		QCOMPARE(service.muted(), muted != FALSE);
		QVERIFY(service.volumePercent() >= 0 && service.volumePercent() <= 100);
		LPWSTR id = nullptr;
		QVERIFY(SUCCEEDED(native.device->GetId(&id)));
		const auto expectedId = QString::fromWCharArray(id);
		CoTaskMemFree(id);
		QCOMPARE(service.deviceId(), expectedId);
	}
	void rejectsNonFiniteWritesWithoutChangingHardware() {
		Volume service;
		if (!service.available()) QSKIP(qPrintable(service.errorString()));
		NativeEndpoint native;
		QVERIFY(SUCCEEDED(native.result));
		float before = -1;
		QVERIFY(SUCCEEDED(native.endpoint->GetMasterVolumeLevelScalar(&before)));
		service.setVolume(std::numeric_limits<qreal>::quiet_NaN());
		QVERIFY(service.available());
		QVERIFY(!service.errorString().isEmpty());
		float after = -1;
		QVERIFY(SUCCEEDED(native.endpoint->GetMasterVolumeLevelScalar(&after)));
		QCOMPARE(after, before);
		service.adjustVolume(std::numeric_limits<qreal>::infinity());
		QVERIFY(SUCCEEDED(native.endpoint->GetMasterVolumeLevelScalar(&after)));
		QCOMPARE(after, before);
		service.refresh();
		QVERIFY(service.errorString().isEmpty());
	}
	void controlsAndExternalNotifications() {
		Volume service;
		if (!service.available()) QSKIP(qPrintable(service.errorString()));
		NativeEndpoint native;
		QVERIFY(SUCCEEDED(native.result));
		float originalVolume = -1;
		BOOL originalMute = FALSE;
		QVERIFY(SUCCEEDED(native.endpoint->GetMasterVolumeLevelScalar(&originalVolume)));
		QVERIFY(SUCCEEDED(native.endpoint->GetMute(&originalMute)));
		// Restore hardware state even when an assertion returns early.
		const auto restore = qScopeGuard([&] {
			if (FAILED(native.endpoint->SetMasterVolumeLevelScalar(originalVolume, nullptr)))
				qWarning() << "Could not restore original volume.";
			if (FAILED(native.endpoint->SetMute(originalMute, nullptr)))
				qWarning() << "Could not restore original mute state.";
		});
		const auto direction = originalVolume <= 0.95f ? 1.0f : -1.0f;
		service.setVolume(originalVolume + direction * 0.01f);
		float current = -1;
		QVERIFY(SUCCEEDED(native.endpoint->GetMasterVolumeLevelScalar(&current)));
		QVERIFY(std::abs(service.volume() - current) < 0.000001);
		QVERIFY(std::abs(current - originalVolume) > 0.0001);
		QTest::qWait(100); // Drain notifications from our own write.

		QSignalSpy changes(&service, &Volume::statusChanged);
		QVERIFY(SUCCEEDED(native.endpoint->SetMasterVolumeLevelScalar(originalVolume + direction * 0.02f, nullptr)));
		QVERIFY(SUCCEEDED(native.endpoint->GetMasterVolumeLevelScalar(&current)));
		// 1500ms is below the five-second fallback, so this exercises callbacks.
		QTRY_VERIFY_WITH_TIMEOUT(std::abs(service.volume() - current) < 0.000001, 1500);
		QVERIFY(changes.count() > 0);
		QTest::qWait(100);
		QVERIFY(SUCCEEDED(native.endpoint->SetMute(!originalMute, nullptr)));
		QTRY_COMPARE_WITH_TIMEOUT(service.muted(), originalMute == FALSE, 1500);
		service.setMuted(originalMute != FALSE);
		BOOL actualMute = FALSE;
		QVERIFY(SUCCEEDED(native.endpoint->GetMute(&actualMute)));
		QCOMPARE(actualMute, originalMute);
	}
	void lifetimeAndLateMessages() {
		UINT_PTR previousToken = 0;
		for (auto iteration = 0; iteration < 3; ++iteration) {
			Volume service;
			const auto handle = notificationWindow();
			QVERIFY(handle != nullptr);
			// Tokens from an old engine must not cause a destroyed QObject to
			// be called when Windows reuses a notification HWND.
			PostMessageW(handle, RegisterWindowMessageW(L"Quickshell.Windows.AudioChanged"), previousToken, 0);
			QCoreApplication::processEvents();
			++previousToken;
		}
		QVERIFY(notificationWindow() == nullptr);
	}
};

QTEST_MAIN(VolumeTest)
#include "volume.moc"
