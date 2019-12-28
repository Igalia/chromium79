// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_RENDERER_CONTENT_SETTINGS_OBSERVER_H_
#define CHROME_RENDERER_CONTENT_SETTINGS_OBSERVER_H_

#include <string>
#include <utility>

#include "base/callback.h"
#include "base/containers/flat_map.h"
#include "base/containers/flat_set.h"
#include "base/gtest_prod_util.h"
#include "base/time/time.h"
#include "chrome/common/content_settings_renderer.mojom.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/renderer/render_frame_observer.h"
#include "content/public/renderer/render_frame_observer_tracker.h"
#include "extensions/buildflags/buildflags.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_receiver_set.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"
#include "url/gurl.h"

namespace blink {
struct WebEnabledClientHints;
class WebFrame;
class WebSecurityOrigin;
class WebURL;
}

#if BUILDFLAG(ENABLE_EXTENSIONS)
namespace extensions {
class Dispatcher;
class Extension;
}
#endif

// Handles blocking content per content settings for each RenderFrame.
class ContentSettingsObserver
    : public content::RenderFrameObserver,
      public content::RenderFrameObserverTracker<ContentSettingsObserver>,
      public blink::WebContentSettingsClient,
      public chrome::mojom::ContentSettingsRenderer {
 public:
  // Set |should_whitelist| to true if |render_frame()| contains content that
  // should be whitelisted for content settings.
  ContentSettingsObserver(content::RenderFrame* render_frame,
                          bool should_whitelist,
                          service_manager::BinderRegistry* registry);
  ~ContentSettingsObserver() override;

#if BUILDFLAG(ENABLE_EXTENSIONS)
  // Sets the extension dispatcher. Call this right after constructing this
  // class. This should only be called once.
  void SetExtensionDispatcher(extensions::Dispatcher* extension_dispatcher);
#endif

  // Sets the content setting rules which back |allowImage()|, |allowScript()|,
  // |allowScriptFromSource()| and |allowAutoplay()|. |content_setting_rules|
  // must outlive this |ContentSettingsObserver|.
  void SetContentSettingRules(
      const RendererContentSettingRules* content_setting_rules);
  const RendererContentSettingRules* GetContentSettingRules();

  bool IsPluginTemporarilyAllowed(const std::string& identifier);

  // Sends an IPC notification that the specified content type was blocked.
  void DidBlockContentType(ContentSettingsType settings_type);

  // Sends an IPC notification that the specified content type was blocked
  // with additional metadata.
  void DidBlockContentType(ContentSettingsType settings_type,
                           const base::string16& details);

  // blink::WebContentSettingsClient:
  bool AllowDatabase() override;
  void RequestFileSystemAccessAsync(
      base::OnceCallback<void(bool)> callback) override;
  bool AllowImage(bool enabled_per_settings,
                  const blink::WebURL& image_url) override;
  bool AllowIndexedDB(const blink::WebSecurityOrigin& origin) override;
  bool AllowCacheStorage(const blink::WebSecurityOrigin& origin) override;
  bool AllowScript(bool enabled_per_settings) override;
  bool AllowScriptFromSource(bool enabled_per_settings,
                             const blink::WebURL& script_url) override;
  bool AllowStorage(bool local) override;
  bool AllowReadFromClipboard(bool default_value) override;
  bool AllowWriteToClipboard(bool default_value) override;
  bool AllowMutationEvents(bool default_value) override;
  void DidNotAllowPlugins() override;
  void DidNotAllowScript() override;
  bool AllowRunningInsecureContent(bool allowed_per_settings,
                                   const blink::WebSecurityOrigin& context,
                                   const blink::WebURL& url) override;
  bool AllowAutoplay(bool default_value) override;
  bool AllowPopupsAndRedirects(bool default_value) override;
  void PassiveInsecureContentFound(const blink::WebURL&) override;
  void PersistClientHints(
      const blink::WebEnabledClientHints& enabled_client_hints,
      base::TimeDelta duration,
      const blink::WebURL& url) override;
  void GetAllowedClientHintsFromSource(
      const blink::WebURL& url,
      blink::WebEnabledClientHints* client_hints) const override;

  bool allow_running_insecure_content() const {
    return allow_running_insecure_content_;
  }

 private:
  FRIEND_TEST_ALL_PREFIXES(ContentSettingsObserverTest, WhitelistedSchemes);
  FRIEND_TEST_ALL_PREFIXES(ContentSettingsObserverBrowserTest,
                           ContentSettingsInterstitialPages);
  FRIEND_TEST_ALL_PREFIXES(ContentSettingsObserverBrowserTest,
                           PluginsTemporarilyAllowed);

  // RenderFrameObserver implementation.
  bool OnMessageReceived(const IPC::Message& message) override;
  void DidCommitProvisionalLoad(bool is_same_document_navigation,
                                ui::PageTransition transition) override;
  void OnDestruct() override;

  // chrome::mojom::ContentSettingsRenderer:
  void SetAllowRunningInsecureContent() override;
  void SetAsInterstitial() override;

  void OnContentSettingsRendererRequest(
      mojo::PendingAssociatedReceiver<chrome::mojom::ContentSettingsRenderer>
          receiver);

  // Message handlers.
  void OnLoadBlockedPlugins(const std::string& identifier);
  void OnRequestFileSystemAccessAsyncResponse(int request_id, bool allowed);

  // Resets the |content_blocked_| array.
  void ClearBlockedContentSettings();

  // Whether the observed RenderFrame is for a platform app.
  bool IsPlatformApp();

#if BUILDFLAG(ENABLE_EXTENSIONS)
  // If |origin| corresponds to an installed extension, returns that extension.
  // Otherwise returns NULL.
  const extensions::Extension* GetExtension(
      const blink::WebSecurityOrigin& origin) const;
#endif

  // Helpers.
  // True if |render_frame()| contains content that is white-listed for content
  // settings.
  bool IsWhitelistedForContentSettings() const;

  // Exposed for unit tests.
  static bool IsWhitelistedForContentSettings(
      const blink::WebSecurityOrigin& origin,
      const blink::WebURL& document_url);

#if BUILDFLAG(ENABLE_EXTENSIONS)
  // Owned by ChromeContentRendererClient and outlive us.
  extensions::Dispatcher* extension_dispatcher_ = nullptr;
#endif

  // Insecure content may be permitted for the duration of this render view.
  bool allow_running_insecure_content_ = false;

  // A pointer to content setting rules stored by the renderer. Normally, the
  // |RendererContentSettingRules| object is owned by
  // |ChromeRenderThreadObserver|. In the tests it is owned by the caller of
  // |SetContentSettingRules|.
  const RendererContentSettingRules* content_setting_rules_ = nullptr;

  // Stores if images, scripts, and plugins have actually been blocked.
  base::flat_set<ContentSettingsType> content_blocked_;

  // Caches the result of AllowStorage.
  using StoragePermissionsKey = std::pair<GURL, bool>;
  base::flat_map<StoragePermissionsKey, bool> cached_storage_permissions_;

  // Caches the result of AllowScript.
  base::flat_map<blink::WebFrame*, bool> cached_script_permissions_;

  base::flat_set<std::string> temporarily_allowed_plugins_;
  bool is_interstitial_page_ = false;

  int current_request_id_ = 0;
  base::flat_map<int, base::OnceCallback<void(bool)>> permission_requests_;

  // If true, IsWhitelistedForContentSettings will always return true.
  const bool should_whitelist_;

  mojo::AssociatedReceiverSet<chrome::mojom::ContentSettingsRenderer>
      receivers_;

  DISALLOW_COPY_AND_ASSIGN(ContentSettingsObserver);
};

#endif  // CHROME_RENDERER_CONTENT_SETTINGS_OBSERVER_H_
