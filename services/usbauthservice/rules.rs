// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! This module defines the data structures for the USB authorization rule language.
//! It includes enums for actions and operators, structs for device identification
//! and attributes, and the core `Rule` and `Policy` structures for managing
//! authorization rules.

use android_hardware_usb_auth::aidl::android::hardware::usb::UsbAuthorizationSystemState::UsbAuthorizationSystemState;
use std::collections::HashMap;

// Data structures for the USB authorization rule language.

/// Represents the action to be taken when a rule matches a device.
#[derive(Debug, PartialEq, Clone)]
pub enum Action {
    /// Allow the USB device connection.
    Allow,
    /// Ask the user for authorization.
    Ask,
    /// Deny the USB device connection.
    Deny,
    /// Defer the decision to a lower-priority rule or default policy.
    Defer,
    /// Remove the device (e.g., if it was previously authorized).
    Remove,
}

/// Represents an operator for matching multiple values in a condition.
#[derive(Debug, PartialEq, Clone)]
pub enum Operator {
    /// All specified values must match.
    AllOf,
    /// At least one of the specified values must match.
    OneOf,
    /// None of the specified values must match.
    NoneOf,
    /// The set of device values must exactly equal the set of specified values.
    Equals,
}

impl Default for Operator {
    /// The default operator is `Equals`.
    fn default() -> Self {
        Operator::Equals
    }
}

impl Operator {
    /// Evaluates the operator where each rule item is matched against a collection of device items.
    fn evaluate_match<RuleItem, DeviceItem>(
        &self,
        rule_values: &[RuleItem],
        device_values: &[DeviceItem],
        item_matcher: impl Fn(&RuleItem, &DeviceItem) -> bool,
    ) -> bool {
        let has_match = |rule_item: &RuleItem| {
            device_values.iter().any(|device_item| item_matcher(rule_item, device_item))
        };

        match self {
            Operator::OneOf => rule_values.iter().any(has_match),
            Operator::AllOf => rule_values.iter().all(has_match),
            Operator::NoneOf => !rule_values.iter().any(has_match),
            Operator::Equals => {
                device_values.len() == rule_values.len() && rule_values.iter().all(has_match)
            }
        }
    }
}

/// Represents a device's vendor and product IDs.
#[derive(Debug, Clone, Default)]
pub struct DeviceId {
    /// The vendor ID of the device, if specified.
    pub vendor_id: Option<u16>,
    /// The product ID of the device, if specified.
    pub product_id: Option<u16>,
}

impl PartialEq for DeviceId {
    /// Checks if two `DeviceId` instances are equal in a "rule-like" manner.
    ///
    /// This implementation considers `self` (representing a rule's `DeviceId`)
    /// to match `other` (representing a device's `DeviceId`) if:
    /// - If `self.vendor_id` is specified, `other.vendor_id` must match it.
    /// - If `self.product_id` is specified, `other.product_id` must match it.
    ///   If a field in `self` is `None`, it acts as a wildcard and does not impose
    ///   a restriction on the corresponding field in `other`.
    fn eq(&self, other: &Self) -> bool {
        if let Some(self_vendor_id) = self.vendor_id {
            if other.vendor_id != Some(self_vendor_id) {
                return false;
            }
        }
        if let Some(self_product_id) = self.product_id {
            if other.product_id != Some(self_product_id) {
                return false;
            }
        }
        true
    }
}

/// Represents a USB interface type, defined by its class, subclass, and protocol.
#[derive(Debug, Clone)]
pub struct InterfaceType {
    /// The USB interface class.
    pub class: u8,
    /// The USB interface subclass, if specified.
    pub subclass: Option<u8>,
    /// The USB interface protocol, if specified.
    pub protocol: Option<u8>,
}

