#include "backend.hpp"
#include <algorithm>
#include <utility>

#include <qloggingcategory.h>
#include <qset.h>
#include <qstringlist.h>

#include "../../core/logcat.hpp"
#include "../device.hpp"
#include "../enums.hpp"

namespace qs::network::win {

namespace {
QS_LOGGING_CATEGORY(logWinBackend, "quickshell.network.windows", QtWarningMsg);

// Re-reading every interface is a single GetAdaptersAddresses call, which is
// cheap enough to do on this interval.
constexpr int PollInterval = 2000;

QString formatMac(const unsigned char* bytes, ULONG length) {
	QStringList octets;
	for (ULONG i = 0; i < length; ++i)
		octets.append(QString::number(bytes[i], 16).rightJustified(2, '0'));
	return octets.join(':');
}

QString ipv4Of(const SOCKET_ADDRESS* address) {
	if (!address->lpSockaddr) return {};
	auto* v4 = reinterpret_cast<const sockaddr_in*>(address->lpSockaddr);
	if (v4->sin_family != AF_INET) return {};
	return QString::fromLatin1(inet_ntoa(v4->sin_addr));
}

// Each of these lists is a singly linked chain of a different struct, but all
// of them carry a SOCKET_ADDRESS and a Next pointer, so one walk serves them
// all.
template <typename T>
QStringList ipv4List(T* head) {
	QStringList result;
	for (auto* node = head; node; node = node->Next) {
		const auto address = ipv4Of(&node->Address);
		if (!address.isEmpty() && !result.contains(address)) result.append(address);
	}
	return result;
}

QString ssidOf(const DOT11_SSID& ssid) {
	if (ssid.uSSIDLength == 0 || ssid.uSSIDLength > DOT11_SSID_MAX_LENGTH) return {};
	const auto length = static_cast<int>(ssid.uSSIDLength);
	const auto* bytes = reinterpret_cast<const char*>(ssid.ucSSID);
	// An SSID is arbitrary octets rather than a UTF-16 string, so decoding it
	// as one mangles ordinary ASCII names into CJK garbage.
	auto text = QString::fromUtf8(bytes, length);
	if (text.contains(QChar(0xFFFD))) text = QString::fromLatin1(bytes, length);
	return text;
}

// GetAdaptersAddresses leaves FirstGatewayAddress empty when the default route
// is installed by a filter driver, which is common with VPN and container
// networking stacks. The forwarding table is authoritative about what Windows
// actually routes through, so it is consulted when the adapter list has
// nothing to say.
QStringList defaultGateways(NET_IFINDEX interfaceIndex) {
	QStringList result;
	MIB_IPFORWARD_TABLE2* table = nullptr;
	if (GetIpForwardTable2(AF_INET, &table) != NO_ERROR || table == nullptr) return result;
	for (ULONG i = 0; i < table->NumEntries; ++i) {
		const auto& row = table->Table[i];
		if (row.InterfaceIndex != interfaceIndex) continue;
		// Only the default route, which is what a panel means by "gateway".
		if (row.DestinationPrefix.Prefix.si_family != AF_INET) continue;
		if (row.DestinationPrefix.PrefixLength != 0) continue;
		auto* next = reinterpret_cast<const sockaddr_in*>(&row.NextHop);
		if (next->sin_family != AF_INET) continue;
		const auto address = QString::fromLatin1(inet_ntoa(next->sin_addr));
		if (!address.isEmpty() && !result.contains(address)) result.append(address);
	}
	FreeMibTable(table);
	return result;
}

// Adapters can report a link speed of ULONG64_MAX or similar when the driver
// has nothing meaningful to say; anything past the fastest plausible link is
// treated as unknown rather than shown as a nonsense figure.
constexpr quint64 PlausibleLinkSpeedMbps = 400000;

} // namespace

Backend::Backend(QObject* parent): NetworkBackend(parent) {
	this->ready = true;
	this->bindableConnectivityCheckEnabled().setValue(true);

	this->pollTimer = new QTimer(this);
	this->pollTimer->setInterval(PollInterval);
	connect(this->pollTimer, &QTimer::timeout, this, &Backend::refresh);
	this->pollTimer->start();

	DWORD negotiated = 0;
	if (WlanOpenHandle(2, nullptr, &negotiated, &this->wlanHandle) != ERROR_SUCCESS) {
		qCWarning(logWinBackend) << "The WLAN API is unavailable; wireless state will not be reported";
		this->wlanHandle = nullptr;
	}
	// Interfaces are deliberately not read here. Publishing a device emits
	// deviceAdded, and the consumer wires that signal up only after this
	// constructor returns, so an eager read would be missed. Callers must
	// refresh() once they are connected.
}

Backend::~Backend() {
	this->pollTimer->stop();
	if (this->wlanHandle) WlanCloseHandle(this->wlanHandle, nullptr);
}

void Backend::setWifiEnabled(bool enabled) {
	if (this->bWifiEnabled == enabled) return;
	// The WLAN API exposes no supported way to toggle the radio; Windows owns
	// it. Say so rather than pretending the write took effect.
	qCWarning(logWinBackend) << "Windows owns the wireless radio; change it from Windows settings";
}

void Backend::setConnectivityCheckEnabled(bool enabled) {
	if (this->bConnectivityCheckEnabled == enabled) return;
	this->bConnectivityCheckEnabled = enabled;
}

void Backend::checkConnectivity() { this->readInterfaces(); }

void Backend::refresh() { this->readInterfaces(); }

QSet<QString> Backend::wirelessDescriptions() const {
	QSet<QString> result;
	WLAN_INTERFACE_INFO_LIST* list = nullptr;
	if (!this->wlanHandle) return result;
	if (WlanEnumInterfaces(this->wlanHandle, nullptr, &list) != ERROR_SUCCESS) return result;
	for (DWORD i = 0; i < list->dwNumberOfItems; ++i)
		result.insert(QString::fromWCharArray(list->InterfaceInfo[i].strInterfaceDescription).trimmed());
	WlanFreeMemory(list);
	return result;
}

void Backend::readWireless(WifiDevice* device) {
	if (!this->wlanHandle) {
		device->applyAssociation({}, 0, WifiSecurityType::Unknown, 0);
		return;
	}
	// The WLAN API keys interfaces by GUID and the SDK declares no
	// alias-to-GUID conversion, so the interface is matched by the description
	// both enumerations report.
	WLAN_INTERFACE_INFO_LIST* list = nullptr;
	if (WlanEnumInterfaces(this->wlanHandle, nullptr, &list) != ERROR_SUCCESS) {
		device->applyAssociation({}, 0, WifiSecurityType::Unknown, 0);
		return;
	}
	auto guid = std::optional<Guid> {};
	for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
		const auto description =
		    QString::fromWCharArray(list->InterfaceInfo[i].strInterfaceDescription).trimmed();
		if (description != device->description()) continue;
		guid = list->InterfaceInfo[i].InterfaceGuid;
		break;
	}
	if (!guid) {
		WlanFreeMemory(list);
		device->applyAssociation({}, 0, WifiSecurityType::Unknown, 0);
		return;
	}
	WlanFreeMemory(list);

