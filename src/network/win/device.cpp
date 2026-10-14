#include "device.hpp"

#include <qstringlist.h>
#include <utility>

namespace qs::network::win {

WifiNetwork::WifiNetwork(QString ssid, qs::network::WifiDevice* device, QObject* parent)
    : qs::network::WifiNetwork(ssid, device, parent) {
	this->bindableKnown().setValue(true);
}

WiredNetwork::WiredNetwork(QString name, qs::network::WiredDevice* device, QObject* parent)
    : qs::network::Network(name, device, parent) {
	this->bindableKnown().setValue(true);
}

WifiDevice::WifiDevice(QString alias, QObject* parent)
    : qs::network::WifiDevice(parent)
    , mAlias(std::move(alias)) {
	this->bindableName().setValue(this->mAlias);
	this->bindableState().setValue(qs::network::ConnectionState::Unknown);
}

WifiNetwork* WifiDevice::networkFor(const QString& ssid) {
	auto* network = this->mNetworks.value(ssid, nullptr);
	if (network) return network;
	network = new WifiNetwork(ssid, this, this);
	this->mNetworks.insert(ssid, network);
	this->networkAdded(network);
	return network;
}

void WifiDevice::applyInterface(
    quint64 index,
    bool up,
    const QString& description,
    const QString& mac,
    const QStringList& ipv4,
    const QStringList& gateways,
    const QStringList& dns,
    quint32 linkSpeed
) {
	this->mInterfaceIndex = index;
	this->mDescription = description;
	this->bindableAddress().setValue(mac);
	this->bindableIpv4Addresses().setValue(ipv4);
	this->bindableGateways().setValue(gateways);
	this->bindableDnsServers().setValue(dns);
	this->bindableLinkSpeed().setValue(linkSpeed);
	if (!up && this->mConnectedSsid.isEmpty()) this->bindableState().setValue(qs::network::ConnectionState::Unknown);
}

void WifiDevice::applyAssociation(
    const QString& ssid,
    qreal signal,
    WifiSecurityType::Enum security,
    qint32 band
) {
	if (ssid.isEmpty()) {
		// Nothing associated. Report that rather than leaving the previous
		// state, so an interface that never associated is not Unknown.
		this->mConnectedSsid.clear();
		this->setAssociationState(qs::network::ConnectionState::Disconnected);
		return;
	}
	if (ssid == this->mConnectedSsid) {
		auto* network = this->networkFor(ssid);
		network->bindableSignalStrength().setValue(signal);
		network->bindableSecurity().setValue(security);
		network->bindableBand().setValue(band);
		return;
	}

	// The previous association is gone, and without scanning its network is no
	// longer something the interface can vouch for.
	if (!this->mConnectedSsid.isEmpty()) {
		auto* previous = this->mNetworks.take(this->mConnectedSsid);
		if (previous) {
			previous->bindableConnected().setValue(false);
			previous->bindableState().setValue(qs::network::ConnectionState::Disconnected);
			this->networkRemoved(previous);
			previous->deleteLater();
		}
	}
	this->mConnectedSsid = ssid;
	auto* network = this->networkFor(ssid);
	network->bindableConnected().setValue(true);
	network->bindableState().setValue(qs::network::ConnectionState::Connected);
	network->bindableSignalStrength().setValue(signal);
	network->bindableSecurity().setValue(security);
	network->bindableBand().setValue(band);
}

void WifiDevice::setAssociationState(ConnectionState::Enum state) {
	this->mAssociation = state;
	this->bindableState().setValue(state);
	if (this->mConnectedSsid.isEmpty()) return;
	auto* network = this->mNetworks.value(this->mConnectedSsid, nullptr);
	if (!network) return;
	network->bindableConnected().setValue(state == ConnectionState::Connected);
	network->bindableState().setValue(state);
}

WiredDevice::WiredDevice(QString alias, QObject* parent)
    : qs::network::WiredDevice(parent)
    , mAlias(std::move(alias)) {
	this->bindableName().setValue(this->mAlias);
	this->bindableState().setValue(qs::network::ConnectionState::Unknown);
	this->bindableHasLink().setValue(false);
}

void WiredDevice::applyInterface(
    quint64 index,
    bool up,
    bool hasLink,
    const QString& description,
    const QString& mac,
    const QStringList& ipv4,
    const QStringList& gateways,
    const QStringList& dns,
    quint32 linkSpeed
) {
	this->mInterfaceIndex = index;
	this->mDescription = description;
	this->bindableAddress().setValue(mac);
	this->bindableIpv4Addresses().setValue(ipv4);
	this->bindableGateways().setValue(gateways);
	this->bindableDnsServers().setValue(dns);
	this->bindableLinkSpeed().setValue(linkSpeed);
	this->bindableHasLink().setValue(hasLink);
	this->bindableState().setValue(
	    hasLink ? qs::network::ConnectionState::Connected : qs::network::ConnectionState::Disconnected
	);
	if (!up) this->bindableState().setValue(qs::network::ConnectionState::Unknown);
}

void WiredDevice::setNetworkName(const QString& name) {
	auto replace = [&](WiredNetwork* next) {
		auto* previous = this->mNetwork;
		this->mNetwork = next;
		if (previous) this->networkRemoved(previous);
		if (next) {
			next->bindableConnected().setValue(true);
			next->bindableState().setValue(qs::network::ConnectionState::Connected);
			this->networkAdded(next);
		}
		if (previous) previous->deleteLater();
	};
	if (name.isEmpty()) {
		if (this->mNetwork) replace(nullptr);
		return;
	}
	if (this->mNetwork && this->mNetwork->name() == name) return;
	replace(new WiredNetwork(name, this, this));
}

} // namespace qs::network::win