impl PartialEq for InterfaceType {
    /// Checks if two `InterfaceType` instances are equal.
    ///
    /// This implementation considers two `InterfaceType`s equal if their `class`
    /// matches, and if `subclass` and `protocol` are specified in `self`, they
    /// must also match in `other`. This allows for a "rule-like" matching where
    /// a more general rule (e.g., class only) can match a more specific device
    /// interface (class, subclass, protocol).
    fn eq(&self, other: &Self) -> bool {
        if self.class != other.class {
            return false;
        }
        if let Some(subclass) = self.subclass {
            if other.subclass != Some(subclass) {
                return false;
            }
        }
        if let Some(protocol) = self.protocol {
            if other.protocol != Some(protocol) {
                return false;
            }
        }
        true
    }
}

/// Represents a rule attribute for matching device ports.
#[derive(Debug, PartialEq, Clone)]
pub struct PortAttribute {
    /// The operator to use for matching.
    pub operator: Operator,
    /// The list of port identifiers to match against.
    pub ports: Vec<String>,
}

impl PortAttribute {
    /// Creates a new `PortAttribute`.
    /// If `operator` is `None`, it defaults to `Operator::Equals`.
    pub fn new(operator: Option<Operator>, ports: Vec<String>) -> Self {
        Self { operator: operator.unwrap_or_default(), ports }
    }

    /// Checks if this `PortAttribute` matches the given device ports.
    pub fn matches_device_ports(&self, device_ports: &[String]) -> bool {
        self.operator.evaluate_match(&self.ports, device_ports, |rule_port, device_port| {
            rule_port == device_port
        })
    }
}

/// Represents a rule attribute for matching device interfaces.
#[derive(Debug, PartialEq, Clone)]
pub struct InterfaceAttribute {
    /// The operator to use for matching.
    pub operator: Operator,
    /// The list of interface types to match against.
    pub interfaces: Vec<InterfaceType>,
}

impl InterfaceAttribute {
    /// Creates a new `InterfaceAttribute`.
    /// If `operator` is `None`, it defaults to `Operator::Equals`.
    pub fn new(operator: Option<Operator>, interfaces: Vec<InterfaceType>) -> Self {
        Self { operator: operator.unwrap_or_default(), interfaces }
    }

    /// Checks if this `InterfaceAttribute` matches the given device interfaces.
    pub fn matches_device_interfaces(&self, device_interfaces: &[InterfaceType]) -> bool {
        self.operator.evaluate_match(&self.interfaces, device_interfaces, PartialEq::eq)
    }
}

/// Represents a USB device with its various attributes.
///
/// This struct holds information about a connected USB device, which is then
/// used to evaluate against defined rules.
#[derive(Debug, Clone, Default)]
pub struct UsbDevice {
    /// The name of the device, if available.
    pub name: Option<String>,
    /// The serial number of the device, if available.
    pub serial: Option<String>,
    /// The vendor ID of the device, if available.
    pub vendor_id: Option<u16>,
    /// The product ID of the device, if available.
    pub product_id: Option<u16>,
    /// A list of port identifiers the device is connected through.
    pub ports: Vec<String>,
    /// A list of interface types supported by the device.
    pub interfaces: Vec<InterfaceType>,
    /// Indicates if the device is an internal component of the system.
    pub is_internal: bool,
}

/// Represents a set of attributes used to match against a `UsbDevice`.
///
/// These attributes define the conditions under which a `Rule` applies.
#[derive(Debug, PartialEq, Clone, Default)]
pub struct DeviceAttributes {
    /// Matches the device's name.
    pub name: Option<String>,
    /// Matches the device's serial number.
    pub serial: Option<String>,
    /// Matches the device's vendor and/or product ID.
    pub with_id: Option<DeviceId>,
    /// Matches the ports the device is connected via, using an optional operator.
    pub via_port: Option<PortAttribute>,
    /// Matches the interface types the device provides, using an optional operator.
    pub with_interface: Option<InterfaceAttribute>,
    /// Matches whether the device is an internal component.
    pub internal_device: Option<bool>,
}

