// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/json/json_file_value_serializer.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/test/test_browser_thread.h"
#include "extensions/browser/info_map.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

using content::BrowserThread;

namespace keys = extensions::manifest_keys;

namespace extensions {

class InfoMapTest : public testing::Test {
 public:
  InfoMapTest()
      : ui_thread_(BrowserThread::UI, &message_loop_),
        io_thread_(BrowserThread::IO, &message_loop_) {}

 private:
  base::MessageLoop message_loop_;
  content::TestBrowserThread ui_thread_;
  content::TestBrowserThread io_thread_;
};

// Returns a barebones test Extension object with the given name.
static scoped_refptr<Extension> CreateExtension(const std::string& name) {
#if defined(OS_WIN)
  base::FilePath path(FILE_PATH_LITERAL("c:\\foo"));
#elif defined(OS_POSIX)
  base::FilePath path(FILE_PATH_LITERAL("/foo"));
#endif

  base::DictionaryValue manifest;
  manifest.SetString(keys::kVersion, "1.0.0.0");
  manifest.SetString(keys::kName, name);

  std::string error;
  scoped_refptr<Extension> extension =
      Extension::Create(path.AppendASCII(name),
                        Manifest::INVALID_LOCATION,
                        manifest,
                        Extension::NO_FLAGS,
                        &error);
  EXPECT_TRUE(extension.get()) << error;

  return extension;
}

static scoped_refptr<Extension> LoadManifest(const std::string& dir,
                                             const std::string& test_file) {
  base::FilePath path;
  PathService::Get(chrome::DIR_TEST_DATA, &path);
  path = path.AppendASCII("extensions").AppendASCII(dir).AppendASCII(test_file);

  JSONFileValueSerializer serializer(path);
  scoped_ptr<base::Value> result(serializer.Deserialize(NULL, NULL));
  if (!result)
    return NULL;

  std::string error;
  scoped_refptr<Extension> extension =
      Extension::Create(path,
                        Manifest::INVALID_LOCATION,
                        *static_cast<base::DictionaryValue*>(result.get()),
                        Extension::NO_FLAGS,
                        &error);
  EXPECT_TRUE(extension.get()) << error;

  return extension;
}

// Test that the InfoMap handles refcounting properly.
TEST_F(InfoMapTest, RefCounting) {
  scoped_refptr<InfoMap> info_map(new InfoMap());

  // New extensions should have a single reference holding onto them.
  scoped_refptr<Extension> extension1(CreateExtension("extension1"));
  scoped_refptr<Extension> extension2(CreateExtension("extension2"));
  scoped_refptr<Extension> extension3(CreateExtension("extension3"));
  EXPECT_TRUE(extension1->HasOneRef());
  EXPECT_TRUE(extension2->HasOneRef());
  EXPECT_TRUE(extension3->HasOneRef());

  // Add a ref to each extension and give it to the info map.
  info_map->AddExtension(extension1.get(), base::Time(), false, false);
  info_map->AddExtension(extension2.get(), base::Time(), false, false);
  info_map->AddExtension(extension3.get(), base::Time(), false, false);

  // Release extension1, and the info map should have the only ref.
  const Extension* weak_extension1 = extension1.get();
  extension1 = NULL;
  EXPECT_TRUE(weak_extension1->HasOneRef());

  // Remove extension2, and the extension2 object should have the only ref.
  info_map->RemoveExtension(
      extension2->id(), extensions::UnloadedExtensionInfo::REASON_UNINSTALL);
  EXPECT_TRUE(extension2->HasOneRef());

  // Delete the info map, and the extension3 object should have the only ref.
  info_map = NULL;
  EXPECT_TRUE(extension3->HasOneRef());
}

// Tests that we can query a few extension properties from the InfoMap.
TEST_F(InfoMapTest, Properties) {
  scoped_refptr<InfoMap> info_map(new InfoMap());

  scoped_refptr<Extension> extension1(CreateExtension("extension1"));
  scoped_refptr<Extension> extension2(CreateExtension("extension2"));

  info_map->AddExtension(extension1.get(), base::Time(), false, false);
  info_map->AddExtension(extension2.get(), base::Time(), false, false);

  EXPECT_EQ(2u, info_map->extensions().size());
  EXPECT_EQ(extension1.get(), info_map->extensions().GetByID(extension1->id()));
  EXPECT_EQ(extension2.get(), info_map->extensions().GetByID(extension2->id()));
}

// Tests CheckURLAccessToExtensionPermission given both extension and app URLs.
TEST_F(InfoMapTest, CheckPermissions) {
  scoped_refptr<InfoMap> info_map(new InfoMap());

  scoped_refptr<Extension> app(
      LoadManifest("manifest_tests", "valid_app.json"));
  scoped_refptr<Extension> extension(
      LoadManifest("manifest_tests", "tabs_extension.json"));

  GURL app_url("http://www.google.com/mail/foo.html");
  ASSERT_TRUE(app->is_app());
  ASSERT_TRUE(app->web_extent().MatchesURL(app_url));

  info_map->AddExtension(app.get(), base::Time(), false, false);
  info_map->AddExtension(extension.get(), base::Time(), false, false);

  // The app should have the notifications permission, either from a
  // chrome-extension URL or from its web extent.
  const Extension* match = info_map->extensions().GetExtensionOrAppByURL(
      app->GetResourceURL("a.html"));
  EXPECT_TRUE(match && match->HasAPIPermission(APIPermission::kNotification));
  match = info_map->extensions().GetExtensionOrAppByURL(app_url);
  EXPECT_TRUE(match && match->HasAPIPermission(APIPermission::kNotification));
  EXPECT_FALSE(match && match->HasAPIPermission(APIPermission::kTab));

  // The extension should have the tabs permission.
  match = info_map->extensions().GetExtensionOrAppByURL(
      extension->GetResourceURL("a.html"));
  EXPECT_TRUE(match && match->HasAPIPermission(APIPermission::kTab));
  EXPECT_FALSE(match && match->HasAPIPermission(APIPermission::kNotification));

  // Random URL should not have any permissions.
  GURL evil_url("http://evil.com/a.html");
  match = info_map->extensions().GetExtensionOrAppByURL(evil_url);
  EXPECT_FALSE(match);
}

TEST_F(InfoMapTest, TestNotificationsDisabled) {
  scoped_refptr<InfoMap> info_map(new InfoMap());
  scoped_refptr<Extension> app(LoadManifest("manifest_tests",
                                            "valid_app.json"));
  info_map->AddExtension(app.get(), base::Time(), false, false);

  EXPECT_FALSE(info_map->AreNotificationsDisabled(app->id()));
  info_map->SetNotificationsDisabled(app->id(), true);
  EXPECT_TRUE(info_map->AreNotificationsDisabled(app->id()));
  info_map->SetNotificationsDisabled(app->id(), false);
}

}  // namespace extensions
