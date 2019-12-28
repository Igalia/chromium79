// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/strings/string_number_conversions.h"
#include "chromeos/components/sync_wifi/network_identifier.h"
#include "components/sync/protocol/wifi_configuration_specifics.pb.h"
#include "third_party/cros_system_api/dbus/shill/dbus-constants.h"

namespace chromeos {

namespace sync_wifi {

NetworkIdentifier GeneratePskNetworkId(const std::string& ssid) {
  return NetworkIdentifier(base::HexEncode(ssid.data(), ssid.size()),
                           shill::kSecurityPsk);
}

sync_pb::WifiConfigurationSpecificsData GenerateTestWifiSpecifics(
    const NetworkIdentifier& id) {
  sync_pb::WifiConfigurationSpecificsData specifics;
  specifics.set_hex_ssid(id.hex_ssid());

  if (id.security_type() == shill::kSecurityPsk) {
    specifics.set_security_type(
        sync_pb::WifiConfigurationSpecificsData::SECURITY_TYPE_PSK);
  } else if (id.security_type() == shill::kSecurityWep) {
    specifics.set_security_type(
        sync_pb::WifiConfigurationSpecificsData::SECURITY_TYPE_WEP);
  } else {
    NOTREACHED();
  }
  specifics.set_passphrase("password");
  specifics.set_automatically_connect(
      sync_pb::WifiConfigurationSpecificsData::AUTOMATICALLY_CONNECT_ENABLED);
  specifics.set_is_preferred(
      sync_pb::WifiConfigurationSpecificsData::IS_PREFERRED_ENABLED);
  specifics.set_metered(
      sync_pb::WifiConfigurationSpecificsData::METERED_OPTION_AUTO);
  specifics.mutable_proxy_configuration()->set_proxy_option(
      sync_pb::WifiConfigurationSpecificsData::ProxyConfiguration::
          PROXY_OPTION_DISABLED);
  return specifics;
}

}  // namespace sync_wifi

}  // namespace chromeos
