#pragma once

// Brings in the Windows SDK headers in the order they require.
#include "device.hpp"

#include <qhash.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/doc.hpp"
#include "../enums.hpp"
#include "../qml.hpp"

namespace qs::network::win {

/// The Windows networking backend.
///
/// Reads the interfaces Windows itself manages through `GetAdaptersAddresses`,
/// and the association of wireless ones through the WLAN API. Association,
/// profiles and radio state stay with Windows' own interfaces, so exposed
/// networks are read-only and @@Networking.connectivity reports link and
/// routing state rather than verified internet reachability.
///
/// The Windows SDK declares no notification type for an address, DNS or
/// interface change, so state is re-read on a short interval instead. Wireless
/// changes are picked up in the same pass.
///
/// Two fields are deliberately left unknown because the shipped SDK does not
/// define the values Windows reports for them:
///
/// * `WifiNetwork.security`, since the header's `DOT11_AUTH_ALGORITHM`
///   enumerators predate WPA3 and do not match what `WLAN_SECURITY_DATA`
///   carries.
/// * `WifiNetwork.band`, since `DOT11_PHY_TYPE` has no per-band enumerators
///   past 802.11n.
///
/// Both report their documented unknown value rather than a guess. SSID and
/// signal quality are unambiguous in every SDK revision and are reported.
class Backend: public NetworkBackend {
	Q_OBJECT;

public:
	explicit Backend(QObject* parent = nullptr);
	~Backend() override;

	[[nodiscard]] bool isAvailable() const override { return this->ready; }
	[[nodiscard]] bool wifiEnabled() const { return this->bWifiEnabled; }
	QBindable<bool> bindableWifiEnabled() { return &this->bWifiEnabled; }
	[[nodiscard]] bool wifiHardwareEnabled() const { return this->bWifiHardwareEnabled; }
	QBindable<bool> bindableWifiHardwareEnabled() { return &this->bWifiHardwareEnabled; }
	[[nodiscard]] bool connectivityCheckAvailable() const { return true; }
	[[nodiscard]] bool connectivityCheckEnabled() const { return this->bConnectivityCheckEnabled; }
	QBindable<bool> bindableConnectivityCheckEnabled() { return &this->bConnectivityCheckEnabled; }
	[[nodiscard]] NetworkConnectivity::Enum connectivity() const { return this->bConnectivity; }
	QBindable<NetworkConnectivity::Enum> bindableConnectivity() { return &this->bConnectivity; }

	/// Re-read every interface and publish changes.
///
/// > [!NOTE] Nothing is read during construction, because publishing a device
/// > emits @@deviceAdded and the consumer cannot connect to that until the
/// > constructor has returned. Call this once the backend is wired up.
	Q_INVOKABLE void refresh();

public slots:
	void setWifiEnabled(bool enabled);
	void setConnectivityCheckEnabled(bool enabled);
	void checkConnectivity();

signals:
	void deviceAdded(NetworkDevice* device);
	void deviceRemoved(NetworkDevice* device);
	void wifiEnabledChanged(bool enabled);
	void wifiHardwareEnabledChanged(bool enabled);
	void connectivityStateChanged(NetworkConnectivity::Enum state);
	void connectivityCheckAvailableChanged(bool available);
	void connectivityCheckEnabledChanged(bool enabled);

private:
	using Guid = GUID;

	void readInterfaces();
	void readWireless(WifiDevice* device);
	void publishConnectivity();
	/// Descriptions of the interfaces the WLAN API recognises as radios.
	[[nodiscard]] QSet<QString> wirelessDescriptions() const;

	QHash<QString, WifiDevice*> wifiDevices;
	QHash<QString, WiredDevice*> wiredDevices;
	HANDLE wlanHandle = nullptr;
	QTimer* pollTimer = nullptr;
	bool ready = false;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(Backend, bool, bWifiEnabled, &Backend::wifiEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Backend, bool, bWifiHardwareEnabled, &Backend::wifiHardwareEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Backend, bool, bConnectivityCheckEnabled, &Backend::connectivityCheckEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Backend, NetworkConnectivity::Enum, bConnectivity, &Backend::connectivityStateChanged);
	// clang-format on
};

} // namespace qs::network::win