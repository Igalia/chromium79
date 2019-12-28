// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/web_contents/web_contents_view_aura.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/containers/flat_set.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/macros.h"
#include "base/message_loop/message_loop_current.h"
#include "base/no_destructor.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "components/viz/common/features.h"
#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/browser/download/drag_download_util.h"
#include "content/browser/frame_host/interstitial_page_impl.h"
#include "content/browser/frame_host/navigation_entry_impl.h"
#include "content/browser/renderer_host/dip_util.h"
#include "content/browser/renderer_host/input/touch_selection_controller_client_aura.h"
#include "content/browser/renderer_host/overscroll_controller.h"
#include "content/browser/renderer_host/render_view_host_factory.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_input_event_router.h"
#include "content/browser/renderer_host/render_widget_host_view_aura.h"
#include "content/browser/web_contents/aura/gesture_nav_simple.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/guest_mode.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/overscroll_configuration.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_view_delegate.h"
#include "content/public/browser/web_drag_dest_delegate.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_features.h"
#include "net/base/filename_util.h"
#include "third_party/blink/public/platform/web_input_event.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/drag_drop_client.h"
#include "ui/aura/client/drag_drop_delegate.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/aura/window_tree_host.h"
#include "ui/aura/window_tree_host_observer.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/custom_data_helper.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/os_exchange_data_provider_factory.h"
#include "ui/base/hit_test.h"
#include "ui/base/ui_base_switches_util.h"
#include "ui/compositor/layer.h"
#include "ui/display/screen.h"
#include "ui/events/blink/web_input_event.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_png_rep.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/touch_selection/touch_selection_controller.h"

namespace content {

WebContentsView* CreateWebContentsView(
    WebContentsImpl* web_contents,
    WebContentsViewDelegate* delegate,
    RenderViewHostDelegateView** render_view_host_delegate_view) {
  WebContentsViewAura* rv = new WebContentsViewAura(web_contents, delegate);
  *render_view_host_delegate_view = rv;
  return rv;
}

namespace {

WebContentsViewAura::RenderWidgetHostViewCreateFunction
    g_create_render_widget_host_view = nullptr;

RenderWidgetHostViewAura* ToRenderWidgetHostViewAura(
    RenderWidgetHostView* view) {
  if (!view || (RenderViewHostFactory::has_factory() &&
      !RenderViewHostFactory::is_real_render_view_host())) {
    return nullptr;  // Can't cast to RenderWidgetHostViewAura in unit tests.
  }

  RenderViewHost* rvh = RenderViewHost::From(view->GetRenderWidgetHost());
  WebContentsImpl* web_contents = static_cast<WebContentsImpl*>(
      rvh ? WebContents::FromRenderViewHost(rvh) : nullptr);
  if (BrowserPluginGuest::IsGuest(web_contents))
    return nullptr;
  return static_cast<RenderWidgetHostViewAura*>(view);
}

// Listens to all mouse drag events during a drag and drop and sends them to
// the renderer.
class WebDragSourceAura : public NotificationObserver {
 public:
  WebDragSourceAura(aura::Window* window, WebContentsImpl* contents)
      : window_(window),
        contents_(contents) {
    registrar_.Add(this,
                   NOTIFICATION_WEB_CONTENTS_DISCONNECTED,
                   Source<WebContents>(contents));
  }

  ~WebDragSourceAura() override {}

  // NotificationObserver:
  void Observe(int type,
               const NotificationSource& source,
               const NotificationDetails& details) override {
    if (type != NOTIFICATION_WEB_CONTENTS_DISCONNECTED)
      return;

    // Cancel the drag if it is still in progress.
    aura::client::DragDropClient* dnd_client =
        aura::client::GetDragDropClient(window_->GetRootWindow());
    if (dnd_client && dnd_client->IsDragDropInProgress())
      dnd_client->DragCancel();

    window_ = nullptr;
    contents_ = nullptr;
  }

  aura::Window* window() const { return window_; }

