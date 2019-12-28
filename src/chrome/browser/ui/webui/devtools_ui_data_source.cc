// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/devtools_ui_data_source.h"

#include <list>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/task/post_task.h"
#include "chrome/browser/devtools/devtools_ui_bindings.h"
#include "chrome/browser/devtools/url_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/devtools_frontend_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/url_constants.h"

namespace {

std::string PathWithoutParams(const std::string& path) {
  return GURL(base::StrCat({content::kChromeDevToolsScheme,
                            url::kStandardSchemeSeparator,
                            chrome::kChromeUIDevToolsHost}))
      .Resolve(path)
      .path()
      .substr(1);
}

scoped_refptr<base::RefCountedMemory> CreateNotFoundResponse() {
  const char kHttpNotFound[] = "HTTP/1.1 404 Not Found\n\n";
  return base::MakeRefCounted<base::RefCountedStaticMemory>(
      kHttpNotFound, strlen(kHttpNotFound));
}

// DevToolsDataSource ---------------------------------------------------------

std::string GetMimeTypeForPath(const std::string& path) {
  std::string filename = PathWithoutParams(path);
  if (base::EndsWith(filename, ".html", base::CompareCase::INSENSITIVE_ASCII)) {
    return "text/html";
  } else if (base::EndsWith(filename, ".css",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "text/css";
  } else if (base::EndsWith(filename, ".js",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "application/javascript";
  } else if (base::EndsWith(filename, ".png",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/png";
  } else if (base::EndsWith(filename, ".gif",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/gif";
  } else if (base::EndsWith(filename, ".svg",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/svg+xml";
  } else if (base::EndsWith(filename, ".manifest",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "text/cache-manifest";
  }
  return "text/html";
}
}  // namespace

DevToolsDataSource::DevToolsDataSource(
    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory)
    : url_loader_factory_(std::move(url_loader_factory)) {}

DevToolsDataSource::~DevToolsDataSource() {}

std::string DevToolsDataSource::GetSource() {
  return chrome::kChromeUIDevToolsHost;
}

// static
GURL GetCustomDevToolsFrontendURL() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kCustomDevtoolsFrontend)) {
    return GURL(base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
        switches::kCustomDevtoolsFrontend));
  }
  return GURL();
}

void DevToolsDataSource::StartDataRequest(
    const std::string& path,
    const content::WebContents::Getter& wc_getter,
    const GotDataCallback& callback) {
  // Serve request to devtools://bundled/ from local bundle.
  std::string bundled_path_prefix(chrome::kChromeUIDevToolsBundledPath);
  bundled_path_prefix += "/";
  if (base::StartsWith(path, bundled_path_prefix,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    std::string path_without_params = PathWithoutParams(path);

    DCHECK(base::StartsWith(path_without_params, bundled_path_prefix,
                            base::CompareCase::INSENSITIVE_ASCII));
    std::string path_under_bundled =
        path_without_params.substr(bundled_path_prefix.length());
#if !BUILDFLAG(DEBUG_DEVTOOLS)
    if (!GetCustomDevToolsFrontendURL().SchemeIsFile()) {
      // Fetch from packaged resources.
      StartBundledDataRequest(path_under_bundled, callback);
      return;
    }
#endif
    // Fetch from file system.
    StartFileRequest(path_under_bundled, callback);
    return;
  }

  // Serve request to devtools://blank as empty page.
  std::string empty_path_prefix(chrome::kChromeUIDevToolsBlankPath);
  if (base::StartsWith(path, empty_path_prefix,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    callback.Run(new base::RefCountedStaticMemory());
    return;
  }

  // Serve request to devtools://remote from remote location.
  std::string remote_path_prefix(chrome::kChromeUIDevToolsRemotePath);
  remote_path_prefix += "/";
  if (base::StartsWith(path, remote_path_prefix,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    GURL url(kRemoteFrontendBase + path.substr(remote_path_prefix.length()));

    CHECK_EQ(url.host(), kRemoteFrontendDomain);
    if (url.is_valid() && DevToolsUIBindings::IsValidRemoteFrontendURL(url)) {
      StartRemoteDataRequest(url, callback);
    } else {
      DLOG(ERROR) << "Refusing to load invalid remote front-end URL";
      callback.Run(CreateNotFoundResponse());
    }
    return;
  }

  // Serve request to devtools://custom from custom URL.
  std::string custom_path_prefix(chrome::kChromeUIDevToolsCustomPath);
  custom_path_prefix += "/";
  if (base::StartsWith(path, custom_path_prefix,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    GURL custom_devtools_frontend = GetCustomDevToolsFrontendURL();
    if (!custom_devtools_frontend.is_empty()) {
      GURL url = GURL(custom_devtools_frontend.spec() +
                      path.substr(custom_path_prefix.length()));
      DCHECK(url.is_valid());
      StartCustomDataRequest(url, callback);
      return;
    }
  }

  callback.Run(CreateNotFoundResponse());
}

std::string DevToolsDataSource::GetMimeType(const std::string& path) {
  return GetMimeTypeForPath(path);
}

bool DevToolsDataSource::ShouldAddContentSecurityPolicy() {
  return false;
}

bool DevToolsDataSource::ShouldDenyXFrameOptions() {
  return false;
}

bool DevToolsDataSource::ShouldServeMimeTypeAsContentTypeHeader() {
  return true;
}

void DevToolsDataSource::StartBundledDataRequest(
    const std::string& path,
    const content::URLDataSource::GotDataCallback& callback) {
  base::StringPiece resource =
      content::DevToolsFrontendHost::GetFrontendResource(path);

  DLOG_IF(WARNING, resource.empty())
      << "Unable to find dev tool resource: " << path
      << ". If you compiled with debug_devtools=1, try running with "
         "--debug-devtools.";
  scoped_refptr<base::RefCountedStaticMemory> bytes(
      new base::RefCountedStaticMemory(resource.data(), resource.length()));
  callback.Run(bytes.get());
}

void DevToolsDataSource::StartRemoteDataRequest(
    const GURL& url,
    const content::URLDataSource::GotDataCallback& callback) {
  CHECK(url.is_valid());
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("devtools_hard_coded_data_source",
                                          R"(
        semantics {
          sender: "Developer Tools Remote Data Request From Google"
          description:
            "This service fetches Chromium DevTools front-end files from the "
            "cloud for the remote debugging scenario."
          trigger:
            "When user attaches to mobile phone for debugging."
          data: "None"
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting: "This feature cannot be disabled by settings."
          chrome_policy {
            DeveloperToolsAvailability {
              policy_options {mode: MANDATORY}
              DeveloperToolsAvailability: 2
            }
          }
        })");

  StartNetworkRequest(url, traffic_annotation, net::LOAD_NORMAL, callback);
}

void DevToolsDataSource::StartCustomDataRequest(
    const GURL& url,
    const content::URLDataSource::GotDataCallback& callback) {
  if (!url.is_valid()) {
    callback.Run(CreateNotFoundResponse());
    return;
  }
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("devtools_free_data_source", R"(
        semantics {
          sender: "Developer Tools Remote Data Request"
          description:
            "This service fetches Chromium DevTools front-end files from the "
            "cloud for the remote debugging scenario. This can only happen if "
            "a URL was passed on the commandline via flag "
            "'--custom-devtools-frontend'. This URL overrides the default "
            "fetching from a Google website, see "
            "devtools_hard_coded_data_source."
          trigger:
            "When command line flag --custom-devtools-frontend is specified "
            "and DevTools is opened."
          data: "None"
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting: "This feature cannot be disabled by settings."
          chrome_policy {
            DeveloperToolsAvailability {
              policy_options {mode: MANDATORY}
              DeveloperToolsAvailability: 2
            }
          }
        })");

  StartNetworkRequest(url, traffic_annotation, net::LOAD_DISABLE_CACHE,
                      callback);
}

void DevToolsDataSource::StartNetworkRequest(
    const GURL& url,
    const net::NetworkTrafficAnnotationTag& traffic_annotation,
    int load_flags,
    const GotDataCallback& callback) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = url;
  request->load_flags = load_flags;

  auto request_iter = pending_requests_.emplace(pending_requests_.begin());
  request_iter->callback = callback;
  request_iter->loader =
      network::SimpleURLLoader::Create(std::move(request), traffic_annotation);
  request_iter->loader->DownloadToStringOfUnboundedSizeUntilCrashAndDie(
      url_loader_factory_.get(),
      base::BindOnce(&DevToolsDataSource::OnLoadComplete,
                     base::Unretained(this), request_iter));
}

scoped_refptr<base::RefCountedMemory> ReadFileForDevTools(
    const base::FilePath& path) {
  std::string buffer;
  if (!base::ReadFileToString(path, &buffer)) {
    LOG(ERROR) << "Failed to read " << path;
    return CreateNotFoundResponse();
  }
  return base::RefCountedString::TakeString(&buffer);
}

void DevToolsDataSource::StartFileRequest(const std::string& path,
                                          const GotDataCallback& callback) {
  base::FilePath base_path;
  GURL custom_devtools_frontend = GetCustomDevToolsFrontendURL();
  if (custom_devtools_frontend.SchemeIsFile()) {
    base_path = base_path.AppendASCII(custom_devtools_frontend.GetContent());
  } else {
#if BUILDFLAG(DEBUG_DEVTOOLS)
    // Use default path for unbundled files when debug_devtools=true
    if (!base::PathService::Get(chrome::DIR_INSPECTOR_DEBUG, &base_path)) {
      callback.Run(CreateNotFoundResponse());
      return;
    }
#else
    NOTREACHED();
#endif
  }

  base::FilePath full_path = base_path.AppendASCII(path);
  CHECK(base_path.IsParent(full_path));

  base::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::ThreadPool(),
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN,
       base::TaskPriority::USER_VISIBLE},
      // The usage of BindRepeating below is only because the type of
      // task callback needs to match that of response callback, which
      // is currently a repeating callback.
      base::BindRepeating(ReadFileForDevTools, std::move(full_path)), callback);
}

void DevToolsDataSource::OnLoadComplete(
    std::list<PendingRequest>::iterator request_iter,
    std::unique_ptr<std::string> response_body) {
  std::move(request_iter->callback)
      .Run(response_body
               ? base::RefCountedString::TakeString(response_body.get())
               : CreateNotFoundResponse());
  pending_requests_.erase(request_iter);
}

DevToolsDataSource::PendingRequest::PendingRequest() = default;

DevToolsDataSource::PendingRequest::PendingRequest(PendingRequest&& other) =
    default;

DevToolsDataSource::PendingRequest::~PendingRequest() {
  if (callback)
    callback.Run(CreateNotFoundResponse());
}