	WLAN_CONNECTION_ATTRIBUTES* attributes = nullptr;
	DWORD size = 0;
	WLAN_OPCODE_VALUE_TYPE type {};
	// The SDK takes the buffer size before the buffer itself.
	if (WlanQueryInterface(
	        this->wlanHandle,
	        &*guid,
	        wlan_intf_opcode_current_connection,
	        nullptr,
	        &size,
	        reinterpret_cast<void**>(&attributes),
	        &type
	    ) != ERROR_SUCCESS
	) {
		device->applyAssociation({}, 0, WifiSecurityType::Unknown, 0);
		return;
	}
	const auto& association = attributes->wlanAssociationAttributes;
	auto state = ConnectionState::Disconnected;
	switch (attributes->isState) {
	case wlan_interface_state_connected: state = ConnectionState::Connected; break;
	case wlan_interface_state_associating:
	case wlan_interface_state_authenticating:
	case wlan_interface_state_discovering: state = ConnectionState::Connecting; break;
	case wlan_interface_state_disconnecting: state = ConnectionState::Disconnecting; break;
	default: break;
	}
	device->applyAssociation(
	    ssidOf(association.dot11Ssid),
	    association.wlanSignalQuality / 100.0,
	    WifiSecurityType::Unknown,
	    0
	);
	device->setAssociationState(state);
	WlanFreeMemory(attributes);
}