 private:
  aura::Window* window_;
  WebContentsImpl* contents_;
  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(WebDragSourceAura);
};

#if defined(USE_X11) || defined(OS_WIN)
// Fill out the OSExchangeData with a file contents, synthesizing a name if
// necessary.
void PrepareDragForFileContents(const DropData& drop_data,
                                ui::OSExchangeData::Provider* provider) {
  base::Optional<base::FilePath> filename =
      drop_data.GetSafeFilenameForImageFileContents();
  if (filename)
    provider->SetFileContents(*filename, drop_data.file_contents);
}
#endif

#if defined(OS_WIN)
void PrepareDragForDownload(
    const DropData& drop_data,
    ui::OSExchangeData::Provider* provider,
    WebContentsImpl* web_contents) {
  const GURL& page_url = web_contents->GetLastCommittedURL();
  const std::string& page_encoding = web_contents->GetEncoding();

  // Parse the download metadata.
  base::string16 mime_type;
  base::FilePath file_name;
  GURL download_url;
  if (!ParseDownloadMetadata(drop_data.download_metadata,
                             &mime_type,
                             &file_name,
                             &download_url))
    return;

  // Generate the file name based on both mime type and proposed file name.
  std::string default_name =
      GetContentClient()->browser()->GetDefaultDownloadName();
  base::FilePath generated_download_file_name =
      net::GenerateFileName(download_url,
                            std::string(),
                            std::string(),
                            base::UTF16ToUTF8(file_name.value()),
                            base::UTF16ToUTF8(mime_type),
                            default_name);

  // http://crbug.com/332579
  base::ThreadRestrictions::ScopedAllowIO allow_file_operations;

  base::FilePath temp_dir_path;
  if (!base::CreateNewTempDirectory(FILE_PATH_LITERAL("chrome_drag"),
                                    &temp_dir_path))
    return;

  base::FilePath download_path =
      temp_dir_path.Append(generated_download_file_name);

  // We cannot know when the target application will be done using the temporary
  // file, so schedule it to be deleted after rebooting.
  base::DeleteFileAfterReboot(download_path);
  base::DeleteFileAfterReboot(temp_dir_path);

  // Provide the data as file (CF_HDROP). A temporary download file with the
  // Zone.Identifier ADS (Alternate Data Stream) attached will be created.
  scoped_refptr<DragDownloadFile> download_file =
      new DragDownloadFile(
          download_path,
          base::File(),
          download_url,
          Referrer(page_url, drop_data.referrer_policy),
          page_encoding,
          web_contents);
  ui::OSExchangeData::DownloadFileInfo file_download(base::FilePath(),
                                                     download_file.get());
  provider->SetDownloadFileInfo(file_download);
}
#endif  // defined(OS_WIN)

// Returns the ClipboardFormatType to store file system files.
const ui::ClipboardFormatType& GetFileSystemFileFormatType() {
  static base::NoDestructor<ui::ClipboardFormatType> format(
      ui::ClipboardFormatType::GetType("chromium/x-file-system-files"));
  return *format;
}

// Utility to fill a ui::OSExchangeDataProvider object from DropData.
void PrepareDragData(const DropData& drop_data,
                     ui::OSExchangeData::Provider* provider,
                     WebContentsImpl* web_contents) {
  provider->MarkOriginatedFromRenderer();
#if defined(OS_WIN)
  // Put download before file contents to prefer the download of a image over
  // its thumbnail link.
  if (!drop_data.download_metadata.empty())
    PrepareDragForDownload(drop_data, provider, web_contents);
#endif
#if defined(USE_X11) || defined(OS_WIN)
  // We set the file contents before the URL because the URL also sets file
  // contents (to a .URL shortcut).  We want to prefer file content data over
  // a shortcut so we add it first.
  if (!drop_data.file_contents.empty())
    PrepareDragForFileContents(drop_data, provider);
#endif
  // Call SetString() before SetURL() when we actually have a custom string.
  // SetURL() will itself do SetString() when a string hasn't been set yet,
  // but we want to prefer drop_data.text.string() over the URL string if it
  // exists.
  if (!drop_data.text.string().empty())
    provider->SetString(drop_data.text.string());
  if (drop_data.url.is_valid())
    provider->SetURL(drop_data.url, drop_data.url_title);
  if (!drop_data.html.string().empty())
    provider->SetHtml(drop_data.html.string(), drop_data.html_base_url);
  if (!drop_data.filenames.empty())
    provider->SetFilenames(drop_data.filenames);
  if (!drop_data.file_system_files.empty()) {
    base::Pickle pickle;
    DropData::FileSystemFileInfo::WriteFileSystemFilesToPickle(
        drop_data.file_system_files, &pickle);
    provider->SetPickledData(GetFileSystemFileFormatType(), pickle);
  }
  if (!drop_data.custom_data.empty()) {
    base::Pickle pickle;
    ui::WriteCustomDataToPickle(drop_data.custom_data, &pickle);
    provider->SetPickledData(ui::ClipboardFormatType::GetWebCustomDataType(),
                             pickle);
  }
}

#if defined(OS_WIN)
// Function returning whether this drop target should extract virtual file data
// from the data store.
// (1) As with real files, only add virtual files if the drag did not originate
// in the renderer process. Without this, if an anchor element is dragged and
// then dropped on the same page, the browser will navigate to the URL
// referenced by the anchor. That is because virtual ".url" file data
// (internet shortcut) is added to the data object on drag start, and if
// script doesn't handle the drop, the browser behaves just as if a .url file
// were dragged in from the desktop. Filtering out virtual files if the drag
// is renderer tainted also prevents the possibility of a compromised renderer
// gaining access to the backing temp file paths.
// (2) Even if the drag is not renderer tainted, also exclude virtual files
// if the UniformResourceLocatorW clipboard format is found in the data object.
// Drags initiated in the browser process, such as dragging a bookmark from
// the bookmark bar, will add a virtual .url file to the data object using the
// CFSTR_FILEDESCRIPTORW/CFSTR_FILECONTENTS formats, which represents an
// internet shortcut intended to be  dropped on the desktop. But this causes a
// regression in the behavior of the extensions page (see
// https://crbug.com/963392). The primary scenario for introducing virtual file
// support was for dragging items out of Outlook.exe for upload to a file
// hosting service. The Outlook drag source does not add url data to the data
// object.
// TODO(https://crbug.com/958273): DragDrop: Extend virtual filename support
// to DropData, for parity with real filename support.
// TODO(https://crbug.com/964461): Drag and drop: Should support both virtual
// file and url data on drop.
bool ShouldIncludeVirtualFiles(const DropData& drop_data) {
  return !drop_data.did_originate_from_renderer && drop_data.url.is_empty();
}
#endif

// Utility to fill a DropData object from ui::OSExchangeData.
void PrepareDropData(DropData* drop_data, const ui::OSExchangeData& data) {
  drop_data->did_originate_from_renderer = data.DidOriginateFromRenderer();

  base::string16 plain_text;
  data.GetString(&plain_text);
  if (!plain_text.empty())
    drop_data->text = base::NullableString16(plain_text, false);

  GURL url;
  base::string16 url_title;
  data.GetURLAndTitle(
      ui::OSExchangeData::DO_NOT_CONVERT_FILENAMES, &url, &url_title);
  if (url.is_valid()) {
    drop_data->url = url;
    drop_data->url_title = url_title;
  }

  base::string16 html;
  GURL html_base_url;
  data.GetHtml(&html, &html_base_url);
  if (!html.empty())
    drop_data->html = base::NullableString16(html, false);
  if (html_base_url.is_valid())
    drop_data->html_base_url = html_base_url;

  data.GetFilenames(&drop_data->filenames);

#if defined(OS_WIN)
  // Get a list of virtual files for later retrieval when a drop is performed
  // (will return empty vector if there are any non-virtual files in the data
  // store).
  if (ShouldIncludeVirtualFiles(*drop_data))
    data.GetVirtualFilenames(&drop_data->filenames);
#endif

  base::Pickle pickle;
  std::vector<DropData::FileSystemFileInfo> file_system_files;
  if (data.GetPickledData(GetFileSystemFileFormatType(), &pickle) &&
      DropData::FileSystemFileInfo::ReadFileSystemFilesFromPickle(
          pickle, &file_system_files))
    drop_data->file_system_files = file_system_files;

  if (data.GetPickledData(ui::ClipboardFormatType::GetWebCustomDataType(),
                          &pickle))
    ui::ReadCustomDataIntoMap(
        pickle.data(), pickle.size(), &drop_data->custom_data);
}

// Utilities to convert between blink::WebDragOperationsMask and
// ui::DragDropTypes.
int ConvertFromWeb(blink::WebDragOperationsMask ops) {
  int drag_op = ui::DragDropTypes::DRAG_NONE;
  if (ops & blink::kWebDragOperationCopy)
    drag_op |= ui::DragDropTypes::DRAG_COPY;
  if (ops & blink::kWebDragOperationMove)
    drag_op |= ui::DragDropTypes::DRAG_MOVE;
  if (ops & blink::kWebDragOperationLink)
    drag_op |= ui::DragDropTypes::DRAG_LINK;
  return drag_op;
}

blink::WebDragOperationsMask ConvertToWeb(int drag_op) {
  int web_drag_op = blink::kWebDragOperationNone;
  if (drag_op & ui::DragDropTypes::DRAG_COPY)
    web_drag_op |= blink::kWebDragOperationCopy;
  if (drag_op & ui::DragDropTypes::DRAG_MOVE)
    web_drag_op |= blink::kWebDragOperationMove;
  if (drag_op & ui::DragDropTypes::DRAG_LINK)
    web_drag_op |= blink::kWebDragOperationLink;
  return (blink::WebDragOperationsMask) web_drag_op;
}

GlobalRoutingID GetRenderViewHostID(RenderViewHost* rvh) {
  return GlobalRoutingID(rvh->GetProcess()->GetID(), rvh->GetRoutingID());
}

// Returns the host window for |window|, or nullpr if it has no host window.
aura::Window* GetHostWindow(aura::Window* window) {
  aura::Window* host_window = window->GetProperty(aura::client::kHostWindowKey);
  if (host_window)
    return host_window;
  return window->parent();
}

}  // namespace

WebContentsViewAura::OnPerformDropContext::OnPerformDropContext(
    RenderWidgetHostImpl* target_rwh,
    const ui::DropTargetEvent& event,
    std::unique_ptr<ui::OSExchangeData> data,
    base::ScopedClosureRunner end_drag_runner,
    base::Optional<gfx::PointF> transformed_pt,
    gfx::PointF screen_pt)
    : target_rwh(target_rwh->GetWeakPtr()),
      event(event),
      data(std::move(data)),
      end_drag_runner(std::move(end_drag_runner)),
      transformed_pt(std::move(transformed_pt)),
      screen_pt(screen_pt) {}

WebContentsViewAura::OnPerformDropContext::OnPerformDropContext(
    OnPerformDropContext&&) = default;

WebContentsViewAura::OnPerformDropContext::~OnPerformDropContext() = default;

#if defined(OS_WIN)
// A web contents observer that watches for navigations while an async drop
// operation is in progress during virtual file data retrieval and temp file
// creation. Navigations may cause completion of the drop to be disallowed. The
// class also serves to cache the drop parameters as they were at the beginning
// of the drop. This is needed for checking that the drop target is still valid
// when the async operation completes, and if so, passing the parameters on to
// the render widget host to complete the drop.
class WebContentsViewAura::AsyncDropNavigationObserver
    : public WebContentsObserver {
 public:
  AsyncDropNavigationObserver(WebContents* watched_contents,
                              std::unique_ptr<DropData> drop_data,
                              base::ScopedClosureRunner end_drag_runner,
                              RenderWidgetHostImpl* target_rwh,
                              const gfx::PointF& client_pt,
                              const gfx::PointF& screen_pt,
                              int key_modifiers);

  // WebContentsObserver:
  void DidFinishNavigation(NavigationHandle* navigation_handle) override;

  // Was a navigation observed while the async drop was being processed that
  // should disallow the drop?
  bool drop_allowed() const { return drop_allowed_; }

  DropData* drop_data() const { return drop_data_.get(); }
  RenderWidgetHostImpl* target_rwh() const { return target_rwh_.get(); }
  const gfx::PointF& client_pt() const { return client_pt_; }
  const gfx::PointF& screen_pt() const { return screen_pt_; }
  int key_modifiers() const { return key_modifiers_; }

 private:
  bool drop_allowed_;

  // Data cached at the start of the drop operation and needed to complete the
  // drop.
  std::unique_ptr<DropData> drop_data_;
  base::ScopedClosureRunner end_drag_runner_;
  base::WeakPtr<RenderWidgetHostImpl> target_rwh_;
  const gfx::PointF client_pt_;
  const gfx::PointF screen_pt_;
  const int key_modifiers_;

  DISALLOW_COPY_AND_ASSIGN(AsyncDropNavigationObserver);
};

WebContentsViewAura::AsyncDropNavigationObserver::AsyncDropNavigationObserver(
    WebContents* watched_contents,
    std::unique_ptr<DropData> drop_data,
    base::ScopedClosureRunner end_drag_runner,
    RenderWidgetHostImpl* target_rwh,
    const gfx::PointF& client_pt,
    const gfx::PointF& screen_pt,
    int key_modifiers)
    : WebContentsObserver(watched_contents),
      drop_allowed_(true),
      drop_data_(std::move(drop_data)),
      end_drag_runner_(std::move(end_drag_runner)),
      target_rwh_(target_rwh->GetWeakPtr()),
      client_pt_(client_pt),
      screen_pt_(screen_pt),
      key_modifiers_(key_modifiers) {}

void WebContentsViewAura::AsyncDropNavigationObserver::DidFinishNavigation(
    NavigationHandle* navigation_handle) {
  // This method is called every time any navigation completes in the observed
  // web contents, including subframe navigations. In the case of a subframe
  // navigation, we can't readily determine on the browser process side if the
  // navigated subframe is the intended drop target. Err on the side of security
  // and disallow the drop if any navigation commits to a different url.
  if (navigation_handle->HasCommitted() &&
      (navigation_handle->GetURL() != navigation_handle->GetPreviousURL())) {
    drop_allowed_ = false;
  }
}

// Deletes registered temp files asynchronously when the object goes out of
// scope (when the WebContentsViewAura is deleted on tab closure).
class WebContentsViewAura::AsyncDropTempFileDeleter {
 public:
  AsyncDropTempFileDeleter() = default;
  ~AsyncDropTempFileDeleter();
  void RegisterFile(const base::FilePath& path);

