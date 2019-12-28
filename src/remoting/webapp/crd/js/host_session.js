// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * Class handling creation and teardown of a remoting host session.
 *
 * This abstracts a <embed> element and controls the plugin which does the
 * actual remoting work.  There should be no UI code inside this class.  It
 * should be purely thought of as a controller of sorts.
 */

'use strict';

/** @suppress {duplicate} */
var remoting = remoting || {};

/**
 * @constructor
 * @implements {base.Disposable}
 */
remoting.HostSession = function() {
  /** @type {remoting.It2MeHostFacade} @private */
  this.hostFacade_ = null;
};

// Note that these values are copied directly from it2me_host.h and must be kept
// in sync.
/** @enum {number} */
remoting.HostSession.State = {
  UNKNOWN: -1,
  DISCONNECTED: 0,
  STARTING: 1,
  REQUESTED_ACCESS_CODE: 2,
  RECEIVED_ACCESS_CODE: 3,
  CONNECTING: 4,
  CONNECTED: 5,
  DISCONNECTING: 6,
  ERROR: 7,
  INVALID_DOMAIN_ERROR: 8,
};

remoting.HostSession.prototype.dispose = function() {
  base.dispose(this.hostFacade_);
  this.hostFacade_ = null;
};

/**
 * @param {string} stateString The string representation of the host state.
 * @return {remoting.HostSession.State} The HostSession.State enum value
 *     corresponding to stateString.
 */
remoting.HostSession.State.fromString = function(stateString) {
  if (!remoting.HostSession.State.hasOwnProperty(stateString)) {
    console.error('Unexpected HostSession.State string: ', stateString);
    return remoting.HostSession.State.UNKNOWN;
  }
  return remoting.HostSession.State[stateString];
};

/**
 * Initiates a connection.
 * @param {remoting.It2MeHostFacade} hostFacade It2Me host facade to use.
 * @param {string} email The user's email address.
 * @param {string} accessToken A valid OAuth2 access token.
 * @param {Object} iceConfig ICE config for the host.
 * @param {function(remoting.HostSession.State):void} onStateChanged
 *     Callback for notifications of changes to the host plugin's state.
 * @param {function(boolean):void} onNatTraversalPolicyChanged Callback
 *     for notification of changes to the NAT traversal policy.
 * @param {function(string):void} logDebugInfo Callback allowing the plugin
 *     to log messages to the debug log.
 * @param {function(remoting.Error):void} onError Callback to invoke in case
 *     of an error.
 */
remoting.HostSession.prototype.connect = function(
    hostFacade, email, accessToken, iceConfig, onStateChanged,
    onNatTraversalPolicyChanged, logDebugInfo, onError) {
  /** @private */
  this.hostFacade_ = hostFacade;

  this.hostFacade_.connect(
      email, 'oauth2:' + accessToken, iceConfig, onStateChanged,
      onNatTraversalPolicyChanged, logDebugInfo, remoting.settings.XMPP_SERVER,
      remoting.settings.XMPP_SERVER_USE_TLS,
      remoting.settings.DIRECTORY_BOT_JID, onError);
};

/**
 * Get the access code generated by the it2me host. Valid only after the
 * host state is RECEIVED_ACCESS_CODE.
 * @return {string} The access code.
 */
remoting.HostSession.prototype.getAccessCode = function() {
  return this.hostFacade_.getAccessCode();
};

/**
 * Get the lifetime for the access code. Valid only after the host state is
 * RECEIVED_ACCESS_CODE.
 * @return {number} The access code lifetime, in seconds.
 */
remoting.HostSession.prototype.getAccessCodeLifetime = function() {
  return this.hostFacade_.getAccessCodeLifetime();
};

/**
 * Get the email address of the connected client. Valid only after the plugin
 * state is CONNECTED.
 * @return {string} The client's email address.
 */
remoting.HostSession.prototype.getClient = function() {
  return this.hostFacade_.getClient();
};

/**
 * Disconnect the it2me session.
 * @return {void} Nothing.
 */
remoting.HostSession.prototype.disconnect = function() {
  this.hostFacade_.disconnect();
};
