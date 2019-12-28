// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/public/cpp/mutable_network_traffic_annotation_tag_mojom_traits.h"

#include "base/macros.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/network/public/mojom/mutable_network_traffic_annotation_tag.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace network {

TEST(MutableNetworkTrafficAnnottionTagsTest, BasicTest) {
  net::MutableNetworkTrafficAnnotationTag original;
  net::MutableNetworkTrafficAnnotationTag copy;

  original.unique_id_hash_code = 1;
  EXPECT_TRUE(mojom::MutableNetworkTrafficAnnotationTag::Deserialize(
      mojom::MutableNetworkTrafficAnnotationTag::Serialize(&original), &copy));
  EXPECT_EQ(copy.unique_id_hash_code, 1);

  original.unique_id_hash_code = 2;
  EXPECT_TRUE(mojom::MutableNetworkTrafficAnnotationTag::Deserialize(
      mojom::MutableNetworkTrafficAnnotationTag::Serialize(&original), &copy));
  EXPECT_EQ(copy.unique_id_hash_code, 2);
}

}  // namespace network