 private:
  void DeleteAllFilesAsync() const;
  void DeleteFileAsync(const base::FilePath& path) const;

  std::vector<base::FilePath> scoped_files_to_delete_;
  DISALLOW_COPY_AND_ASSIGN(AsyncDropTempFileDeleter);
};

WebContentsViewAura::AsyncDropTempFileDeleter::~AsyncDropTempFileDeleter() {
  DeleteAllFilesAsync();
}

void WebContentsViewAura::AsyncDropTempFileDeleter::RegisterFile(
    const base::FilePath& path) {
  scoped_files_to_delete_.push_back(path);
}

void WebContentsViewAura::AsyncDropTempFileDeleter::DeleteAllFilesAsync()
    const {
  for (const auto& path : scoped_files_to_delete_)
    DeleteFileAsync(path);
}

void WebContentsViewAura::AsyncDropTempFileDeleter::DeleteFileAsync(
    const base::FilePath& path) const {
  base::PostTask(
      FROM_HERE,
      {base::ThreadPool(), base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce(base::IgnoreResult(&base::DeleteFile), std::move(path),
                     false));
}
#endif

class WebContentsViewAura::WindowObserver
    : public aura::WindowObserver, public aura::WindowTreeHostObserver {
 public:
  explicit WindowObserver(WebContentsViewAura* view) : view_(view) {
    view_->window_->AddObserver(this);
  }

  ~WindowObserver() override {
    view_->window_->RemoveObserver(this);
    if (view_->window_->GetHost())
      view_->window_->GetHost()->RemoveObserver(this);
    if (host_window_)
      host_window_->RemoveObserver(this);
  }

  void OnWindowParentChanged(aura::Window* window,
                             aura::Window* parent) override {
    if (window != view_->window_.get())
      return;

    aura::Window* const host_window = GetHostWindow(window);

    if (host_window_)
      host_window_->RemoveObserver(this);

    host_window_ = host_window;
    if (host_window)
      host_window->AddObserver(this);
  }

  void OnWindowBoundsChanged(aura::Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds,
                             ui::PropertyChangeReason reason) override {
    DCHECK(window == host_window_ || window == view_->window_.get());
    if (pending_window_changes_) {
      pending_window_changes_->window_bounds_changed = true;
      if (old_bounds.origin() != new_bounds.origin())
        pending_window_changes_->window_origin_changed = true;
      return;
    }
    ProcessWindowBoundsChange(old_bounds.origin() != new_bounds.origin());
  }

  void OnWindowDestroying(aura::Window* window) override {
    if (window == host_window_) {
      host_window_->RemoveObserver(this);
      host_window_ = nullptr;
    }
  }

  void OnWindowAddedToRootWindow(aura::Window* window) override {
    if (window == view_->window_.get())
      window->GetHost()->AddObserver(this);
  }

  void OnWindowRemovingFromRootWindow(aura::Window* window,
                                      aura::Window* new_root) override {
    if (window == view_->window_.get()) {
      window->GetHost()->RemoveObserver(this);
      pending_window_changes_.reset();
    }
  }

  // Overridden WindowTreeHostObserver:
  void OnHostWillProcessBoundsChange(aura::WindowTreeHost* host) override {
    DCHECK(!pending_window_changes_);
    pending_window_changes_ = std::make_unique<PendingWindowChanges>();
  }

  void OnHostDidProcessBoundsChange(aura::WindowTreeHost* host) override {
    if (!pending_window_changes_)
      return;  // Happens if added to a new host during bounds change.

    if (pending_window_changes_->window_bounds_changed)
      ProcessWindowBoundsChange(pending_window_changes_->window_origin_changed);
    else if (pending_window_changes_->host_moved)
      ProcessHostMovedInPixels();
    pending_window_changes_.reset();
  }

  void OnHostMovedInPixels(aura::WindowTreeHost* host,
                           const gfx::Point& new_origin_in_pixels) override {
    if (pending_window_changes_) {
      pending_window_changes_->host_moved = true;
      return;
    }
    ProcessHostMovedInPixels();
  }

 private:
  // Used to avoid multiple calls to SendScreenRects(). In particular, when
  // WindowTreeHost changes its size, it's entirely likely the aura::Windows
  // will change as well. When OnHostWillProcessBoundsChange() is called,
  // |pending_window_changes_| is created and any changes are set in it.
  // In OnHostDidProcessBoundsChange() is called, all accumulated changes are
  // applied.
  struct PendingWindowChanges {
    // Set to true if OnWindowBoundsChanged() is called.
    bool window_bounds_changed = false;

    // Set to true if OnWindowBoundsChanged() is called *and* the origin of the
    // window changed.
    bool window_origin_changed = false;

    // Set to true if OnHostMovedInPixels() is called.
    bool host_moved = false;
  };

  void ProcessWindowBoundsChange(bool did_origin_change) {
    SendScreenRects();
    if (did_origin_change) {
      TouchSelectionControllerClientAura* selection_controller_client =
          view_->GetSelectionControllerClient();
      if (selection_controller_client)
        selection_controller_client->OnWindowMoved();
    }
  }

  void ProcessHostMovedInPixels() {
    // NOTE: this function is *not* called if OnHostWillProcessBoundsChange()
    // *and* the bounds changes (OnWindowBoundsChanged() is called).
    TRACE_EVENT1(
        "ui", "WebContentsViewAura::WindowObserver::OnHostMovedInPixels",
        "new_origin_in_pixels",
        view_->window_->GetHost()->GetBoundsInPixels().origin().ToString());
    SendScreenRects();
  }

  void SendScreenRects() { view_->web_contents_->SendScreenRects(); }

  WebContentsViewAura* view_;

  // The parent window that hosts the constrained windows. We cache the old host
  // view so that we can unregister when it's not the parent anymore.
  aura::Window* host_window_ = nullptr;

  std::unique_ptr<PendingWindowChanges> pending_window_changes_;

  DISALLOW_COPY_AND_ASSIGN(WindowObserver);
};

// static
void WebContentsViewAura::InstallCreateHookForTests(
    RenderWidgetHostViewCreateFunction create_render_widget_host_view) {
  CHECK_EQ(nullptr, g_create_render_widget_host_view);
  g_create_render_widget_host_view = create_render_widget_host_view;
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, public:

WebContentsViewAura::WebContentsViewAura(WebContentsImpl* web_contents,
                                         WebContentsViewDelegate* delegate)
    : web_contents_(web_contents),
      delegate_(delegate),
      current_drag_op_(blink::kWebDragOperationNone),
      drag_dest_delegate_(nullptr),
      current_rvh_for_drag_(ChildProcessHost::kInvalidUniqueID,
                            MSG_ROUTING_NONE),
      drag_start_process_id_(ChildProcessHost::kInvalidUniqueID),
      drag_start_view_id_(ChildProcessHost::kInvalidUniqueID, MSG_ROUTING_NONE),
      drag_in_progress_(false),
      init_rwhv_with_null_parent_for_testing_(false) {}

void WebContentsViewAura::SetDelegateForTesting(
    WebContentsViewDelegate* delegate) {
  delegate_.reset(delegate);
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, private:

WebContentsViewAura::~WebContentsViewAura() {
  if (!window_)
    return;

  window_observer_.reset();

  // Window needs a valid delegate during its destructor, so we explicitly
  // delete it here.
  window_.reset();
}

void WebContentsViewAura::SizeChangedCommon(const gfx::Size& size) {
  if (web_contents_->GetInterstitialPage())
    web_contents_->GetInterstitialPage()->SetSize(size);
  RenderWidgetHostView* rwhv =
      web_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->SetSize(size);
}

void WebContentsViewAura::EndDrag(RenderWidgetHost* source_rwh,
                                  blink::WebDragOperationsMask ops) {
  drag_start_process_id_ = ChildProcessHost::kInvalidUniqueID;
  drag_start_view_id_ = GlobalRoutingID(ChildProcessHost::kInvalidUniqueID,
                                        MSG_ROUTING_NONE);

  if (!web_contents_)
    return;

  aura::Window* window = GetContentNativeView();
  gfx::PointF screen_loc =
      gfx::PointF(display::Screen::GetScreen()->GetCursorScreenPoint());
  gfx::PointF client_loc = screen_loc;
  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(window->GetRootWindow());
  if (screen_position_client)
    screen_position_client->ConvertPointFromScreen(window, &client_loc);

  // |client_loc| is in the root coordinate space, for non-root
  // RenderWidgetHosts it needs to be transformed.
  gfx::PointF transformed_point = client_loc;
  if (source_rwh && web_contents_->GetRenderWidgetHostView()) {
    static_cast<RenderWidgetHostViewBase*>(
        web_contents_->GetRenderWidgetHostView())
        ->TransformPointToCoordSpaceForView(
            client_loc,
            static_cast<RenderWidgetHostViewBase*>(source_rwh->GetView()),
            &transformed_point);
  }

  web_contents_->DragSourceEndedAt(transformed_point.x(), transformed_point.y(),
                                   screen_loc.x(), screen_loc.y(), ops,
                                   source_rwh);

  web_contents_->SystemDragEnded(source_rwh);
}

void WebContentsViewAura::InstallOverscrollControllerDelegate(
    RenderWidgetHostViewAura* view) {
  if (!base::FeatureList::IsEnabled(features::kOverscrollHistoryNavigation))
    return;

  if (!gesture_nav_simple_)
    gesture_nav_simple_ = std::make_unique<GestureNavSimple>(web_contents_);
  if (view)
    view->overscroll_controller()->set_delegate(gesture_nav_simple_.get());
}

ui::TouchSelectionController* WebContentsViewAura::GetSelectionController()
    const {
  RenderWidgetHostViewAura* view =
      ToRenderWidgetHostViewAura(web_contents_->GetRenderWidgetHostView());
  return view ? view->selection_controller() : nullptr;
}

TouchSelectionControllerClientAura*
WebContentsViewAura::GetSelectionControllerClient() const {
  RenderWidgetHostViewAura* view =
      ToRenderWidgetHostViewAura(web_contents_->GetRenderWidgetHostView());
  return view ? view->selection_controller_client() : nullptr;
}

gfx::NativeView WebContentsViewAura::GetRenderWidgetHostViewParent() const {
  if (init_rwhv_with_null_parent_for_testing_)
    return nullptr;
  return window_.get();
}

bool WebContentsViewAura::IsValidDragTarget(
    RenderWidgetHostImpl* target_rwh) const {
  return target_rwh->GetProcess()->GetID() == drag_start_process_id_ ||
      GetRenderViewHostID(web_contents_->GetRenderViewHost()) !=
      drag_start_view_id_;
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, WebContentsView implementation:

gfx::NativeView WebContentsViewAura::GetNativeView() const {
  return window_.get();
}

gfx::NativeView WebContentsViewAura::GetContentNativeView() const {
  RenderWidgetHostView* rwhv = web_contents_->GetRenderWidgetHostView();
  return rwhv ? rwhv->GetNativeView() : nullptr;
}

gfx::NativeWindow WebContentsViewAura::GetTopLevelNativeWindow() const {
  gfx::NativeWindow window = window_->GetToplevelWindow();
  return window ? window : delegate_->GetNativeWindow();
}

void WebContentsViewAura::GetContainerBounds(gfx::Rect* out) const {
  *out = GetNativeView()->GetBoundsInScreen();
}

void WebContentsViewAura::SizeContents(const gfx::Size& size) {
  gfx::Rect bounds = window_->bounds();
  if (bounds.size() != size) {
    bounds.set_size(size);
    window_->SetBounds(bounds);
  } else {
    // Our size matches what we want but the renderers size may not match.
    // Pretend we were resized so that the renderers size is updated too.
    SizeChangedCommon(size);
  }
}

void WebContentsViewAura::Focus() {
  if (delegate_)
    delegate_->ResetStoredFocus();

  if (web_contents_->GetInterstitialPage()) {
    web_contents_->GetInterstitialPage()->Focus();
    return;
  }

  if (delegate_ && delegate_->Focus())
    return;

  RenderWidgetHostView* rwhv =
      web_contents_->GetFullscreenRenderWidgetHostView();
  if (!rwhv)
    rwhv = web_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->Focus();
}

void WebContentsViewAura::SetInitialFocus() {
  if (delegate_)
    delegate_->ResetStoredFocus();

  if (web_contents_->FocusLocationBarByDefault())
    web_contents_->SetFocusToLocationBar();
  else
    Focus();
}

void WebContentsViewAura::StoreFocus() {
  if (delegate_)
    delegate_->StoreFocus();
}

void WebContentsViewAura::RestoreFocus() {
  if (delegate_ && delegate_->RestoreFocus())
    return;
  SetInitialFocus();
}

void WebContentsViewAura::FocusThroughTabTraversal(bool reverse) {
  if (delegate_)
    delegate_->ResetStoredFocus();

  if (web_contents_->ShowingInterstitialPage()) {
    web_contents_->GetInterstitialPage()->FocusThroughTabTraversal(reverse);
    return;
  }
  content::RenderWidgetHostView* fullscreen_view =
      web_contents_->GetFullscreenRenderWidgetHostView();
  if (fullscreen_view) {
    fullscreen_view->Focus();
    return;
  }
  web_contents_->GetRenderViewHost()->SetInitialFocus(reverse);
}

DropData* WebContentsViewAura::GetDropData() const {
  return current_drop_data_.get();
}

gfx::Rect WebContentsViewAura::GetViewBounds() const {
  return GetNativeView()->GetBoundsInScreen();
}

void WebContentsViewAura::CreateAuraWindow(aura::Window* context) {
  DCHECK(aura::Env::HasInstance());
  DCHECK(!window_);
  window_ =
      std::make_unique<aura::Window>(this, aura::client::WINDOW_TYPE_CONTROL);
  window_->set_owned_by_parent(false);
  window_->SetName("WebContentsViewAura");
  window_->Init(ui::LAYER_NOT_DRAWN);
  aura::Window* root_window = context ? context->GetRootWindow() : nullptr;
  if (root_window) {
    // There are places where there is no context currently because object
    // hierarchies are built before they're attached to a Widget. (See
    // views::WebView as an example; GetWidget() returns NULL at the point
    // where we are created.)
    //
    // It should be OK to not set a default parent since such users will
    // explicitly add this WebContentsViewAura to their tree after they create
    // us.
    aura::client::ParentWindowWithContext(window_.get(), root_window,
                                          root_window->GetBoundsInScreen());
  }
  window_->layer()->SetMasksToBounds(true);
  window_->TrackOcclusionState();

  // WindowObserver is not interesting and is problematic for Browser Plugin
  // guests.
  // The use cases for WindowObserver do not apply to Browser Plugins:
  // 1) guests do not support NPAPI plugins.
  // 2) guests' window bounds are supposed to come from its embedder.
  if (!BrowserPluginGuest::IsGuest(web_contents_))
    window_observer_.reset(new WindowObserver(this));
}

void WebContentsViewAura::UpdateWebContentsVisibility() {
  web_contents_->UpdateWebContentsVisibility(GetVisibility());
}

Visibility WebContentsViewAura::GetVisibility() const {
  if (window_->occlusion_state() == aura::Window::OcclusionState::VISIBLE)
    return Visibility::VISIBLE;

  if (window_->occlusion_state() == aura::Window::OcclusionState::OCCLUDED)
    return Visibility::OCCLUDED;

  DCHECK_EQ(window_->occlusion_state(), aura::Window::OcclusionState::HIDDEN);
  return Visibility::HIDDEN;
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, WebContentsView implementation:

void WebContentsViewAura::CreateView(gfx::NativeView context) {
  CreateAuraWindow(context);

  // delegate_->GetDragDestDelegate() creates a new delegate on every call.
  // Hence, we save a reference to it locally. Similar model is used on other
  // platforms as well.
  if (delegate_)
    drag_dest_delegate_ = delegate_->GetDragDestDelegate();
}

RenderWidgetHostViewBase* WebContentsViewAura::CreateViewForWidget(
    RenderWidgetHost* render_widget_host, bool is_guest_view_hack) {
  if (render_widget_host->GetView()) {
    // During testing, the view will already be set up in most cases to the
    // test view, so we don't want to clobber it with a real one. To verify that
    // this actually is happening (and somebody isn't accidentally creating the
    // view twice), we check for the RVH Factory, which will be set when we're
    // making special ones (which go along with the special views).
    DCHECK(RenderViewHostFactory::has_factory());
    return static_cast<RenderWidgetHostViewBase*>(
        render_widget_host->GetView());
  }

  RenderWidgetHostViewAura* view =
      g_create_render_widget_host_view
          ? g_create_render_widget_host_view(render_widget_host,
                                             is_guest_view_hack)
          : new RenderWidgetHostViewAura(render_widget_host,
                                         is_guest_view_hack);
  view->InitAsChild(GetRenderWidgetHostViewParent());

  RenderWidgetHostImpl* host_impl =
      RenderWidgetHostImpl::From(render_widget_host);

  if (!host_impl->is_hidden())
    view->Show();

  // We listen to drag drop events in the newly created view's window.
  aura::client::SetDragDropDelegate(view->GetNativeView(), this);

  if (view->overscroll_controller() &&
      (!web_contents_->GetDelegate() ||
       web_contents_->GetDelegate()->CanOverscrollContent())) {
    InstallOverscrollControllerDelegate(view);
  }

  return view;
}

RenderWidgetHostViewBase* WebContentsViewAura::CreateViewForChildWidget(
    RenderWidgetHost* render_widget_host) {
  return new RenderWidgetHostViewAura(render_widget_host, false);
}

void WebContentsViewAura::SetPageTitle(const base::string16& title) {
  window_->SetTitle(title);
  aura::Window* child_window = GetContentNativeView();
  if (child_window)
    child_window->SetTitle(title);
}

void WebContentsViewAura::RenderViewCreated(RenderViewHost* host) {
}

void WebContentsViewAura::RenderViewReady() {}

void WebContentsViewAura::RenderViewHostChanged(RenderViewHost* old_host,
                                                RenderViewHost* new_host) {}

void WebContentsViewAura::SetOverscrollControllerEnabled(bool enabled) {
  RenderWidgetHostViewAura* view =
      ToRenderWidgetHostViewAura(web_contents_->GetRenderWidgetHostView());
  if (view)
    view->SetOverscrollControllerEnabled(enabled);
  if (enabled)
    InstallOverscrollControllerDelegate(view);
  else
    gesture_nav_simple_.reset();
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, RenderViewHostDelegateView implementation:

void WebContentsViewAura::ShowContextMenu(RenderFrameHost* render_frame_host,
                                          const ContextMenuParams& params) {
  TouchSelectionControllerClientAura* selection_controller_client =
      GetSelectionControllerClient();
  if (selection_controller_client &&
      selection_controller_client->HandleContextMenu(params)) {
    return;
  }

  if (delegate_) {
    delegate_->ShowContextMenu(render_frame_host, params);
    // WARNING: we may have been deleted during the call to ShowContextMenu().
  }
}

void WebContentsViewAura::StartDragging(
    const DropData& drop_data,
    blink::WebDragOperationsMask operations,
    const gfx::ImageSkia& image,
    const gfx::Vector2d& image_offset,
    const DragEventSourceInfo& event_info,
    RenderWidgetHostImpl* source_rwh) {
  aura::Window* root_window = GetNativeView()->GetRootWindow();
  if (!aura::client::GetDragDropClient(root_window)) {
    web_contents_->SystemDragEnded(source_rwh);
    return;
  }

  // Grab a weak pointer to the RenderWidgetHost, since it can be destroyed
  // during the drag and drop nested run loop in StartDragAndDrop.
  // For example, the RenderWidgetHost can be deleted if a cross-process
  // transfer happens while dragging, since the RenderWidgetHost is deleted in
  // that case.
  base::WeakPtr<RenderWidgetHostImpl> source_rwh_weak_ptr =
      source_rwh->GetWeakPtr();

  drag_start_process_id_ = source_rwh->GetProcess()->GetID();
  drag_start_view_id_ = GetRenderViewHostID(web_contents_->GetRenderViewHost());

  ui::TouchSelectionController* selection_controller = GetSelectionController();
  if (selection_controller)
    selection_controller->HideAndDisallowShowingAutomatically();
  std::unique_ptr<ui::OSExchangeData::Provider> provider =
      ui::OSExchangeDataProviderFactory::CreateProvider();
  PrepareDragData(drop_data, provider.get(), web_contents_);

  auto data(std::make_unique<ui::OSExchangeData>(
      std::move(provider)));  // takes ownership of |provider|.

  if (!image.isNull())
    data->provider().SetDragImage(image, image_offset);

  std::unique_ptr<WebDragSourceAura> drag_source(
      new WebDragSourceAura(GetNativeView(), web_contents_));

  // We need to enable recursive tasks on the message loop so we can get
  // updates while in the system DoDragDrop loop.
  int result_op = 0;
  {
    gfx::NativeView content_native_view = GetContentNativeView();
    base::MessageLoopCurrent::ScopedNestableTaskAllower allow;
    result_op = aura::client::GetDragDropClient(root_window)
                    ->StartDragAndDrop(
                        std::move(data), root_window, content_native_view,
                        event_info.event_location, ConvertFromWeb(operations),
                        event_info.event_source);
  }

  // Bail out immediately if the contents view window is gone. Note that it is
  // not safe to access any class members in this case since |this| may already
  // be destroyed. The local variable |drag_source| will still be valid though,
  // so we can use it to determine if the window is gone.
  if (!drag_source->window()) {
    // Note that in this case, we don't need to call SystemDragEnded() since the
    // renderer is going away.
    return;
  }

  // If drag is still in progress that means we haven't received drop targeting
  // callback yet. So we have to make sure to delay calling EndDrag until drop
  // is done.
  if (!drag_in_progress_)
    EndDrag(source_rwh_weak_ptr.get(), ConvertToWeb(result_op));
  else
    end_drag_runner_ = base::ScopedClosureRunner(base::BindOnce(
        &WebContentsViewAura::EndDrag, weak_ptr_factory_.GetWeakPtr(),
        source_rwh_weak_ptr.get(), ConvertToWeb(result_op)));
}

void WebContentsViewAura::UpdateDragCursor(blink::WebDragOperation operation) {
  current_drag_op_ = operation;
}

void WebContentsViewAura::GotFocus(RenderWidgetHostImpl* render_widget_host) {
  web_contents_->NotifyWebContentsFocused(render_widget_host);
}

void WebContentsViewAura::LostFocus(RenderWidgetHostImpl* render_widget_host) {
  web_contents_->NotifyWebContentsLostFocus(render_widget_host);
}

void WebContentsViewAura::TakeFocus(bool reverse) {
  if (web_contents_->GetDelegate() &&
      !web_contents_->GetDelegate()->TakeFocus(web_contents_, reverse) &&
      delegate_.get()) {
    delegate_->TakeFocus(reverse);
  }
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, aura::WindowDelegate implementation:

gfx::Size WebContentsViewAura::GetMinimumSize() const {
  return gfx::Size();
}

gfx::Size WebContentsViewAura::GetMaximumSize() const {
  return gfx::Size();
}

void WebContentsViewAura::OnBoundsChanged(const gfx::Rect& old_bounds,
                                          const gfx::Rect& new_bounds) {
  SizeChangedCommon(new_bounds.size());

  // Constrained web dialogs, need to be kept centered over our content area.
  for (size_t i = 0; i < window_->children().size(); i++) {
    if (window_->children()[i]->GetProperty(
            aura::client::kConstrainedWindowKey)) {
      gfx::Rect bounds = window_->children()[i]->bounds();
      bounds.set_origin(
          gfx::Point((new_bounds.width() - bounds.width()) / 2,
                     (new_bounds.height() - bounds.height()) / 2));
      window_->children()[i]->SetBounds(bounds);
    }
  }
}

gfx::NativeCursor WebContentsViewAura::GetCursor(const gfx::Point& point) {
  return gfx::kNullCursor;
}

int WebContentsViewAura::GetNonClientComponent(const gfx::Point& point) const {
  return HTCLIENT;
}

bool WebContentsViewAura::ShouldDescendIntoChildForEventHandling(
    aura::Window* child,
    const gfx::Point& location) {
  return true;
}

bool WebContentsViewAura::CanFocus() {
  // Do not take the focus if the render widget host view aura is gone or
  // is in the process of shutting down because neither the view window nor
  // this window can handle key events.
  RenderWidgetHostViewAura* view = ToRenderWidgetHostViewAura(
      web_contents_->GetRenderWidgetHostView());
  if (view != nullptr && !view->IsClosing())
    return true;

  return false;
}

void WebContentsViewAura::OnCaptureLost() {
}

void WebContentsViewAura::OnPaint(const ui::PaintContext& context) {
}

void WebContentsViewAura::OnDeviceScaleFactorChanged(
    float old_device_scale_factor,
    float new_device_scale_factor) {}

void WebContentsViewAura::OnWindowDestroying(aura::Window* window) {}

void WebContentsViewAura::OnWindowDestroyed(aura::Window* window) {
}

void WebContentsViewAura::OnWindowTargetVisibilityChanged(bool visible) {
}

void WebContentsViewAura::OnWindowOcclusionChanged(
    aura::Window::OcclusionState occlusion_state,
    const SkRegion&) {
  UpdateWebContentsVisibility();
}

bool WebContentsViewAura::HasHitTestMask() const {
  return false;
}

void WebContentsViewAura::GetHitTestMask(SkPath* mask) const {}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, ui::EventHandler implementation:

void WebContentsViewAura::OnKeyEvent(ui::KeyEvent* event) {
}

void WebContentsViewAura::OnMouseEvent(ui::MouseEvent* event) {
  if (!web_contents_->GetDelegate())
    return;

  ui::EventType type = event->type();
  if (type == ui::ET_MOUSE_PRESSED) {
    // Linux window managers like to handle raise-on-click themselves.  If we
    // raise-on-click manually, this may override user settings that prevent
    // focus-stealing.
#if !defined(USE_X11)
    web_contents_->GetDelegate()->ActivateContents(web_contents_);
#endif
  }

  web_contents_->GetDelegate()->ContentsMouseEvent(
      web_contents_, type == ui::ET_MOUSE_MOVED, type == ui::ET_MOUSE_EXITED);
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, aura::client::DragDropDelegate implementation:

void WebContentsViewAura::DragEnteredCallback(
    ui::DropTargetEvent event,
    std::unique_ptr<DropData> drop_data,
    base::WeakPtr<RenderWidgetHostViewBase> target,
    base::Optional<gfx::PointF> transformed_pt) {
  drag_in_progress_ = true;
  if (!target)
    return;
  RenderWidgetHostImpl* target_rwh =
      RenderWidgetHostImpl::From(target->GetRenderWidgetHost());
  if (!IsValidDragTarget(target_rwh))
    return;

  current_rwh_for_drag_ = target_rwh->GetWeakPtr();
  current_rvh_for_drag_ =
      GetRenderViewHostID(web_contents_->GetRenderViewHost());
  current_drop_data_.reset(drop_data.release());
  current_rwh_for_drag_->FilterDropData(current_drop_data_.get());

  blink::WebDragOperationsMask op = ConvertToWeb(event.source_operations());

  // Give the delegate an opportunity to cancel the drag.
  if (web_contents_->GetDelegate() &&
      !web_contents_->GetDelegate()->CanDragEnter(
          web_contents_, *current_drop_data_.get(), op)) {
    current_drop_data_.reset(nullptr);
    return;
  }

  DCHECK(transformed_pt.has_value());
  gfx::PointF screen_pt(display::Screen::GetScreen()->GetCursorScreenPoint());
  current_rwh_for_drag_->DragTargetDragEnter(
      *current_drop_data_, transformed_pt.value(), screen_pt, op,
      ui::EventFlagsToWebEventModifiers(event.flags()));

  if (drag_dest_delegate_) {
    drag_dest_delegate_->OnDragEnter();
  }
}

void WebContentsViewAura::OnDragEntered(const ui::DropTargetEvent& event) {
#if defined(OS_WIN)
  async_drop_navigation_observer_.reset();
#endif

  std::unique_ptr<DropData> drop_data = std::make_unique<DropData>();
  // Calling this here as event.data might become invalid inside the callback.
  PrepareDropData(drop_data.get(), event.data());

  if (drag_dest_delegate_) {
    drag_dest_delegate_->DragInitialize(web_contents_);
    drag_dest_delegate_->OnReceiveDragData(event.data());
  }

  web_contents_->GetInputEventRouter()
      ->GetRenderWidgetHostAtPointAsynchronously(
          web_contents_->GetRenderViewHost()->GetWidget()->GetView(),
          event.location_f(),
          base::BindOnce(&WebContentsViewAura::DragEnteredCallback,
                         weak_ptr_factory_.GetWeakPtr(), event,
                         std::move(drop_data)));
}

void WebContentsViewAura::DragUpdatedCallback(
    ui::DropTargetEvent event,
    std::unique_ptr<DropData> drop_data,
    base::WeakPtr<RenderWidgetHostViewBase> target,
    base::Optional<gfx::PointF> transformed_pt) {
  // If drag is not in progress it means drag has already finished and we get
  // this callback after that already. This happens for example when drag leaves
  // out window and we get the exit signal while still waiting for this
  // targeting callback to be called for the previous drag update signal. In
  // this case we just ignore this operation.
  if (!drag_in_progress_)
    return;
  if (!target)
    return;
  RenderWidgetHostImpl* target_rwh =
      RenderWidgetHostImpl::From(target->GetRenderWidgetHost());
  if (!IsValidDragTarget(target_rwh))
    return;

  aura::Window* root_window = GetNativeView()->GetRootWindow();
  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(root_window);
  gfx::PointF screen_pt = event.root_location_f();
  if (screen_position_client)
    screen_position_client->ConvertPointToScreen(root_window, &screen_pt);

  if (target_rwh != current_rwh_for_drag_.get()) {
    if (current_rwh_for_drag_) {
      gfx::PointF transformed_leave_point = event.location_f();
      static_cast<RenderWidgetHostViewBase*>(
          web_contents_->GetRenderWidgetHostView())
          ->TransformPointToCoordSpaceForView(
              event.location_f(),
              static_cast<RenderWidgetHostViewBase*>(
                  current_rwh_for_drag_->GetView()),
              &transformed_leave_point);
      current_rwh_for_drag_->DragTargetDragLeave(transformed_leave_point,
                                                 screen_pt);
    }
    DragEnteredCallback(event, std::move(drop_data), target, transformed_pt);
  }

  if (!current_drop_data_) {
    return;
  }

  DCHECK(transformed_pt.has_value());
  blink::WebDragOperationsMask op = ConvertToWeb(event.source_operations());
  target_rwh->DragTargetDragOver(
      transformed_pt.value(), screen_pt, op,
      ui::EventFlagsToWebEventModifiers(event.flags()));

  if (drag_dest_delegate_)
    drag_dest_delegate_->OnDragOver();
}

int WebContentsViewAura::OnDragUpdated(const ui::DropTargetEvent& event) {
  std::unique_ptr<DropData> drop_data = std::make_unique<DropData>();
  // Calling this here as event.data might become invalid inside the callback.
  PrepareDropData(drop_data.get(), event.data());

  web_contents_->GetInputEventRouter()
      ->GetRenderWidgetHostAtPointAsynchronously(
          web_contents_->GetRenderViewHost()->GetWidget()->GetView(),
          event.location_f(),
          base::BindOnce(&WebContentsViewAura::DragUpdatedCallback,
                         weak_ptr_factory_.GetWeakPtr(), event,
                         std::move(drop_data)));
  return ConvertFromWeb(current_drag_op_);
}

void WebContentsViewAura::OnDragExited() {
  drag_in_progress_ = false;

  if (current_rvh_for_drag_ !=
      GetRenderViewHostID(web_contents_->GetRenderViewHost()) ||
      !current_drop_data_) {
    return;
  }

  if (current_rwh_for_drag_) {
    current_rwh_for_drag_->DragTargetDragLeave(gfx::PointF(), gfx::PointF());
    current_rwh_for_drag_.reset();
  }

  if (drag_dest_delegate_)
    drag_dest_delegate_->OnDragLeave();

  current_drop_data_.reset();
}

void WebContentsViewAura::PerformDropCallback(
    ui::DropTargetEvent event,
    std::unique_ptr<ui::OSExchangeData> data,
    base::WeakPtr<RenderWidgetHostViewBase> target,
    base::Optional<gfx::PointF> transformed_pt) {
  drag_in_progress_ = false;
  base::ScopedClosureRunner end_drag_runner(std::move(end_drag_runner_));

  if (!target)
    return;
  RenderWidgetHostImpl* target_rwh =
      RenderWidgetHostImpl::From(target->GetRenderWidgetHost());
  if (!IsValidDragTarget(target_rwh))
    return;

  DCHECK(transformed_pt.has_value());

  gfx::PointF screen_pt(display::Screen::GetScreen()->GetCursorScreenPoint());
  if (target_rwh != current_rwh_for_drag_.get()) {
    if (current_rwh_for_drag_)
      current_rwh_for_drag_->DragTargetDragLeave(transformed_pt.value(),
                                                 screen_pt);

    std::unique_ptr<DropData> drop_data = std::make_unique<DropData>();
    PrepareDropData(drop_data.get(), *data.get());
    DragEnteredCallback(event, std::move(drop_data), target, transformed_pt);
  }

  if (!current_drop_data_)
    return;

  OnPerformDropContext context(target_rwh, event, std::move(data),
                               std::move(end_drag_runner), transformed_pt,
                               screen_pt);
  // |delegate_| may be null in unit tests.
  if (delegate_) {
    delegate_->OnPerformDrop(
        *current_drop_data_,
        base::BindOnce(&WebContentsViewAura::FinishOnPerformDropCallback,
                       weak_ptr_factory_.GetWeakPtr(), std::move(context)));
  } else {
    FinishOnPerformDropCallback(
        std::move(context),
        WebContentsViewDelegate::DropCompletionResult::kContinue);
  }
}

void WebContentsViewAura::FinishOnPerformDropCallback(
    OnPerformDropContext context,
    WebContentsViewDelegate::DropCompletionResult result) {
  const int key_modifiers =
      ui::EventFlagsToWebEventModifiers(context.event.flags());
  // This is possibly an async callback.  Make sure the RWH is still valid.
  if (!context.target_rwh || !IsValidDragTarget(context.target_rwh.get()))
    return;

  if (result != WebContentsViewDelegate::DropCompletionResult::kContinue) {
    if (!drop_callback_for_testing_.is_null()) {
      std::move(drop_callback_for_testing_)
          .Run(context.target_rwh.get(), *current_drop_data_,
               context.transformed_pt.value(), context.screen_pt, key_modifiers,
               /*drop_allowed=*/false);
    }
    return;
  }

#if defined(OS_WIN)
  if (ShouldIncludeVirtualFiles(*current_drop_data_) &&
      context.data->HasVirtualFilenames()) {
    // Asynchronously retrieve the actual content of any virtual files now (this
    // step is not needed for "real" files already on the file system, e.g.
    // those dropped on Chromium from the desktop). When all content has been
    // written to temporary files, the OnGotVirtualFilesAsTempFiles
    // callback will be invoked and the drop communicated to the renderer
    // process.
    auto callback =
        base::BindOnce(&WebContentsViewAura::OnGotVirtualFilesAsTempFiles,
                       weak_ptr_factory_.GetWeakPtr());

    // GetVirtualFilesAsTempFiles will immediately return false if there are no
    // virtual files to retrieve (all items are folders e.g.) and no callback
    // will be received.
    if (context.data->GetVirtualFilesAsTempFiles(std::move(callback))) {
      // Cache the parameters as they were at the time of the drop. This is
      // needed for checking that the drop target is still valid when the async
      // operation completes.
      async_drop_navigation_observer_ =
          std::make_unique<AsyncDropNavigationObserver>(
              web_contents_, std::move(current_drop_data_),
              std::move(context.end_drag_runner), context.target_rwh.get(),
              context.transformed_pt.value(), context.screen_pt, key_modifiers);
      return;
    }
  }

#endif
  CompleteDrop(context.target_rwh.get(), *current_drop_data_,
               context.transformed_pt.value(), context.screen_pt,
               key_modifiers);
  current_drop_data_.reset();
}

int WebContentsViewAura::OnPerformDrop(
    const ui::DropTargetEvent& event,
    std::unique_ptr<ui::OSExchangeData> data) {
  web_contents_->GetInputEventRouter()
      ->GetRenderWidgetHostAtPointAsynchronously(
          web_contents_->GetRenderViewHost()->GetWidget()->GetView(),
          event.location_f(),
          base::BindOnce(&WebContentsViewAura::PerformDropCallback,
                         weak_ptr_factory_.GetWeakPtr(), event,
                         std::move(data)));
  return ConvertFromWeb(current_drag_op_);
}

void WebContentsViewAura::CompleteDrop(RenderWidgetHostImpl* target_rwh,
                                       const DropData& drop_data,
                                       const gfx::PointF& client_pt,
                                       const gfx::PointF& screen_pt,
                                       int key_modifiers) {
  target_rwh->DragTargetDrop(drop_data, client_pt, screen_pt, key_modifiers);
  if (drag_dest_delegate_)
    drag_dest_delegate_->OnDrop();

  if (!drop_callback_for_testing_.is_null()) {
    std::move(drop_callback_for_testing_)
        .Run(target_rwh, drop_data, client_pt, screen_pt, key_modifiers,
             /*drop_allowed=*/true);
  }
}

void WebContentsViewAura::RegisterDropCallbackForTesting(
    DropCallbackForTesting callback) {
  drop_callback_for_testing_ = std::move(callback);
}

#if defined(OS_WIN)
void WebContentsViewAura::OnGotVirtualFilesAsTempFiles(
    const std::vector<std::pair<base::FilePath, base::FilePath>>&
        filepaths_and_names) {
  DCHECK(!filepaths_and_names.empty());

  if (!async_drop_navigation_observer_)
    return;

  std::unique_ptr<AsyncDropNavigationObserver> drop_observer(
      std::move(async_drop_navigation_observer_));

  RenderWidgetHostImpl* target_rwh = drop_observer->target_rwh();
  DropData* drop_data = drop_observer->drop_data();

  // Security check--don't allow the drop if a navigation occurred since the
  // drop was initiated or the render widget host has changed or it is not a
  // valid target.
  if (!drop_observer->drop_allowed() ||
      !(target_rwh && target_rwh == current_rwh_for_drag_.get() &&
        IsValidDragTarget(target_rwh))) {
    // Signal test code that the drop is disallowed
    if (!drop_callback_for_testing_.is_null()) {
      std::move(drop_callback_for_testing_)
          .Run(target_rwh, *drop_data, drop_observer->client_pt(),
               drop_observer->screen_pt(), drop_observer->key_modifiers(),
               drop_observer->drop_allowed());
    }

    return;
  }

  // The vector of filenames will still have items added during dragenter
  // (script is allowed to enumerate the files in the data store but not
  // retrieve the file contents in dragenter). But the temp file path in the
  // FileInfo structs will just be a placeholder. Clear out the vector before
  // replacing it with FileInfo structs that have the paths to the retrieved
  // file contents.
  drop_data->filenames.clear();

  // Ensure we have temp file deleter.
  if (!async_drop_temp_file_deleter_) {
    async_drop_temp_file_deleter_ =
        std::make_unique<AsyncDropTempFileDeleter>();
  }

  for (const auto& filepath_and_name : filepaths_and_names) {
    drop_data->filenames.push_back(
        ui::FileInfo(filepath_and_name.first, filepath_and_name.second));

    // Make sure the temp file eventually gets cleaned up.
    async_drop_temp_file_deleter_->RegisterFile(filepath_and_name.first);
  }

  CompleteDrop(target_rwh, *drop_data, drop_observer->client_pt(),
               drop_observer->screen_pt(), drop_observer->key_modifiers());
}
#endif

int WebContentsViewAura::GetTopControlsHeight() const {
  WebContentsDelegate* delegate = web_contents_->GetDelegate();
  if (!delegate)
    return 0;
  return delegate->GetTopControlsHeight();
}

int WebContentsViewAura::GetBottomControlsHeight() const {
  WebContentsDelegate* delegate = web_contents_->GetDelegate();
  if (!delegate)
    return 0;
  return delegate->GetBottomControlsHeight();
}

bool WebContentsViewAura::DoBrowserControlsShrinkRendererSize() const {
  WebContentsDelegate* delegate = web_contents_->GetDelegate();
  if (!delegate)
    return false;
  return delegate->DoBrowserControlsShrinkRendererSize(web_contents_);
}

#if BUILDFLAG(USE_EXTERNAL_POPUP_MENU)
void WebContentsViewAura::ShowPopupMenu(RenderFrameHost* render_frame_host,
                                        const gfx::Rect& bounds,
                                        int item_height,
                                        double item_font_size,
                                        int selected_item,
                                        const std::vector<MenuItem>& items,
                                        bool right_aligned,
                                        bool allow_multiple_selection) {
  NOTIMPLEMENTED() << " show " << items.size() << " menu items";
}

void WebContentsViewAura::HidePopupMenu() {
  NOTIMPLEMENTED();
}
#endif

}  // namespace content