/// If you update the `UsbAuthorizationSystemState` AIDL file, you must also
/// update the `ALL_SYSTEM_STATES` constant to ensure consistency
/// between the AIDL definition and the Rust policy. This constant is used to
/// initialize the policy with all valid system states and to validate rules
/// during addition.
/// A constant array containing all possible `UsbAuthorizationSystemState` values.
pub const ALL_SYSTEM_STATES: &[UsbAuthorizationSystemState] = &[
    UsbAuthorizationSystemState::BOOTED,
    UsbAuthorizationSystemState::LOGGED_IN,
    UsbAuthorizationSystemState::SCREEN_LOCKED,
    UsbAuthorizationSystemState::SET_UP,
];

/// Represents a condition based on the system's authorization state.
#[derive(Debug, PartialEq, Clone)]
pub struct SystemCondition {
    /// The operator to apply when evaluating multiple states (e.g., `OneOf`, `AllOf`).
    pub operator: Operator,
    /// The list of `UsbAuthorizationSystemState` values to match against.
    pub states: Vec<UsbAuthorizationSystemState>,
}

/// Represents a single USB authorization rule.
///
/// A rule consists of an `Action` to perform, `DeviceAttributes` to match against
/// a `UsbDevice`, and an optional `SystemCondition` based on the system's state.
#[derive(Debug, PartialEq, Clone)]
pub struct Rule {
    /// The action to take if this rule matches.
    pub action: Action,
    /// The attributes a `UsbDevice` must have to match this rule.
    pub attributes: Option<DeviceAttributes>,
    /// An optional condition based on the system's current authorization state.
    pub condition: Option<SystemCondition>,
}

impl SystemCondition {
    /// Creates a new `SystemCondition`.
    /// If `operator` is `None`, it defaults to `Operator::Equals`.
    pub fn new(operator: Option<Operator>, states: Vec<UsbAuthorizationSystemState>) -> Self {
        Self { operator: operator.unwrap_or_default(), states }
    }
}

/// Implementation of the `Rule` struct.
impl Rule {
    /// Evaluates if the given `UsbDevice` matches the rule's attributes.
    pub fn evaluate(&self, device: &UsbDevice) -> bool {
        // If no attributes are specified in the rule, it matches all devices.
        let Some(attributes) = &self.attributes else {
            return true;
        };

        if attributes.name.is_some() && device.name.as_deref() != attributes.name.as_deref() {
            return false;
        }

        if attributes.serial.is_some() && device.serial.as_deref() != attributes.serial.as_deref() {
            return false;
        }

        if let Some(rule_device_id) = &attributes.with_id {
            let device_id_from_usb_device =
                DeviceId { vendor_id: device.vendor_id, product_id: device.product_id };
            if rule_device_id != &device_id_from_usb_device {
                return false;
            }
        }

        if let Some(rule_is_internal) = attributes.internal_device {
            if rule_is_internal != device.is_internal {
                return false;
            }
        }

        if let Some(via_port) = &attributes.via_port {
            let device_ports = &device.ports;
            if !via_port.matches_device_ports(device_ports) {
                return false;
            }
        }

        if let Some(with_interface) = &attributes.with_interface {
            let device_interfaces = &device.interfaces;
            if !with_interface.matches_device_interfaces(device_interfaces) {
                return false;
            }
        }

        true
    }
}

/// Represents a collection of USB authorization rules.
///
/// This struct manages all loaded rules, organizing them for efficient lookup
/// based on the system's current state.
#[derive(Debug, Default)]
pub struct Policy {
    /// A flat list of all rules added to the policy.
    pub all_rules: Vec<Rule>,
    /// A map of system states to lists of rules that apply to those states.
    pub rules_by_state: HashMap<UsbAuthorizationSystemState, Vec<Rule>>,
    /// A flag to track if a default rule (no device attributes and no system state condition) has been added.
    pub default_rule: Option<Rule>,
}

