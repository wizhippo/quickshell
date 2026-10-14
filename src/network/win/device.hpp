#pragma once

// The IP helper and WLAN headers declare winsock types of their own and must
// not be reached before winsock2, so the whole SDK block is kept together and
// in this order.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windot11.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <wlanapi.h>

#include <qhash.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../device.hpp"
#include "../enums.hpp"
#include "../network.hpp"
#include "../wifi.hpp"
#include "../wired.hpp"

namespace qs::network::win {

/// A Wi-Fi network as reported by the native WLAN API.
///
/// Windows owns association and profile storage, so this is read-only. The
/// @@@security of the network the interface is currently associated with is
/// reported from the connection; scanned neighbours report `Unknown`, because
/// the WLAN BSS list does not carry per-network authentication.
class WifiNetwork: public qs::network::WifiNetwork {
	Q_OBJECT;

public:
	explicit WifiNetwork(QString ssid, qs::network::WifiDevice* device, QObject* parent = nullptr);
};

/// The wired network of an interface.
///
/// Windows has no separate identity for Ethernet, so a wired device exposes
/// exactly one network, named after the connection profile Windows reports.
class WiredNetwork: public qs::network::Network {
	Q_OBJECT;

public:
	explicit WiredNetwork(QString name, qs::network::WiredDevice* device, QObject* parent = nullptr);
};

/// A Windows wireless network interface.
class WifiDevice: public qs::network::WifiDevice {
	Q_OBJECT;

public:
	explicit WifiDevice(QString alias, QObject* parent = nullptr);

	[[nodiscard]] QString alias() const { return this->mAlias; }
	[[nodiscard]] quint64 interfaceIndex() const { return this->mInterfaceIndex; }
	[[nodiscard]] QString description() const { return this->mDescription; }
	[[nodiscard]] QString connectedSsid() const { return this->mConnectedSsid; }

	/// Apply the addressing and link state from `GetAdaptersAddresses`.
	void applyInterface(
	    quint64 index,
	    bool up,
	    const QString& description,
	    const QString& mac,
	    const QStringList& ipv4,
	    const QStringList& gateways,
	    const QStringList& dns,
	    quint32 linkSpeed
	);

	/// Apply the association state from the WLAN API. An empty `ssid` means the
	/// interface is not associated with anything.
	void applyAssociation(const QString& ssid, qreal signal, WifiSecurityType::Enum security, qint32 band);

	/// Apply the association state separately, so a transitioning interface
	/// keeps its name while it is on its way up or down.
	void setAssociationState(ConnectionState::Enum state);

private:
	WifiNetwork* networkFor(const QString& ssid);

	QString mAlias;
	QString mDescription;
	quint64 mInterfaceIndex = 0;
	QString mConnectedSsid;
	ConnectionState::Enum mAssociation = ConnectionState::Unknown;
	// Networks kept between refreshes, keyed by SSID.
	QHash<QString, WifiNetwork*> mNetworks;
};

/// A Windows wired network interface.
class WiredDevice: public qs::network::WiredDevice {
	Q_OBJECT;

public:
	explicit WiredDevice(QString alias, QObject* parent = nullptr);

	[[nodiscard]] QString alias() const { return this->mAlias; }
	[[nodiscard]] quint64 interfaceIndex() const { return this->mInterfaceIndex; }
	[[nodiscard]] QString description() const { return this->mDescription; }

	/// Apply the addressing and link state from `GetAdaptersAddresses`.
	void applyInterface(
	    quint64 index,
	    bool up,
	    bool hasLink,
	    const QString& description,
	    const QString& mac,
	    const QStringList& ipv4,
	    const QStringList& gateways,
	    const QStringList& dns,
	    quint32 linkSpeed
	);

	/// Name the single wired network from the profile Windows reports, or an
	/// empty string when the interface is not connected.
	void setNetworkName(const QString& name);

private:
	QString mAlias;
	QString mDescription;
	quint64 mInterfaceIndex = 0;
	WiredNetwork* mNetwork = nullptr;
};

} // namespace qs::network::win