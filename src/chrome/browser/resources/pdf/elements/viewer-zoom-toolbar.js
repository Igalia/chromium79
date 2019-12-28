// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @typedef {{
 *   fittingType: !FittingType,
 *   userInitiated: boolean,
 * }}
 */
let FitToChangedEvent;

(function() {

const FIT_TO_PAGE_BUTTON_STATE = 0;
const FIT_TO_WIDTH_BUTTON_STATE = 1;

Polymer({
  is: 'viewer-zoom-toolbar',

  properties: {
    newPrintPreview: {
      type: Boolean,
      reflectToAttribute: true,
    },

    /** @private */
    keyboardNavigationActive_: {
      type: Boolean,
      value: false,
    },

    /** @private */
    showOnLeft_: {
      type: Boolean,
      computed: 'computeShowOnLeft_(newPrintPreview)',
      reflectToAttribute: true,
    },
  },

  listeners: {
    'focus': 'onFocus_',
    'keyup': 'onKeyUp_',
    'pointerdown': 'onPointerDown_',
  },

  /** @private {boolean} */
  isPrintPreview_: false,

  /** @private {boolean} */
  visible_: true,

  /** @param {boolean} isPrintPreview */
  setIsPrintPreview: function(isPrintPreview) {
    this.isPrintPreview_ = isPrintPreview;
  },

  /** @return {boolean} */
  isPrintPreview: function() {
    return this.isPrintPreview_;
  },

  /** @return {boolean} */
  isVisible: function() {
    return this.visible_;
  },

  /** @private */
  onFocus_: function() {
    // This can only happen when the plugin is shown within Print Preview using
    // keyboard navigation.
    if (!this.visible_) {
      assert(this.isPrintPreview_);
      this.fire('keyboard-navigation-active', true);
      this.show();
    }
  },

  /** @private */
  onKeyUp_: function() {
    if (this.isPrintPreview_) {
      this.fire('keyboard-navigation-active', true);
    }
    this.keyboardNavigationActive_ = true;
  },

  /** @private */
  onPointerDown_: function() {
    if (this.isPrintPreview_) {
      this.fire('keyboard-navigation-active', false);
    }
    this.keyboardNavigationActive_ = false;
  },

  /**
   * @return {boolean} Whether to show the zoom toolbar on the left side of the
   *     viewport.
   * @private
   */
  computeShowOnLeft_: function() {
    return isRTL() !== this.newPrintPreview;
  },

  /**
   * Change button tooltips to match any changes to localized strings.
   * @param {!{tooltipFitToPage: string,
   *           tooltipFitToWidth: string,
   *           tooltipZoomIn: string,
   *           tooltipZoomOut: string}} strings
   */
  setStrings: function(strings) {
    this.$['fit-button'].tooltips =
        [strings.tooltipFitToPage, strings.tooltipFitToWidth];
    this.$['zoom-in-button'].tooltips = [strings.tooltipZoomIn];
    this.$['zoom-out-button'].tooltips = [strings.tooltipZoomOut];
  },

  /** Handle clicks of the fit-button. */
  fitToggle: function() {
    this.fireFitToChangedEvent_(
        this.$['fit-button'].activeIndex == FIT_TO_WIDTH_BUTTON_STATE ?
            FittingType.FIT_TO_WIDTH :
            FittingType.FIT_TO_PAGE,
        true);
  },

  /** Handle the keyboard shortcut equivalent of fit-button clicks. */
  fitToggleFromHotKey: function() {
    this.fitToggle();

    // Toggle the button state since there was no mouse click.
    const button = this.$['fit-button'];
    button.activeIndex =
        (button.activeIndex == FIT_TO_WIDTH_BUTTON_STATE ?
             FIT_TO_PAGE_BUTTON_STATE :
             FIT_TO_WIDTH_BUTTON_STATE);
  },

  /**
   * Handle forcing zoom via scripting to a fitting type.
   * @param {!FittingType} fittingType Page fitting type to force.
   */
  forceFit: function(fittingType) {
    this.fireFitToChangedEvent_(fittingType, false);

    // Set the button state since there was no mouse click.
    const nextButtonState =
        (fittingType == FittingType.FIT_TO_WIDTH ? FIT_TO_PAGE_BUTTON_STATE :
                                                   FIT_TO_WIDTH_BUTTON_STATE);
    this.$['fit-button'].activeIndex = nextButtonState;
  },

  /**
   * Fire a 'fit-to-changed' {CustomEvent} with the given FittingType as detail.
   * @param {!FittingType} fittingType to include as payload.
   * @param {boolean} userInitiated whether the event was initiated by a user
   *     action.
   * @private
   */
  fireFitToChangedEvent_: function(fittingType, userInitiated) {
    this.fire(
        'fit-to-changed',
        {fittingType: fittingType, userInitiated: userInitiated});
  },

  /**
   * Handle clicks of the zoom-in-button.
   */
  zoomIn: function() {
    this.fire('zoom-in');
  },

  /**
   * Handle clicks of the zoom-out-button.
   */
  zoomOut: function() {
    this.fire('zoom-out');
  },

  show: function() {
    if (!this.visible_) {
      this.visible_ = true;
      this.$['fit-button'].show();
      this.$['zoom-in-button'].show();
      this.$['zoom-out-button'].show();
    }
  },

  hide: function() {
    if (this.visible_) {
      this.visible_ = false;
      this.$['fit-button'].hide();
      this.$['zoom-in-button'].hide();
      this.$['zoom-out-button'].hide();
    }
  },
});
})();
