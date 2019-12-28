// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_APP_LIST_MODEL_FOLDER_IMAGE_H_
#define ASH_APP_LIST_MODEL_FOLDER_IMAGE_H_

#include <stddef.h>

#include <vector>

#include "ash/app_list/model/app_list_item_list_observer.h"
#include "ash/app_list/model/app_list_item_observer.h"
#include "ash/app_list/model/app_list_model_export.h"
#include "ash/public/cpp/app_list/app_list_types.h"
#include "base/observer_list.h"
#include "ui/gfx/image/image_skia.h"

namespace gfx {
class Rect;
}

namespace ash {

class AppListConfig;
class AppListItem;
class AppListItemList;

class APP_LIST_MODEL_EXPORT FolderImageObserver {
 public:
  // Called when the folder icon has changed.
  virtual void OnFolderImageUpdated(ash::AppListConfigType config_type) {}

 protected:
  virtual ~FolderImageObserver() {}
};

// The icon for an app list folder, dynamically generated by drawing the
// folder's items inside a circle. Automatically keeps itself up to date, and
// notifies observers when it changes.
class APP_LIST_MODEL_EXPORT FolderImage : public AppListItemListObserver,
                                          public AppListItemObserver {
 public:
  // Number of the top items in a folder, which are shown inside the folder icon
  // and animated when opening and closing a folder.
  static const size_t kNumFolderTopItems;

  FolderImage(const AppListConfig* app_list_config, AppListItemList* item_list);
  ~FolderImage() override;

  // Generates the folder's icon from the icons of the items in the item list,
  // and notifies observers that the icon has changed.
  void UpdateIcon();

  // Given an AppListItem currently being dragged, updates |dragged_item_| then
  // executes an ordinary run of UpdateIcon()
  void UpdateDraggedItem(const AppListItem* dragged_item);

  const gfx::ImageSkia& icon() const { return icon_; }

  // Calculates the top item icons' bounds inside |folder_icon_bounds|.
  // Returns the bounds of top item icons based on total number of items.
  // |app_list_config| is the app list configuration for which bounds are being
  // calculated.
  static std::vector<gfx::Rect> GetTopIconsBounds(
      const AppListConfig& app_list_config,
      const gfx::Rect& folder_icon_bounds,
      size_t num_items);

  // Returns the target icon bounds for |item| to fly back to its parent folder
  // icon in animation UI. If |item| is one of the top item icon, this will
  // match its corresponding top item icon in the folder icon. Otherwise,
  // the target icon bounds is centered at the |folder_icon_bounds| with
  // the same size of the top item icon.
  // The Rect returned is in the same coordinates of |folder_icon_bounds|.
  // |app_list_config| is the app list configuration for which bounds are being
  // calculated.
  gfx::Rect GetTargetIconRectInFolderForItem(
      const AppListConfig& app_list_config,
      AppListItem* item,
      const gfx::Rect& folder_icon_bounds) const;

  void AddObserver(FolderImageObserver* observer);
  void RemoveObserver(FolderImageObserver* observer);

  // AppListItemObserver overrides:
  void ItemIconChanged(ash::AppListConfigType config_type) override;

  // AppListItemListObserver overrides:
  void OnListItemAdded(size_t index, AppListItem* item) override;
  void OnListItemRemoved(size_t index, AppListItem* item) override;
  void OnListItemMoved(size_t from_index,
                       size_t to_index,
                       AppListItem* item) override;

 private:
  // Updates the folder's icon from the icons of |top_items_| and calls
  // OnFolderImageUpdated. Does not refresh the |top_items_| list, so should
  // only be called if the |item_list_| has not been changed (see UpdateIcon).
  void RedrawIconAndNotify();

  // The app list config for which this folder image is created.
  const AppListConfig* app_list_config_;

  // The unclipped icon image. This will be clipped in AppListItemView before
  // being shown in apps grid.
  gfx::ImageSkia icon_;

  // List of top-level app list items (to display small in the icon).
  AppListItemList* item_list_;

  // Item being dragged, if any.
  const AppListItem* dragged_item_ = nullptr;

  // Top items for generating folder icon.
  std::vector<AppListItem*> top_items_;

  base::ObserverList<FolderImageObserver>::Unchecked observers_;
};

}  // namespace ash

#endif  // ASH_APP_LIST_MODEL_FOLDER_IMAGE_H_
