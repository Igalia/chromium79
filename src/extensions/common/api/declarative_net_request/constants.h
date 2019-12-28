// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_COMMON_API_DECLARATIVE_NET_REQUEST_CONSTANTS_H_
#define EXTENSIONS_COMMON_API_DECLARATIVE_NET_REQUEST_CONSTANTS_H_

namespace extensions {
namespace declarative_net_request {

// Permission name.
extern const char kAPIPermission[];

// Feedback permission name.
extern const char kFeedbackAPIPermission[];

// Minimum valid value of a declarative rule ID.
constexpr int kMinValidID = 1;

// Minimum valid value of a declarative rule priority.
constexpr int kMinValidPriority = 1;

// Default priority used for rules when the priority is not explicity provided
// by an extension.
constexpr int kDefaultPriority = 1;

// Keys used in rules.
extern const char kIDKey[];
extern const char kPriorityKey[];
extern const char kRuleConditionKey[];
extern const char kRuleActionKey[];
extern const char kUrlFilterKey[];
extern const char kIsUrlFilterCaseSensitiveKey[];
extern const char kDomainsKey[];
extern const char kExcludedDomainsKey[];
extern const char kResourceTypesKey[];
extern const char kExcludedResourceTypesKey[];
extern const char kDomainTypeKey[];
extern const char kRuleActionTypeKey[];
extern const char kRemoveHeadersListKey[];
extern const char kRedirectPath[];
extern const char kExtensionPathPath[];
extern const char kTransformSchemePath[];
extern const char kTransformPortPath[];
extern const char kTransformQueryPath[];
extern const char kTransformFragmentPath[];
extern const char kTransformQueryTransformPath[];
extern const char kRedirectKey[];
extern const char kExtensionPathKey[];
extern const char kRedirectUrlKey[];
extern const char kRedirectUrlPath[];
extern const char kTransformKey[];
extern const char kTransformSchemeKey[];
extern const char kTransformHostKey[];
extern const char kTransformPortKey[];
extern const char kTransformPathKey[];
extern const char kTransformQueryKey[];
extern const char kTransformQueryTransformKey[];
extern const char kTransformFragmentKey[];
extern const char kTransformUsernameKey[];
extern const char kTransformPasswordKey[];
extern const char kQueryTransformRemoveParamsKey[];
extern const char kQueryTransformAddReplaceParamsKey[];
extern const char kQueryKeyKey[];
extern const char kQueryValueKey[];

}  // namespace declarative_net_request
}  // namespace extensions

#endif  // EXTENSIONS_COMMON_API_DECLARATIVE_NET_REQUEST_CONSTANTS_H_
