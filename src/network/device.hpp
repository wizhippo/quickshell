#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstringlist.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/doc.hpp"
#include "../core/model.hpp"
#include "enums.hpp"
#include "network.hpp"

namespace qs::network {

///! A network device.
/// The @@type property may be used to determine if this device is a @@WifiDevice or @@WiredDevice.
class NetworkDevice: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("Devices can only be acquired through Network");
	// clang-format off
	/// The device type.
	///
	/// When the device type is `Wifi`, the device object is a @@WifiDevice.
	/// When the device type is `Wired`, the device object is a @@WiredDevice.
	/// connection and scanning.
	Q_PROPERTY(DeviceType::Enum type READ type CONSTANT);
	/// The name of the device's control interface.
	Q_PROPERTY(QString name READ name NOTIFY nameChanged BINDABLE bindableName);
	/// A list of available or connected networks for this device.
	///
	/// When the device type is 'Wifi', this model will only contain @@WifiNetwork.
	QSDOC_TYPE_OVERRIDE(ObjectModel<Network>*);
	Q_PROPERTY(UntypedObjectModel* networks READ networks CONSTANT);
	/// The hardware address of the device in the XX:XX:XX:XX:XX:XX format.
	Q_PROPERTY(QString address READ default NOTIFY addressChanged BINDABLE bindableAddress);
	/// IPv4 addresses currently assigned to the device, in host order.
	///
	/// > [!NOTE] Not all backends report addressing. An empty list means the
	/// > backend has nothing to say, not that the device is unaddressed.
	Q_PROPERTY(QStringList ipv4Addresses READ default NOTIFY ipv4AddressesChanged BINDABLE bindableIpv4Addresses);
	/// IPv4 addresses of the gateways reachable through the device, in priority order.
	///
	/// > [!NOTE] Not all backends report addressing. An empty list means the
	/// > backend has nothing to say, not that the device has no gateway.
	Q_PROPERTY(QStringList gateways READ default NOTIFY gatewaysChanged BINDABLE bindableGateways);
	/// Addresses of the DNS servers configured for the device, in priority order.
	///
	/// > [!NOTE] Not all backends report addressing. An empty list means the
	/// > backend has nothing to say, not that DNS is unconfigured.
	Q_PROPERTY(QStringList dnsServers READ default NOTIFY dnsServersChanged BINDABLE bindableDnsServers);
	/// Speed of the physical device link in megabits per second, or 0 if unknown.
	Q_PROPERTY(quint32 linkSpeed READ default NOTIFY linkSpeedChanged BINDABLE bindableLinkSpeed);
	/// True if the device is connected.
	Q_PROPERTY(bool connected READ default NOTIFY connectedChanged BINDABLE bindableConnected);
	/// Connection state of the device.
	Q_PROPERTY(qs::network::ConnectionState::Enum state READ default NOTIFY stateChanged BINDABLE bindableState);
	/// True if the device is managed by NetworkManager.
	///
	/// > [!WARNING] Only valid for the NetworkManager backend.
	Q_PROPERTY(bool nmManaged READ nmManaged WRITE setNmManaged NOTIFY nmManagedChanged)
	/// True if the device is allowed to autoconnect to a network.
	Q_PROPERTY(bool autoconnect READ autoconnect WRITE setAutoconnect NOTIFY autoconnectChanged);
	// clang-format on

public:
	explicit NetworkDevice(DeviceType::Enum type, QObject* parent = nullptr);

	/// Disconnects the device and prevents it from automatically activating further connections.
	Q_INVOKABLE void disconnect();

	virtual void networkAdded(Network* net);
	virtual void networkRemoved(Network* net);

	[[nodiscard]] ObjectModel<Network>* networks() { return &this->mNetworks; }
	[[nodiscard]] DeviceType::Enum type() const { return this->mType; }
	QBindable<QString> bindableName() { return &this->bName; }
	[[nodiscard]] QString name() const { return this->bName; }
	QBindable<QString> bindableAddress() { return &this->bAddress; }
	QBindable<QStringList> bindableIpv4Addresses() { return &this->bIpv4Addresses; }
	QBindable<QStringList> bindableGateways() { return &this->bGateways; }
	QBindable<QStringList> bindableDnsServers() { return &this->bDnsServers; }
	QBindable<quint32> bindableLinkSpeed() { return &this->bLinkSpeed; }
	QBindable<bool> bindableConnected() { return &this->bConnected; }
	[[nodiscard]] QStringList gateways() const { return this->bGateways; }
	[[nodiscard]] ConnectionState::Enum state() const { return this->bState; }
	QBindable<ConnectionState::Enum> bindableState() { return &this->bState; }
	QBindable<bool> bindableNmManaged() { return &this->bNmManaged; }
	[[nodiscard]] bool nmManaged() { return this->bNmManaged; }
	void setNmManaged(bool managed);
	QBindable<bool> bindableAutoconnect() { return &this->bAutoconnect; }
	[[nodiscard]] bool autoconnect() { return this->bAutoconnect; }
	void setAutoconnect(bool autoconnect);

signals:
	QSDOC_HIDE void requestDisconnect();
	QSDOC_HIDE void requestSetAutoconnect(bool autoconnect);
	QSDOC_HIDE void requestSetNmManaged(bool managed);
	void nameChanged();
	void addressChanged();
	void ipv4AddressesChanged();
	void gatewaysChanged();
	void dnsServersChanged();
	void linkSpeedChanged();
	void connectedChanged();
	void stateChanged();
	void nmManagedChanged();
	void autoconnectChanged();

protected:
	ObjectModel<Network> mNetworks {this};

private:
	DeviceType::Enum mType;
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, QString, bName, &NetworkDevice::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, QString, bAddress, &NetworkDevice::addressChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, QStringList, bIpv4Addresses, &NetworkDevice::ipv4AddressesChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, QStringList, bGateways, &NetworkDevice::gatewaysChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, QStringList, bDnsServers, &NetworkDevice::dnsServersChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, quint32, bLinkSpeed, &NetworkDevice::linkSpeedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, bool, bConnected, &NetworkDevice::connectedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, ConnectionState::Enum, bState, &NetworkDevice::stateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, bool, bNmManaged, &NetworkDevice::nmManagedChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NetworkDevice, bool, bAutoconnect, &NetworkDevice::autoconnectChanged);
	// clang-format on
};

} // namespace qs::network
