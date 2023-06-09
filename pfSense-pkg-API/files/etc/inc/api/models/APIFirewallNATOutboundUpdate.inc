<?php
//   Copyright 2022 Jared Hendrickson
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

require_once("api/framework/APIModel.inc");
require_once("api/framework/APIResponse.inc");

class APIFirewallNATOutboundUpdate extends APIModel {
    private $modes;
    private $current_mode;

    # Create our method constructor
    public function __construct() {
        parent::__construct();
        $this->privileges = ["page-all", "page-firewall-nat-outbound"];
        $this->modes = ["automatic", "hybrid", "advanced", "disabled"];
    }

    public function action() {
        # Create automatic rules if required, write to config and reload the filter
        $this->create_automatic_rules();
        $this->config["nat"]["outbound"]["mode"] = $this->validated_data["mode"];
        $this->write_config();
        mark_subsystem_dirty('natconf');

        # Only reload the firewall filter if a false value was not passed in
        # TODO: This condition applies the changes by default to stay backwards compatible with v1.3.0
        # TODO: this should be refactored in a future release to not apply by default
        if ($this->initial_data["apply"] !== false) {
            APIFirewallApplyCreate::apply();
        }

        return APIResponse\get(0, $this->validated_data);
    }

    private function __get_default() {
        # Default to the current configured value, if no value is configured default to automatic
        $this->validated_data["mode"] = $this->config["nat"]["outbound"]["mode"];
        if (empty($this->validated_data["mode"])) {
            $this->validated_data["mode"] = "automatic";
        }
    }

    private function __validate_mode() {
        $this->current_mode = $this->validated_data["mode"];
        # Optionally allow client to update the outbound NAT mode
        if (isset($this->initial_data["mode"])) {
            # Require the mode to be a supported outbound NAT mode
            if (in_array($this->initial_data["mode"], $this->modes)) {
                $this->validated_data["mode"] = $this->initial_data["mode"];
            } else {
                $this->errors[] = APIResponse\get(4085);
            }
        }
    }

    # Creates manual rule entries for auto created rules when switching from hybrid/automatic to advanced
    public function create_automatic_rules() {
        global $FilterIflist;
        global $GatewaysList;

        if ($this->validated_data["mode"] == "advanced" and in_array($this->current_mode, ["automatic", "hybrid"])) {
            if (empty($FilterIflist)) {
                filter_generate_optcfg_array();
            }

            if (empty($GatewaysList)) {
                filter_generate_gateways();
            }
            $to_nat_hosts = filter_nat_rules_automatic_tonathosts(true);
            $automatic_rules = filter_nat_rules_outbound_automatic("");

            foreach ($to_nat_hosts as $to_nat_host) {
                foreach ($automatic_rules as $nat_ent) {
                    $nat_ent['source']['network'] = $to_nat_host['subnet'];
                    $nat_ent['descr'] .= sprintf(gettext(' - %1$s to %2$s'),
                        $to_nat_host['descr'],
                        convert_real_interface_to_friendly_descr($nat_ent['interface']));
                    $nat_ent['created'] = make_config_revision_entry(null, gettext("Manual Outbound NAT Switch"));

                    if ($this->__is_duplicate_rule($nat_ent) === false) {
                        $this->config["nat"]["outbound"]["rule"][] = $nat_ent;
                    }
                }
            }
            unset($FilterIflist, $GatewaysList);
        }
    }

    # Checks if this outbound NAT entry is a duplicate. Returns true or false.
    private function __is_duplicate_rule($nat_ent) {
        foreach ($this->config["nat"]["outbound"]["rule"] as $rule) {
            if ($rule['interface'] == $nat_ent['interface'] &&
                $rule['source']['network'] == $nat_ent['source']['network'] &&
                $rule['dstport'] == $nat_ent['dstport'] &&
                $rule['target'] == $nat_ent['target'] &&
                $rule['descr'] == $nat_ent['descr']) {
                return true;
            }
        }
        return false;
    }

    # Ensures we have our configuration array formatted for outbound NAT configurations
    private function __init_config() {
        $this->config["nat"] = (is_array($this->config["nat"])) ? $this->config["nat"] : [];
        $this->config["nat"]["outbound"] = (is_array($this->config["nat"]["outbound"])) ? $this->config["nat"]["outbound"] : [];
        $this->config["nat"]["outbound"]["rule"] = (is_array($this->config["nat"]["outbound"]["rule"])) ? $this->config["nat"]["outbound"]["rule"] : [];
    }

    public function validate_payload() {
        $this->__init_config();
        $this->__get_default();
        $this->__validate_mode();
    }
}
