// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRINTING_PRINT_VIEW_MANAGER_BASIC_H_
#define CHROME_BROWSER_PRINTING_PRINT_VIEW_MANAGER_BASIC_H_

#include "base/macros.h"
#include "build/build_config.h"
#include "chrome/browser/printing/print_view_manager_base.h"
#include "content/public/browser/web_contents_user_data.h"

namespace printing {

// Manages the print commands for a WebContents - basic version.
class PrintViewManagerBasic
    : public PrintViewManagerBase,
      public content::WebContentsUserData<PrintViewManagerBasic> {
 public:
  ~PrintViewManagerBasic() override;

#if defined(OS_ANDROID)
  // printing::PrintManager:
  void PdfWritingDone(int page_count) override;
#endif

 private:
  explicit PrintViewManagerBasic(content::WebContents* web_contents);
  friend class content::WebContentsUserData<PrintViewManagerBasic>;

  WEB_CONTENTS_USER_DATA_KEY_DECL();

  DISALLOW_COPY_AND_ASSIGN(PrintViewManagerBasic);
};

}  // namespace printing

#endif  // CHROME_BROWSER_PRINTING_PRINT_VIEW_MANAGER_BASIC_H_
