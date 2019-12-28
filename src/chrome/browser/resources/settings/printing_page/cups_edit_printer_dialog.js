// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview 'settings-cups-edit-printer-dialog' is a dialog to edit the
 * existing printer's information and re-configure it.
 */

Polymer({
  is: 'settings-cups-edit-printer-dialog',

  behaviors: [
    CrScrollableBehavior,
    CrNetworkListenerBehavior,
  ],

  properties: {
    /**
     * The currently saved printer.
     * @type {CupsPrinterInfo}
     */
    activePrinter: Object,

    /**
     * Printer that holds the modified changes to activePrinter and only
     * applies these changes when the save button is clicked.
     * @type {CupsPrinterInfo}
     */
    pendingPrinter_: Object,

    /**
     * If the printer needs to be re-configured.
     * @private {boolean}
     */
    needsReconfigured_: {
      type: Boolean,
      value: false,
    },

    /**
     * The current PPD in use by the printer.
     * @private
     */
    userPPD_: String,


    /**
     * Tracks whether the dialog is fully initialized. This is required because
     * the dialog isn't fully initialized until Model and Manufacturer are set.
     * Allows us to ignore changes made to these fields until initialization is
     * complete.
     * @private
     */
    arePrinterFieldsInitialized_: {
      type: Boolean,
      value: false,
    },

    /**
     * If the printer info has changed since loading this dialog. This will
     * only track the freeform input fields, since the other fields contain
     * input selected from dropdown menus.
     * @private
     */
    printerInfoChanged_: {
      type: Boolean,
      value: false,
    },

    networkProtocolActive_: {
      type: Boolean,
      computed: 'isNetworkProtocol_(pendingPrinter_.printerProtocol)',
    },

    /** @type {?Array<string>} */
    manufacturerList: Array,

    /** @type {?Array<string>} */
    modelList: Array,

    /**
     * Whether the user selected PPD file is valid.
     * @private
     */
    invalidPPD_: {
      type: Boolean,
      value: false,
    },

    /**
     * The base name of a newly selected PPD file.
     * @private
     */
    newUserPPD_: String,

    /**
     * The URL to a printer's EULA.
     * @private
     */
    eulaUrl_: {
      type: String,
      value: '',
    },

    /** @private */
    isOnline_: {
      type: Boolean,
      value: true,
    },

    /**
     * The error text to be displayed on the dialog.
     * @private
     */
    errorText_: {
      type: String,
      value: '',
    },
  },

  observers: [
    'printerPathChanged_(pendingPrinter_.*)',
    'selectedEditManufacturerChanged_(pendingPrinter_.ppdManufacturer)',
    'onModelChanged_(pendingPrinter_.ppdModel)',
  ],

  /** @private {?chromeos.networkConfig.mojom.CrosNetworkConfigRemote} */
  networkConfig_: null,

  /** @override */
  created: function() {
    this.networkConfig_ = network_config.MojoInterfaceProviderImpl.getInstance()
                              .getMojoServiceRemote();
  },

  /** @override */
  attached: function() {
    // Create a copy of activePrinter so that we can modify its fields.
    this.pendingPrinter_ = /** @type{CupsPrinterInfo} */
        (Object.assign({}, this.activePrinter));

    this.refreshNetworks_();

    settings.CupsPrintersBrowserProxyImpl.getInstance()
        .getPrinterPpdManufacturerAndModel(this.pendingPrinter_.printerId)
        .then(
            this.onGetPrinterPpdManufacturerAndModel_.bind(this),
            this.onGetPrinterPpdManufacturerAndModelFailed_.bind(this));
    settings.CupsPrintersBrowserProxyImpl.getInstance()
        .getCupsPrinterManufacturersList()
        .then(this.manufacturerListChanged_.bind(this));
    this.userPPD_ =
        settings.printing.getBaseName(this.pendingPrinter_.printerPPDPath);
  },

  /**
   * CrosNetworkConfigObserver impl
   * @param {!Array<chromeos.networkConfig.mojom.NetworkStateProperties>}
   *     networks
   * @private
   */
  onActiveNetworksChanged: function(networks) {
    this.isOnline_ = networks.some(function(network) {
      return OncMojo.connectionStateIsConnected(network.connectionState);
    });
  },

  /**
   * @param {!{path: string, value: string}} change
   * @private
   */
  printerPathChanged_: function(change) {
    if (change.path != 'pendingPrinter_.printerName') {
      this.needsReconfigured_ = true;
    }
  },

  /**
   * @param {!Event} event
   * @private
   */
  onProtocolChange_: function(event) {
    this.set('pendingPrinter_.printerProtocol', event.target.value);
    this.onPrinterInfoChange_();
  },

  /** @private */
  onPrinterInfoChange_: function() {
    this.printerInfoChanged_ = true;
  },

  /** @private */
  onCancelTap_: function() {
    this.$$('add-printer-dialog').close();
  },

  /**
   * Handler for update|reconfigureCupsPrinter success.
   * @param {!PrinterSetupResult} result
   * @private
   */
  onPrinterEditSucceeded_: function(result) {
    this.fire(
        'show-cups-printer-toast',
        {resultCode: result, printerName: this.activePrinter.printerName});
    this.$$('add-printer-dialog').close();
  },

  /**
   * Handler for update|reconfigureCupsPrinter failure.
   * @param {*} result
   * @private
   */
  onPrinterEditFailed_: function(result) {
    this.errorText_ = settings.printing.getErrorText(
        /** @type {PrinterSetupResult} */ (result));
  },

  /** @private */
  onSaveTap_: function() {
    this.updateActivePrinter_();
    if (!this.needsReconfigured_ || !this.isOnline_) {
      // If we don't need to reconfigure or we are offline, just update the
      // printer name.
      settings.CupsPrintersBrowserProxyImpl.getInstance()
          .updateCupsPrinter(
              this.activePrinter.printerId, this.activePrinter.printerName)
          .then(
              this.onPrinterEditSucceeded_.bind(this),
              this.onPrinterEditFailed_.bind(this));
    } else {
      settings.CupsPrintersBrowserProxyImpl.getInstance()
          .reconfigureCupsPrinter(this.activePrinter)
          .then(
              this.onPrinterEditSucceeded_.bind(this),
              this.onPrinterEditFailed_.bind(this));
    }
  },

  /**
   * @param {!CupsPrinterInfo} printer
   * @return {string} The printer's URI that displays in the UI
   * @private
   */
  getPrinterURI_: function(printer) {
    if (!printer) {
      return '';
    } else if (
        printer.printerProtocol && printer.printerAddress &&
        printer.printerQueue) {
      return printer.printerProtocol + '://' + printer.printerAddress + '/' +
          printer.printerQueue;
    } else if (printer.printerProtocol && printer.printerAddress) {
      return printer.printerProtocol + '://' + printer.printerAddress;
    } else {
      return '';
    }
  },

  /**
   * Handler for getPrinterPpdManufacturerAndModel() success case.
   * @param {!PrinterPpdMakeModel} info
   * @private
   */
  onGetPrinterPpdManufacturerAndModel_: function(info) {
    this.set('pendingPrinter_.ppdManufacturer', info.ppdManufacturer);
    this.set('pendingPrinter_.ppdModel', info.ppdModel);

    // |needsReconfigured_| needs to reset to false after |ppdManufacturer| and
    // |ppdModel| are initialized to their correct values.
    this.needsReconfigured_ = false;
  },

  /**
   * Handler for getPrinterPpdManufacturerAndModel() failure case.
   * @private
   */
  onGetPrinterPpdManufacturerAndModelFailed_: function() {
    this.needsReconfigured_ = false;
  },

  /**
   * @param {string} protocol
   * @return {boolean} Whether |protocol| is a network protocol
   * @private
   */
  isNetworkProtocol_: function(protocol) {
    return settings.printing.isNetworkProtocol(protocol);
  },

  /**
   * @return {boolean} Whether the current printer was auto configured.
   * @private
   */
  isAutoconfPrinter_: function() {
    return this.pendingPrinter_.printerPpdReference.autoconf;
  },

  /**
   * @return {boolean} Whether the Save button is enabled.
   * @private
   */
  canSavePrinter_: function() {
    return this.printerInfoChanged_ &&
        (this.isPrinterConfigured_() || !this.isOnline_);
  },

  /**
   * @param {string} manufacturer The manufacturer for which we are retrieving
   *     models.
   * @private
   */
  selectedEditManufacturerChanged_: function(manufacturer) {
    // Reset model if manufacturer is changed.
    this.set('pendingPrinter_.ppdModel', '');
    this.modelList = [];
    if (!!manufacturer && manufacturer.length != 0) {
      settings.CupsPrintersBrowserProxyImpl.getInstance()
          .getCupsPrinterModelsList(manufacturer)
          .then(this.modelListChanged_.bind(this));
    }
  },

  /**
   * Sets printerInfoChanged_ to true to show that the model has changed. Also
   * attempts to get the EULA Url if the selected printer has one.
   * @private
   */
  onModelChanged_: function() {
    if (this.arePrinterFieldsInitialized_) {
      this.printerInfoChanged_ = true;
    }

    if (!this.pendingPrinter_.ppdManufacturer ||
        !this.pendingPrinter_.ppdModel) {
      // Do not check for an EULA unless both |ppdManufacturer| and |ppdModel|
      // are set. Set |eulaUrl_| to be empty in this case.
      this.onGetEulaUrlCompleted_('' /* eulaUrl */);
      return;
    }

    settings.CupsPrintersBrowserProxyImpl.getInstance()
        .getEulaUrl(
            this.pendingPrinter_.ppdManufacturer, this.pendingPrinter_.ppdModel)
        .then(this.onGetEulaUrlCompleted_.bind(this));
  },

  /**
   * @param {string} eulaUrl The URL for the printer's EULA.
   * @private
   */
  onGetEulaUrlCompleted_: function(eulaUrl) {
    this.eulaUrl_ = eulaUrl;
  },

  /** @private */
  onBrowseFile_: function() {
    settings.CupsPrintersBrowserProxyImpl.getInstance()
        .getCupsPrinterPPDPath()
        .then(this.printerPPDPathChanged_.bind(this));
  },

  /**
   * @param {!ManufacturersInfo} manufacturersInfo
   * @private
   */
  manufacturerListChanged_: function(manufacturersInfo) {
    if (!manufacturersInfo.success) {
      return;
    }
    this.manufacturerList = manufacturersInfo.manufacturers;
    if (this.pendingPrinter_.ppdManufacturer.length != 0) {
      settings.CupsPrintersBrowserProxyImpl.getInstance()
          .getCupsPrinterModelsList(this.pendingPrinter_.ppdManufacturer)
          .then(this.modelListChanged_.bind(this));
    }
  },

  /**
   * @param {!ModelsInfo} modelsInfo
   * @private
   */
  modelListChanged_: function(modelsInfo) {
    if (modelsInfo.success) {
      this.modelList = modelsInfo.models;
      // ModelListChanged_ is the final step of initializing pendingPrinter.
      this.arePrinterFieldsInitialized_ = true;
    }
  },

  /**
   * @param {string} path The full path to the selected PPD file
   * @private
   */
  printerPPDPathChanged_: function(path) {
    this.set('pendingPrinter_.printerPPDPath', path);
    this.invalidPPD_ = !path;
    if (!this.invalidPPD_) {
      // A new valid PPD file should be treated as a saveable change.
      this.onPrinterInfoChange_();
    }
    this.userPPD_ = settings.printing.getBaseName(path);
  },

  /**
   * Returns true if the printer has valid name, address, and valid PPD or was
   * auto-configured.
   * @return {boolean}
   * @private
   */
  isPrinterConfigured_: function() {
    return settings.printing.isNameAndAddressValid(this.pendingPrinter_) &&
        (this.isAutoconfPrinter_() ||
         settings.printing.isPPDInfoValid(
             this.pendingPrinter_.ppdManufacturer,
             this.pendingPrinter_.ppdModel,
             this.pendingPrinter_.printerPPDPath));
  },

  /**
   * Helper function to copy over modified fields to activePrinter.
   * @private
   */
  updateActivePrinter_: function() {
    if (!this.isOnline_) {
      // If we are not online, only copy over the printerName.
      this.activePrinter.printerName = this.pendingPrinter_.printerName;
      return;
    }

    this.activePrinter =
        /** @type{CupsPrinterInfo} */ (Object.assign({}, this.pendingPrinter_));
    // Set ppdModel since there is an observer that clears ppdmodel's value when
    // ppdManufacturer changes.
    this.activePrinter.ppdModel = this.pendingPrinter_.ppdModel;
  },

  /**
   * Callback function when networks change.
   * @private
   */
  refreshNetworks_: function() {
    this.networkConfig_
        .getNetworkStateList({
          filter: chromeos.networkConfig.mojom.FilterType.kActive,
          networkType: chromeos.networkConfig.mojom.NetworkType.kAll,
          limit: chromeos.networkConfig.mojom.NO_LIMIT,
        })
        .then((responseParams) => {
          this.onActiveNetworksChanged(responseParams.result);
        });
  },

  /**
   * Returns true if the printer protocol select field should be enabled.
   * @return {boolean}
   * @private
   */
  protocolSelectEnabled: function() {
    return this.isOnline_ && this.networkProtocolActive_;
  },

});