/// Implementation of the `Policy` struct.
impl Policy {
    /// Creates a new, empty `Policy`.
    pub fn new() -> Self {
        let mut rules_by_state = HashMap::new();
        for state in ALL_SYSTEM_STATES {
            rules_by_state.insert(*state, Vec::new());
        }
        Self { all_rules: Vec::new(), rules_by_state, default_rule: None }
    }

    /// Adds a new `Rule` to the policy.
    ///
    /// The rule is added to `all_rules` and also categorized into `rules_by_state`
    /// based on its `SystemCondition`. If no condition is specified, the rule
    /// applies to all `ALL_SYSTEM_STATES`.
    pub fn add_rule(&mut self, rule: Rule) -> Result<(), AddRuleError> {
        // Check for default rule condition: no attributes and no system condition.

        if rule.attributes.is_none() && rule.condition.is_none() {
            if self.default_rule.is_some() {
                return Err(AddRuleError::DuplicateDefaultRule);
            }
            self.default_rule = Some(rule.clone());
        }

        self.all_rules.push(rule.clone());

        let states: &[UsbAuthorizationSystemState] = match &rule.condition {
            Some(condition) => &condition.states,
            None => ALL_SYSTEM_STATES,
        };

        for state in states {
            // If `get_mut` returns `None`, it means the state was not a key in `rules_by_state`.
            // Since `rules_by_state` is initialized with all `ALL_SYSTEM_STATES`,
            // this implies the state is invalid.
            if let Some(rules_for_state) = self.rules_by_state.get_mut(state) {
                rules_for_state.push(rule.clone());
            } else {
                return Err(AddRuleError::InvalidSystemState(*state));
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_add_rule_no_condition() {
        let mut policy = Policy::new();
        let rule = Rule {
            action: Action::Allow,
            attributes: Some(DeviceAttributes::default()),
            condition: None,
        };
        assert!(policy.add_rule(rule.clone()).is_ok());

        assert_eq!(policy.all_rules.len(), 1);
        assert_eq!(policy.all_rules[0], rule);

        assert_eq!(policy.rules_by_state.len(), ALL_SYSTEM_STATES.len());
        for state in ALL_SYSTEM_STATES {
            assert!(policy.rules_by_state.contains_key(state));
            assert_eq!(policy.rules_by_state[state].len(), 1);
            assert_eq!(policy.rules_by_state[state][0], rule);
        }
    }

    #[test]
    fn test_ensure_system_state_enum_is_in_sync() {
        let aidl_states = UsbAuthorizationSystemState::enum_values();
        assert_eq!(aidl_states.len(), ALL_SYSTEM_STATES.len());
        for state in ALL_SYSTEM_STATES {
            assert!(aidl_states.contains(state));
        }
        for state in aidl_states {
            assert!(ALL_SYSTEM_STATES.contains(&state));
        }
    }

    #[test]
    fn test_add_rule_with_single_condition() {
        let mut policy = Policy::new();
        let states = vec![UsbAuthorizationSystemState::LOGGED_IN];
        let condition = SystemCondition::new(None, states.clone());
        let rule = Rule {
            action: Action::Deny,
            attributes: Some(DeviceAttributes::default()),
            condition: Some(condition),
        };
        assert!(policy.add_rule(rule.clone()).is_ok());

        assert_eq!(policy.all_rules.len(), 1);

        for state in ALL_SYSTEM_STATES {
            let rules_for_state = policy.rules_by_state.get(state).unwrap();
            if *state == UsbAuthorizationSystemState::LOGGED_IN {
                assert_eq!(rules_for_state.len(), 1);
                assert_eq!(rules_for_state[0], rule);
                assert_eq!(
                    rules_for_state[0].condition.as_ref().unwrap().operator,
                    Operator::Equals
                );
            } else {
                assert!(rules_for_state.is_empty());
            }
        }
    }

    #[test]
    fn test_add_rule_with_multiple_conditions() {
        let mut policy = Policy::new();
        let states = vec![
            UsbAuthorizationSystemState::LOGGED_IN,
            UsbAuthorizationSystemState::SCREEN_LOCKED,
        ];
        let condition = SystemCondition::new(None, states.clone());
        let rule = Rule {
            action: Action::Ask,
            attributes: Some(DeviceAttributes::default()),
            condition: Some(condition),
        };
        assert!(policy.add_rule(rule.clone()).is_ok());

        assert_eq!(policy.all_rules.len(), 1);

        for state in states {
            assert!(policy.rules_by_state.contains_key(&state));
            assert_eq!(
                policy.rules_by_state[&state][0].condition.as_ref().unwrap().operator,
                Operator::Equals
            );
            assert_eq!(policy.rules_by_state[&state].len(), 1);
            assert_eq!(policy.rules_by_state[&state][0], rule);
        }
    }

    #[test]
    fn test_add_rule_with_invalid_condition() {
        let mut policy = Policy::new();
        // Use a value not in ALL_SYSTEM_STATES
        let invalid_state = UsbAuthorizationSystemState(999) as UsbAuthorizationSystemState;
        let states = vec![invalid_state];
        let condition = SystemCondition::new(None, states.clone());
        let rule = Rule {
            action: Action::Deny,
            attributes: Some(DeviceAttributes::default()),
            condition: Some(condition),
        };
        let result = policy.add_rule(rule.clone());

        assert_eq!(result, Err(AddRuleError::InvalidSystemState(invalid_state)));
        assert!(policy.all_rules.contains(&rule)); // Rule should still be in all_rules
        assert!(!policy.rules_by_state.values().any(|v| v.contains(&rule))); // Rule should not be added to rules_by_state for any state
    }

    #[test]
    fn test_add_duplicate_default_rule() {
        let mut policy = Policy::new();

        // First default rule
        let default_rule_1 = Rule { action: Action::Allow, attributes: None, condition: None };
        assert!(policy.add_rule(default_rule_1.clone()).is_ok());
        assert_eq!(policy.default_rule.as_ref(), Some(&default_rule_1));

        // Second default rule - should cause an error
        let default_rule_2 = Rule { action: Action::Deny, attributes: None, condition: None };
        let result = policy.add_rule(default_rule_2);
        assert_eq!(result, Err(AddRuleError::DuplicateDefaultRule));

        // Verify that only the first rule was added to all_rules and processed into rules_by_state
        assert_eq!(policy.all_rules.len(), 1);
        assert_eq!(policy.all_rules[0], default_rule_1);

        for state in ALL_SYSTEM_STATES {
            assert_eq!(policy.rules_by_state.get(state).unwrap().len(), 1);
            assert_eq!(policy.rules_by_state.get(state).unwrap()[0], default_rule_1);
        }
    }
}

/// Represents an error that occurred when adding a rule to a policy.
#[derive(Debug, PartialEq)]
pub enum AddRuleError {
    /// The rule contains a `UsbAuthorizationSystemState` that is not recognized by the system.
    InvalidSystemState(UsbAuthorizationSystemState),
    /// Attempted to add a second default rule, but only one is allowed.
    DuplicateDefaultRule,
}

impl std::fmt::Display for AddRuleError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            AddRuleError::InvalidSystemState(state) => {
                write!(f, "Rule contains an invalid system state: {:?}", state)
            }
            AddRuleError::DuplicateDefaultRule => {
                write!(f, "Attempted to add a second default rule, but only one is allowed.")
            }
        }
    }
}

impl std::error::Error for AddRuleError {}

/// Represents an error that occurred during policy loading.
#[derive(Debug, PartialEq, Default)]
pub struct PolicyLoadError {
    /// The file path from which the policy was attempted to be loaded.
    pub file_path: String,
    /// A descriptive error message.
    pub error: String,
}

/// Implements the `std::error::Error` trait for `PolicyLoadError`.
impl std::fmt::Display for PolicyLoadError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "Error loading policy from {}: {}", self.file_path, self.error)
    }
}

impl std::error::Error for PolicyLoadError {}