void Backend::readInterfaces() {
	if (!this->ready) return;
auto size = ULONG(16384);
	IP_ADAPTER_ADDRESSES* addresses = nullptr;
	// DNS servers are read, so they must not be skipped. AF_UNSPEC is used
	// because AF_INET leaves FirstGatewayAddress empty on some stacks; the
	// per-address walk filters back to IPv4.
	auto flags = ULONG(GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST);
	// The list has to be sized before it can be filled. An interface arriving
	// between the two calls reports ERROR_BUFFER_OVERFLOW again; the poll
	// retries, so one dropped sample is not worth looping for.
	if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW
	    || size == 0)
		return;
	addresses = static_cast<IP_ADAPTER_ADDRESSES*>(malloc(size));
	if (!addresses) return;
	if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addresses, &size) != NO_ERROR) {
		free(addresses);
		return;
	}

	QSet<QString> seenWifi;
	QSet<QString> seenWired;
	auto radioOn = false;
	// Only the WLAN API knows which interfaces are actual radios. Several
	// virtual adapters (TAP, Bluetooth, container networking) report an
	// 802.11 interface type while having nothing to do with Wi-Fi.
	const auto wirelessDescriptions = this->wirelessDescriptions();

	for (auto* adapter = addresses; adapter; adapter = adapter->Next) {
		if (!adapter->FriendlyName) continue;
		const auto alias = QString::fromWCharArray(adapter->FriendlyName).trimmed();
		if (alias.isEmpty()) continue;
		const auto up = adapter->OperStatus == IfOperStatusUp;
		const auto description =
		    adapter->Description ? QString::fromWCharArray(adapter->Description).trimmed() : alias;
		const auto mac = formatMac(adapter->PhysicalAddress, adapter->PhysicalAddressLength);
		const auto ipv4 = ipv4List(adapter->FirstUnicastAddress);
		auto gateways = ipv4List(adapter->FirstGatewayAddress);
		if (gateways.isEmpty()) gateways = defaultGateways(adapter->IfIndex);
		const auto dns = ipv4List(adapter->FirstDnsServerAddress);
		// TransmitLinkSpeed is reported in bits per second.
		auto speed = static_cast<quint64>(adapter->TransmitLinkSpeed / 1000000);
		if (speed > PlausibleLinkSpeedMbps) speed = 0;
		const auto index = static_cast<quint64>(adapter->IfIndex);

		if (wirelessDescriptions.contains(description)) {
			seenWifi.insert(alias);
			auto* device = this->wifiDevices.value(alias, nullptr);
			if (!device) {
				device = new WifiDevice(alias, this);
				this->wifiDevices.insert(alias, device);
				emit this->deviceAdded(device);
			}
			device->applyInterface(
			    index, up, description, mac, ipv4, gateways, dns,
			    static_cast<quint32>(speed)
			);
			this->readWireless(device);
			// The radio state is per interface, and an operational status only
			// tells us the adapter is present.
			if (up) radioOn = true;
			continue;
		}
		if (adapter->IfType != IF_TYPE_ETHERNET_CSMACD) continue;

		seenWired.insert(alias);
		auto* device = this->wiredDevices.value(alias, nullptr);
		if (!device) {
			device = new WiredDevice(alias, this);
			this->wiredDevices.insert(alias, device);
			emit this->deviceAdded(device);
		}
		// Windows only reports an Ethernet interface as up with a carrier, so
		// the operational status is the link state here.
		device->applyInterface(index, up, up, description, mac, ipv4, gateways, dns, speed);
		device->setNetworkName(up && !ipv4.isEmpty() ? alias : QString());
	}

	free(addresses);

	for (const auto& alias: this->wifiDevices.keys()) {
		if (seenWifi.contains(alias)) continue;
		auto* device = this->wifiDevices.take(alias);
		emit this->deviceRemoved(device);
		device->deleteLater();
	}
	for (const auto& alias: this->wiredDevices.keys()) {
		if (seenWired.contains(alias)) continue;
		auto* device = this->wiredDevices.take(alias);
		emit this->deviceRemoved(device);
		device->deleteLater();
	}

	// An interface reporting Up is not radio-blocked, and the SDK declares no
	// radio state opcode, so oper status is the strongest signal available.
	this->bindableWifiHardwareEnabled().setValue(radioOn);
	this->bindableWifiEnabled().setValue(radioOn);

	this->publishConnectivity();
}

void Backend::publishConnectivity() {
	if (!this->bConnectivityCheckEnabled) return;
	auto connected = false;
	auto routed = false;
	const auto consider = [&](const NetworkDevice* device) {
		if (device->state() != ConnectionState::Connected) return;
		connected = true;
		if (!device->gateways().isEmpty()) routed = true;
	};
	for (auto* device: this->wifiDevices) consider(device);
	for (auto* device: this->wiredDevices) consider(device);

	// No verified probe runs here, so this reports link and routing state
	// rather than confirmed reachability of the internet.
	this->bindableConnectivity().setValue(
	    !connected ? NetworkConnectivity::None
	               : routed ? NetworkConnectivity::Full : NetworkConnectivity::Limited
	);
}

} // namespace qs::network::win