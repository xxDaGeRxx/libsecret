/* GSecret - GLib wrapper for Secret Service
 *
 * Copyright 2011 Collabora Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 */


#include "config.h"

#include "gsecret-collection.h"
#include "gsecret-item.h"
#include "gsecret-service.h"
#include "gsecret-private.h"

#include "mock-service.h"

#include "egg/egg-testing.h"

#include <glib.h>

#include <errno.h>
#include <stdlib.h>

static const GSecretSchema DELETE_SCHEMA = {
	"org.mock.schema.Delete",
	{
		{ "number", GSECRET_ATTRIBUTE_INTEGER },
		{ "string", GSECRET_ATTRIBUTE_STRING },
		{ "even", GSECRET_ATTRIBUTE_BOOLEAN },
	}
};

static const GSecretSchema STORE_SCHEMA = {
	"org.mock.schema.Store",
	{
		{ "number", GSECRET_ATTRIBUTE_INTEGER },
		{ "string", GSECRET_ATTRIBUTE_STRING },
		{ "even", GSECRET_ATTRIBUTE_BOOLEAN },
	}
};

typedef struct {
	GSecretService *service;
} Test;

static void
setup_mock (Test *test,
            gconstpointer data)
{
	GError *error = NULL;
	const gchar *mock_script = data;

	mock_service_start (mock_script, &error);
	g_assert_no_error (error);
}

static void
setup (Test *test,
       gconstpointer data)
{
	GError *error = NULL;

	setup_mock (test, data);

	test->service = gsecret_service_get_sync (GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);
}

static void
teardown_mock (Test *test,
               gconstpointer unused)
{
	mock_service_stop ();
}

static void
teardown (Test *test,
          gconstpointer unused)
{
	egg_test_wait_idle ();

	g_object_unref (test->service);
	egg_assert_not_object (test->service);

	teardown_mock (test, unused);
}

static void
on_complete_get_result (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	GAsyncResult **ret = user_data;
	g_assert (ret != NULL);
	g_assert (*ret == NULL);
	*ret = g_object_ref (result);
	egg_test_wait_stop ();
}

static void
test_get_sync (void)
{
	GSecretService *service1;
	GSecretService *service2;
	GSecretService *service3;
	GError *error = NULL;

	/* Both these sohuld point to the same thing */

	service1 = gsecret_service_get_sync (GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);

	service2 = gsecret_service_get_sync (GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);

	g_assert (GSECRET_IS_SERVICE (service1));
	g_assert (service1 == service2);

	g_object_unref (service1);
	g_assert (G_IS_OBJECT (service1));

	g_object_unref (service2);
	egg_assert_not_object (service2);

	/* Services were unreffed, so this should create a new one */
	service3 = gsecret_service_get_sync (GSECRET_SERVICE_NONE, NULL, &error);
	g_assert (GSECRET_IS_SERVICE (service3));
	g_assert_no_error (error);

	g_object_unref (service3);
	egg_assert_not_object (service3);
}

static void
test_get_async (void)
{
	GSecretService *service1;
	GSecretService *service2;
	GSecretService *service3;
	GAsyncResult *result = NULL;
	GError *error = NULL;

	/* Both these sohuld point to the same thing */

	gsecret_service_get (GSECRET_SERVICE_NONE, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);
	egg_test_wait ();
	service1 = gsecret_service_get_finish (result, &error);
	g_assert_no_error (error);
	g_clear_object (&result);

	gsecret_service_get (GSECRET_SERVICE_NONE, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);
	egg_test_wait ();
	service2 = gsecret_service_get_finish (result, &error);
	g_assert_no_error (error);
	g_clear_object (&result);

	g_assert (GSECRET_IS_SERVICE (service1));
	g_assert (service1 == service2);

	g_object_unref (service1);
	g_assert (G_IS_OBJECT (service1));

	g_object_unref (service2);
	egg_assert_not_object (service2);

	/* Services were unreffed, so this should create a new one */
	gsecret_service_get (GSECRET_SERVICE_NONE, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);
	egg_test_wait ();
	service3 = gsecret_service_get_finish (result, &error);
	g_assert_no_error (error);
	g_clear_object (&result);

	g_object_unref (service3);
	egg_assert_not_object (service3);
}

static void
test_get_more_sync (Test *test,
                    gconstpointer data)
{
	GSecretService *service;
	GSecretService *service2;
	GError *error = NULL;
	const gchar *path;
	GList *collections;

	service = gsecret_service_get_sync (GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_NONE);

	service2 = gsecret_service_get_sync (GSECRET_SERVICE_LOAD_COLLECTIONS, NULL, &error);
	g_assert_no_error (error);

	g_assert (GSECRET_IS_SERVICE (service));
	g_assert (service == service2);

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_LOAD_COLLECTIONS);
	collections = gsecret_service_get_collections (service);
	g_assert (collections != NULL);
	g_list_free_full (collections, g_object_unref);

	g_object_unref (service2);

	service2 = gsecret_service_get_sync (GSECRET_SERVICE_OPEN_SESSION, NULL, &error);
	g_assert_no_error (error);

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_OPEN_SESSION | GSECRET_SERVICE_LOAD_COLLECTIONS);
	path = gsecret_service_get_session_path (service);
	g_assert (path != NULL);

	g_object_unref (service2);

	g_object_unref (service);
	egg_assert_not_object (service);
}

static void
test_get_more_async (Test *test,
                     gconstpointer data)
{
	GAsyncResult *result = NULL;
	GSecretService *service;
	GError *error = NULL;
	const gchar *path;
	GList *collections;

	gsecret_service_get (GSECRET_SERVICE_LOAD_COLLECTIONS | GSECRET_SERVICE_OPEN_SESSION, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	service = gsecret_service_get_finish (result, &error);
	g_assert_no_error (error);
	g_object_unref (result);
	result = NULL;

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_OPEN_SESSION | GSECRET_SERVICE_LOAD_COLLECTIONS);
	path = gsecret_service_get_session_path (service);
	g_assert (path != NULL);

	collections = gsecret_service_get_collections (service);
	g_assert (collections != NULL);
	g_list_free_full (collections, g_object_unref);

	g_object_unref (service);
	egg_assert_not_object (service);

	/* Now get a session with just collections */

	gsecret_service_get (GSECRET_SERVICE_LOAD_COLLECTIONS, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	service = gsecret_service_get_finish (result, &error);
	g_assert_no_error (error);
	g_object_unref (result);

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_LOAD_COLLECTIONS);
	path = gsecret_service_get_session_path (service);
	g_assert (path == NULL);

	collections = gsecret_service_get_collections (service);
	g_assert (collections != NULL);
	g_list_free_full (collections, g_object_unref);

	g_object_unref (service);
	egg_assert_not_object (service);
}

static void
test_new_sync (void)
{
	GSecretService *service1;
	GSecretService *service2;
	GError *error = NULL;

	/* Both these sohuld point to different things */

	service1 = gsecret_service_new_sync (NULL, GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);

	service2 = gsecret_service_new_sync (NULL, GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);

	g_assert (GSECRET_IS_SERVICE (service1));
	g_assert (GSECRET_IS_SERVICE (service2));
	g_assert (service1 != service2);

	g_object_unref (service1);
	egg_assert_not_object (service1);

	g_object_unref (service2);
	egg_assert_not_object (service2);
}

static void
test_new_async (void)
{
	GSecretService *service1;
	GSecretService *service2;
	GAsyncResult *result = NULL;
	GError *error = NULL;

	/* Both these sohuld point to different things */

	gsecret_service_new (NULL, GSECRET_SERVICE_NONE, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);
	egg_test_wait ();
	service1 = gsecret_service_new_finish (result, &error);
	g_assert_no_error (error);
	g_clear_object (&result);

	gsecret_service_new (NULL, GSECRET_SERVICE_NONE, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);
	egg_test_wait ();
	service2 = gsecret_service_new_finish (result, &error);
	g_assert_no_error (error);
	g_clear_object (&result);

	g_assert (GSECRET_IS_SERVICE (service1));
	g_assert (GSECRET_IS_SERVICE (service2));
	g_assert (service1 != service2);

	g_object_unref (service1);
	egg_assert_not_object (service1);

	g_object_unref (service2);
	egg_assert_not_object (service2);
}

static void
test_new_more_sync (Test *test,
                    gconstpointer data)
{
	GSecretService *service;
	GError *error = NULL;
	const gchar *path;
	GList *collections;

	service = gsecret_service_new_sync (NULL, GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert (GSECRET_IS_SERVICE (service));

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_NONE);
	g_assert (gsecret_service_get_collections (service) == NULL);
	g_assert (gsecret_service_get_session_path (service) == NULL);

	g_object_unref (service);
	egg_assert_not_object (service);

	service = gsecret_service_new_sync (NULL, GSECRET_SERVICE_LOAD_COLLECTIONS, NULL, &error);
	g_assert_no_error (error);
	g_assert (GSECRET_IS_SERVICE (service));

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_LOAD_COLLECTIONS);
	collections = gsecret_service_get_collections (service);
	g_assert (collections != NULL);
	g_list_free_full (collections, g_object_unref);
	g_assert (gsecret_service_get_session_path (service) == NULL);

	g_object_unref (service);
	egg_assert_not_object (service);

	service = gsecret_service_new_sync (NULL, GSECRET_SERVICE_OPEN_SESSION, NULL, &error);
	g_assert_no_error (error);
	g_assert (GSECRET_IS_SERVICE (service));

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_OPEN_SESSION);
	g_assert (gsecret_service_get_collections (service) == NULL);
	path = gsecret_service_get_session_path (service);
	g_assert (path != NULL);

	g_object_unref (service);
	egg_assert_not_object (service);
}

static void
test_new_more_async (Test *test,
                     gconstpointer data)
{
	GAsyncResult *result = NULL;
	GSecretService *service;
	GError *error = NULL;
	const gchar *path;
	GList *collections;

	gsecret_service_new (NULL, GSECRET_SERVICE_LOAD_COLLECTIONS | GSECRET_SERVICE_OPEN_SESSION, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	service = gsecret_service_new_finish (result, &error);
	g_assert_no_error (error);
	g_object_unref (result);
	result = NULL;

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_OPEN_SESSION | GSECRET_SERVICE_LOAD_COLLECTIONS);
	path = gsecret_service_get_session_path (service);
	g_assert (path != NULL);

	collections = gsecret_service_get_collections (service);
	g_assert (collections != NULL);
	g_list_free_full (collections, g_object_unref);

	g_object_unref (service);
	egg_assert_not_object (service);

	/* Now get a session with just collections */

	gsecret_service_new (NULL, GSECRET_SERVICE_LOAD_COLLECTIONS, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	service = gsecret_service_new_finish (result, &error);
	g_assert_no_error (error);
	g_object_unref (result);

	g_assert_cmpuint (gsecret_service_get_flags (service), ==, GSECRET_SERVICE_LOAD_COLLECTIONS);
	path = gsecret_service_get_session_path (service);
	g_assert (path == NULL);

	collections = gsecret_service_get_collections (service);
	g_assert (collections != NULL);
	g_list_free_full (collections, g_object_unref);

	g_object_unref (service);
	egg_assert_not_object (service);
}

static void
test_connect_async (Test *test,
                    gconstpointer used)
{
	GError *error = NULL;
	GAsyncResult *result = NULL;
	GSecretService *service;
	const gchar *path;

	/* Passing false, not session */
	gsecret_service_get (GSECRET_SERVICE_NONE, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	service = gsecret_service_get_finish (result, &error);
	g_assert (GSECRET_IS_SERVICE (service));
	g_assert_no_error (error);
	g_object_unref (result);

	path = gsecret_service_get_session_path (service);
	g_assert (path == NULL);

	g_object_unref (service);
	egg_assert_not_object (service);
}

static void
test_connect_ensure_async (Test *test,
                           gconstpointer used)
{
	GError *error = NULL;
	GAsyncResult *result = NULL;
	GSecretService *service;
	const gchar *path;

	/* Passing true, ensures session is established */
	gsecret_service_get (GSECRET_SERVICE_OPEN_SESSION, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	service = gsecret_service_get_finish (result, &error);
	g_assert_no_error (error);
	g_assert (GSECRET_IS_SERVICE (service));
	g_object_unref (result);

	path = gsecret_service_get_session_path (service);
	g_assert (path != NULL);

	g_object_unref (service);
	egg_assert_not_object (service);
}

static void
test_ensure_sync (Test *test,
                  gconstpointer used)
{
	GError *error = NULL;
	GSecretService *service;
	GSecretServiceFlags flags;
	const gchar *path;
	gboolean ret;

	/* Passing true, ensures session is established */
	service = gsecret_service_new_sync (NULL, GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert (service != NULL);

	flags = gsecret_service_get_flags (service);
	g_assert_cmpuint (flags, ==, GSECRET_SERVICE_NONE);

	ret = gsecret_service_ensure_collections_sync (service, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_object_get (service, "flags", &flags, NULL);
	g_assert_cmpuint (flags, ==, GSECRET_SERVICE_LOAD_COLLECTIONS);

	path = gsecret_service_ensure_session_sync (service, NULL, &error);
	g_assert_no_error (error);
	g_assert (path != NULL);

	flags = gsecret_service_get_flags (service);
	g_assert_cmpuint (flags, ==, GSECRET_SERVICE_OPEN_SESSION | GSECRET_SERVICE_LOAD_COLLECTIONS);

	g_object_unref (service);
	egg_assert_not_object (service);
}

static void
test_ensure_async (Test *test,
                   gconstpointer used)
{
	GAsyncResult *result = NULL;
	GSecretServiceFlags flags;
	GSecretService *service;
	GError *error = NULL;
	const gchar *path;
	gboolean ret;

	/* Passing true, ensures session is established */
	service = gsecret_service_new_sync (NULL, GSECRET_SERVICE_NONE, NULL, &error);
	g_assert_no_error (error);
	g_assert (service != NULL);

	flags = gsecret_service_get_flags (service);
	g_assert_cmpuint (flags, ==, GSECRET_SERVICE_NONE);

	gsecret_service_ensure_collections (service, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	ret = gsecret_service_ensure_collections_finish (service, result, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_object_unref (result);
	result = NULL;

	g_object_get (service, "flags", &flags, NULL);
	g_assert_cmpuint (flags, ==, GSECRET_SERVICE_LOAD_COLLECTIONS);

	gsecret_service_ensure_session (service, NULL, on_complete_get_result, &result);
	g_assert (result == NULL);

	egg_test_wait ();

	path = gsecret_service_ensure_session_finish (service, result, &error);
	g_assert_no_error (error);
	g_assert (path != NULL);
	g_object_unref (result);
	result = NULL;

	flags = gsecret_service_get_flags (service);
	g_assert_cmpuint (flags, ==, GSECRET_SERVICE_OPEN_SESSION | GSECRET_SERVICE_LOAD_COLLECTIONS);

	g_object_unref (service);
	egg_assert_not_object (service);
}

static void
test_search_paths_sync (Test *test,
                        gconstpointer used)
{
	GHashTable *attributes;
	gboolean ret;
	gchar **locked;
	gchar **unlocked;
	GError *error = NULL;

	attributes = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (attributes, "number", "1");

	ret = gsecret_service_search_for_paths_sync (test->service, attributes, NULL,
	                                             &unlocked, &locked, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (locked);
	g_assert_cmpstr (locked[0], ==, "/org/freedesktop/secrets/collection/spanish/item_one");

	g_assert (unlocked);
	g_assert_cmpstr (unlocked[0], ==, "/org/freedesktop/secrets/collection/english/item_one");

	g_strfreev (unlocked);
	g_strfreev (locked);

	g_hash_table_unref (attributes);
}

static void
test_search_paths_async (Test *test,
                         gconstpointer used)
{
	GAsyncResult *result = NULL;
	GHashTable *attributes;
	gboolean ret;
	gchar **locked;
	gchar **unlocked;
	GError *error = NULL;

	attributes = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (attributes, "number", "1");

	gsecret_service_search_for_paths (test->service, attributes, NULL,
	                                  on_complete_get_result, &result);
	egg_test_wait ();

	g_assert (G_IS_ASYNC_RESULT (result));
	ret = gsecret_service_search_for_paths_finish (test->service, result,
	                                               &unlocked, &locked,
	                                               &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (locked);
	g_assert_cmpstr (locked[0], ==, "/org/freedesktop/secrets/collection/spanish/item_one");

	g_assert (unlocked);
	g_assert_cmpstr (unlocked[0], ==, "/org/freedesktop/secrets/collection/english/item_one");

	g_strfreev (unlocked);
	g_strfreev (locked);
	g_object_unref (result);

	g_hash_table_unref (attributes);
}

static void
test_search_paths_nulls (Test *test,
                         gconstpointer used)
{
	GAsyncResult *result = NULL;
	GHashTable *attributes;
	gboolean ret;
	gchar **paths;
	GError *error = NULL;

	attributes = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (attributes, "number", "1");

	ret = gsecret_service_search_for_paths_sync (test->service, attributes, NULL,
	                                             &paths, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_assert (paths != NULL);
	g_assert_cmpstr (paths[0], ==, "/org/freedesktop/secrets/collection/english/item_one");
	g_strfreev (paths);

	ret = gsecret_service_search_for_paths_sync (test->service, attributes, NULL,
	                                             NULL, &paths, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_assert (paths != NULL);
	g_assert_cmpstr (paths[0], ==, "/org/freedesktop/secrets/collection/spanish/item_one");
	g_strfreev (paths);

	ret = gsecret_service_search_for_paths_sync (test->service, attributes, NULL,
	                                             NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	gsecret_service_search_for_paths (test->service, attributes, NULL,
	                                  on_complete_get_result, &result);
	egg_test_wait ();
	g_assert (G_IS_ASYNC_RESULT (result));
	ret = gsecret_service_search_for_paths_finish (test->service, result,
	                                               &paths, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_assert (paths != NULL);
	g_assert_cmpstr (paths[0], ==, "/org/freedesktop/secrets/collection/english/item_one");
	g_strfreev (paths);
	g_clear_object (&result);

	gsecret_service_search_for_paths (test->service, attributes, NULL,
	                                  on_complete_get_result, &result);
	egg_test_wait ();
	g_assert (G_IS_ASYNC_RESULT (result));
	ret = gsecret_service_search_for_paths_finish (test->service, result,
	                                               NULL, &paths, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_assert (paths != NULL);
	g_assert_cmpstr (paths[0], ==, "/org/freedesktop/secrets/collection/spanish/item_one");
	g_strfreev (paths);
	g_clear_object (&result);

	gsecret_service_search_for_paths (test->service, attributes, NULL,
	                                  on_complete_get_result, &result);
	egg_test_wait ();
	g_assert (G_IS_ASYNC_RESULT (result));
	ret = gsecret_service_search_for_paths_finish (test->service, result,
	                                               NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_clear_object (&result);

	g_hash_table_unref (attributes);
}

static void
test_search_sync (Test *test,
                  gconstpointer used)
{
	GHashTable *attributes;
	gboolean ret;
	GList *locked;
	GList *unlocked;
	GError *error = NULL;

	attributes = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (attributes, "number", "1");

	ret = gsecret_service_search_sync (test->service, attributes, NULL,
	                                   &unlocked, &locked, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (locked != NULL);
	g_assert_cmpstr (g_dbus_proxy_get_object_path (locked->data), ==, "/org/freedesktop/secrets/collection/spanish/item_one");

	g_assert (unlocked != NULL);
	g_assert_cmpstr (g_dbus_proxy_get_object_path (unlocked->data), ==, "/org/freedesktop/secrets/collection/english/item_one");

	g_list_free_full (unlocked, g_object_unref);
	g_list_free_full (locked, g_object_unref);

	g_hash_table_unref (attributes);
}

static void
test_search_async (Test *test,
                   gconstpointer used)
{
	GAsyncResult *result = NULL;
	GHashTable *attributes;
	gboolean ret;
	GList *locked;
	GList *unlocked;
	GError *error = NULL;

	attributes = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (attributes, "number", "1");

	gsecret_service_search (test->service, attributes, NULL,
	                        on_complete_get_result, &result);
	egg_test_wait ();

	g_assert (G_IS_ASYNC_RESULT (result));
	ret = gsecret_service_search_finish (test->service, result,
	                                     &unlocked, &locked,
	                                     &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (locked != NULL);
	g_assert_cmpstr (g_dbus_proxy_get_object_path (locked->data), ==, "/org/freedesktop/secrets/collection/spanish/item_one");

	g_assert (unlocked != NULL);
	g_assert_cmpstr (g_dbus_proxy_get_object_path (unlocked->data), ==, "/org/freedesktop/secrets/collection/english/item_one");

	g_list_free_full (unlocked, g_object_unref);
	g_list_free_full (locked, g_object_unref);
	g_object_unref (result);

	g_hash_table_unref (attributes);
}

static void
test_search_nulls (Test *test,
                   gconstpointer used)
{
	GAsyncResult *result = NULL;
	GHashTable *attributes;
	gboolean ret;
	GList *items;
	GError *error = NULL;

	attributes = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (attributes, "number", "1");

	ret = gsecret_service_search_sync (test->service, attributes, NULL,
	                                   &items, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_assert (items != NULL);
	g_assert_cmpstr (g_dbus_proxy_get_object_path (items->data), ==, "/org/freedesktop/secrets/collection/english/item_one");
	g_list_free_full (items, g_object_unref);

	ret = gsecret_service_search_sync (test->service, attributes, NULL,
	                                   NULL, &items, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_assert (items != NULL);
	g_assert_cmpstr (g_dbus_proxy_get_object_path (items->data), ==, "/org/freedesktop/secrets/collection/spanish/item_one");
	g_list_free_full (items, g_object_unref);

	ret = gsecret_service_search_sync (test->service, attributes, NULL,
	                                   NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	gsecret_service_search (test->service, attributes, NULL,
	                        on_complete_get_result, &result);
	egg_test_wait ();
	g_assert (G_IS_ASYNC_RESULT (result));
	ret = gsecret_service_search_finish (test->service, result,
	                                     &items, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_assert (items != NULL);
	g_assert_cmpstr (g_dbus_proxy_get_object_path (items->data), ==, "/org/freedesktop/secrets/collection/english/item_one");
	g_list_free_full (items, g_object_unref);
	g_clear_object (&result);

	gsecret_service_search (test->service, attributes, NULL,
	                        on_complete_get_result, &result);
	egg_test_wait ();
	g_assert (G_IS_ASYNC_RESULT (result));
	ret = gsecret_service_search_finish (test->service, result,
	                                     NULL, &items, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_assert (items != NULL);
	g_assert_cmpstr (g_dbus_proxy_get_object_path (items->data), ==, "/org/freedesktop/secrets/collection/spanish/item_one");
	g_list_free_full (items, g_object_unref);
	g_clear_object (&result);

	gsecret_service_search (test->service, attributes, NULL,
	                        on_complete_get_result, &result);
	egg_test_wait ();
	g_assert (G_IS_ASYNC_RESULT (result));
	ret = gsecret_service_search_finish (test->service, result,
	                                     NULL, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
	g_clear_object (&result);

	g_hash_table_unref (attributes);
}

static void
test_secret_for_path_sync (Test *test,
                           gconstpointer used)
{
	GSecretValue *value;
	GError *error = NULL;
	const gchar *path;
	const gchar *password;
	gsize length;

	path = "/org/freedesktop/secrets/collection/english/item_one";
	value = gsecret_service_get_secret_for_path_sync (test->service, path, NULL, &error);
	g_assert_no_error (error);
	g_assert (value != NULL);

	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "111");

	password = gsecret_value_get (value, NULL);
	g_assert_cmpstr (password, ==, "111");

	gsecret_value_unref (value);
}

static void
test_secret_for_path_async (Test *test,
                            gconstpointer used)
{
	GSecretValue *value;
	GError *error = NULL;
	const gchar *path;
	const gchar *password;
	GAsyncResult *result = NULL;
	gsize length;

	path = "/org/freedesktop/secrets/collection/english/item_one";
	gsecret_service_get_secret_for_path (test->service, path, NULL,
	                                     on_complete_get_result, &result);
	g_assert (result == NULL);
	egg_test_wait ();

	value = gsecret_service_get_secret_for_path_finish (test->service, result, &error);
	g_assert_no_error (error);
	g_assert (value != NULL);
	g_object_unref (result);

	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "111");

	password = gsecret_value_get (value, NULL);
	g_assert_cmpstr (password, ==, "111");

	gsecret_value_unref (value);
}

static void
test_secrets_for_paths_sync (Test *test,
                             gconstpointer used)
{
	const gchar *path_item_one = "/org/freedesktop/secrets/collection/english/item_one";
	const gchar *path_item_two = "/org/freedesktop/secrets/collection/english/item_two";
	const gchar *paths[] = {
		path_item_one,
		path_item_two,

		/* This one is locked, and not returned */
		"/org/freedesktop/secrets/collection/spanish/item_one",
		NULL
	};

	GSecretValue *value;
	GHashTable *values;
	GError *error = NULL;
	const gchar *password;
	gsize length;

	values = gsecret_service_get_secrets_for_paths_sync (test->service, paths, NULL, &error);
	g_assert_no_error (error);

	g_assert (values != NULL);
	g_assert_cmpuint (g_hash_table_size (values), ==, 2);

	value = g_hash_table_lookup (values, path_item_one);
	g_assert (value != NULL);
	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "111");

	value = g_hash_table_lookup (values, path_item_two);
	g_assert (value != NULL);
	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "222");

	g_hash_table_unref (values);
}

static void
test_secrets_for_paths_async (Test *test,
                              gconstpointer used)
{
	const gchar *path_item_one = "/org/freedesktop/secrets/collection/english/item_one";
	const gchar *path_item_two = "/org/freedesktop/secrets/collection/english/item_two";
	const gchar *paths[] = {
		path_item_one,
		path_item_two,

		/* This one is locked, and not returned */
		"/org/freedesktop/secrets/collection/spanish/item_one",
		NULL
	};

	GSecretValue *value;
	GHashTable *values;
	GError *error = NULL;
	const gchar *password;
	GAsyncResult *result = NULL;
	gsize length;

	gsecret_service_get_secrets_for_paths (test->service, paths, NULL,
	                                       on_complete_get_result, &result);
	g_assert (result == NULL);
	egg_test_wait ();

	values = gsecret_service_get_secrets_for_paths_finish (test->service, result, &error);
	g_assert_no_error (error);
	g_object_unref (result);

	g_assert (values != NULL);
	g_assert_cmpuint (g_hash_table_size (values), ==, 2);

	value = g_hash_table_lookup (values, path_item_one);
	g_assert (value != NULL);
	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "111");

	value = g_hash_table_lookup (values, path_item_two);
	g_assert (value != NULL);
	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "222");

	g_hash_table_unref (values);
}

static void
test_secrets_sync (Test *test,
                   gconstpointer used)
{
	const gchar *path_item_one = "/org/freedesktop/secrets/collection/english/item_one";
	const gchar *path_item_two = "/org/freedesktop/secrets/collection/english/item_two";
	const gchar *path_item_three = "/org/freedesktop/secrets/collection/spanish/item_one";

	GSecretValue *value;
	GHashTable *values;
	GError *error = NULL;
	const gchar *password;
	GSecretItem *item_one, *item_two, *item_three;
	GList *items = NULL;
	gsize length;

	item_one = gsecret_item_new_sync (test->service, path_item_one, NULL, &error);
	item_two = gsecret_item_new_sync (test->service, path_item_two, NULL, &error);
	item_three = gsecret_item_new_sync (test->service, path_item_three, NULL, &error);

	items = g_list_append (items, item_one);
	items = g_list_append (items, item_two);
	items = g_list_append (items, item_three);

	values = gsecret_service_get_secrets_sync (test->service, items, NULL, &error);
	g_list_free_full (items, g_object_unref);
	g_assert_no_error (error);

	g_assert (values != NULL);
	g_assert_cmpuint (g_hash_table_size (values), ==, 2);

	value = g_hash_table_lookup (values, item_one);
	g_assert (value != NULL);
	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "111");

	value = g_hash_table_lookup (values, item_two);
	g_assert (value != NULL);
	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "222");

	g_hash_table_unref (values);
}

static void
test_secrets_async (Test *test,
                              gconstpointer used)
{
	const gchar *path_item_one = "/org/freedesktop/secrets/collection/english/item_one";
	const gchar *path_item_two = "/org/freedesktop/secrets/collection/english/item_two";
	const gchar *path_item_three = "/org/freedesktop/secrets/collection/spanish/item_one";

	GSecretValue *value;
	GHashTable *values;
	GError *error = NULL;
	const gchar *password;
	GAsyncResult *result = NULL;
	GSecretItem *item_one, *item_two, *item_three;
	GList *items = NULL;
	gsize length;

	item_one = gsecret_item_new_sync (test->service, path_item_one, NULL, &error);
	item_two = gsecret_item_new_sync (test->service, path_item_two, NULL, &error);
	item_three = gsecret_item_new_sync (test->service, path_item_three, NULL, &error);

	items = g_list_append (items, item_one);
	items = g_list_append (items, item_two);
	items = g_list_append (items, item_three);

	gsecret_service_get_secrets (test->service, items, NULL,
	                             on_complete_get_result, &result);
	g_assert (result == NULL);
	g_list_free (items);

	egg_test_wait ();

	values = gsecret_service_get_secrets_finish (test->service, result, &error);
	g_assert_no_error (error);
	g_object_unref (result);

	g_assert (values != NULL);
	g_assert_cmpuint (g_hash_table_size (values), ==, 2);

	value = g_hash_table_lookup (values, item_one);
	g_assert (value != NULL);
	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "111");

	value = g_hash_table_lookup (values, item_two);
	g_assert (value != NULL);
	password = gsecret_value_get (value, &length);
	g_assert_cmpuint (length, ==, 3);
	g_assert_cmpstr (password, ==, "222");

	g_hash_table_unref (values);

	g_object_unref (item_one);
	g_object_unref (item_two);
	g_object_unref (item_three);
}

static void
test_delete_for_path_sync (Test *test,
                           gconstpointer used)

{
	const gchar *path_item_one = "/org/freedesktop/secrets/collection/to_delete/item";
	GError *error = NULL;
	gboolean ret;

	ret = gsecret_service_delete_path_sync (test->service, path_item_one, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
}

static void
test_delete_for_path_sync_prompt (Test *test,
                                  gconstpointer used)

{
	const gchar *path_item_one = "/org/freedesktop/secrets/collection/to_delete/confirm";
	GError *error = NULL;
	gboolean ret;

	ret = gsecret_service_delete_path_sync (test->service, path_item_one, NULL, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);
}

static void
test_lock_paths_sync (Test *test,
                      gconstpointer used)
{
	const gchar *collection_path = "/org/freedesktop/secrets/collection/lock_one";
	const gchar *paths[] = {
		collection_path,
		NULL,
	};

	GError *error = NULL;
	gchar **locked = NULL;
	gboolean ret;

	ret = gsecret_service_lock_paths_sync (test->service, paths, NULL, &locked, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (locked != NULL);
	g_assert_cmpstr (locked[0], ==, collection_path);
	g_assert (locked[1] == NULL);
	g_strfreev (locked);
}

static void
test_lock_prompt_sync (Test *test,
                       gconstpointer used)
{
	const gchar *collection_path = "/org/freedesktop/secrets/collection/lock_prompt";
	const gchar *paths[] = {
		collection_path,
		NULL,
	};

	GError *error = NULL;
	gchar **locked = NULL;
	gboolean ret;

	ret = gsecret_service_lock_paths_sync (test->service, paths, NULL, &locked, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (locked != NULL);
	g_assert_cmpstr (locked[0], ==, collection_path);
	g_assert (locked[1] == NULL);
	g_strfreev (locked);
}

static void
test_lock_sync (Test *test,
                gconstpointer used)
{
	const gchar *collection_path = "/org/freedesktop/secrets/collection/lock_one";
	GSecretCollection *collection;
	GError *error = NULL;
	GList *locked;
	GList *objects;
	gboolean ret;

	collection = gsecret_collection_new_sync (test->service, collection_path, NULL, &error);
	g_assert_no_error (error);

	objects = g_list_append (NULL, collection);

	ret = gsecret_service_lock_sync (test->service, objects, NULL, &locked, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (locked != NULL);
	g_assert (locked->data == collection);
	g_assert (locked->next == NULL);
	g_list_free_full (locked, g_object_unref);

	g_list_free (objects);
	g_object_unref (collection);
}

static void
test_unlock_paths_sync (Test *test,
                        gconstpointer used)
{
	const gchar *collection_path = "/org/freedesktop/secrets/collection/lock_one";
	const gchar *paths[] = {
		collection_path,
		NULL,
	};

	GError *error = NULL;
	gchar **unlocked = NULL;
	gboolean ret;

	ret = gsecret_service_unlock_paths_sync (test->service, paths, NULL, &unlocked, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (unlocked != NULL);
	g_assert_cmpstr (unlocked[0], ==, collection_path);
	g_assert (unlocked[1] == NULL);
	g_strfreev (unlocked);
}

static void
test_unlock_prompt_sync (Test *test,
                         gconstpointer used)
{
	const gchar *collection_path = "/org/freedesktop/secrets/collection/lock_prompt";
	const gchar *paths[] = {
		collection_path,
		NULL,
	};

	GError *error = NULL;
	gchar **unlocked = NULL;
	gboolean ret;

	ret = gsecret_service_unlock_paths_sync (test->service, paths, NULL, &unlocked, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (unlocked != NULL);
	g_assert_cmpstr (unlocked[0], ==, collection_path);
	g_assert (unlocked[1] == NULL);
	g_strfreev (unlocked);
}

static void
test_unlock_sync (Test *test,
                  gconstpointer used)
{
	const gchar *collection_path = "/org/freedesktop/secrets/collection/lock_one";
	GSecretCollection *collection;
	GError *error = NULL;
	GList *unlocked;
	GList *objects;
	gboolean ret;

	collection = gsecret_collection_new_sync (test->service, collection_path, NULL, &error);
	g_assert_no_error (error);

	objects = g_list_append (NULL, collection);

	ret = gsecret_service_unlock_sync (test->service, objects, NULL, &unlocked, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_assert (unlocked != NULL);
	g_assert (unlocked->data == collection);
	g_assert (unlocked->next == NULL);
	g_list_free_full (unlocked, g_object_unref);

	g_list_free (objects);
	g_object_unref (collection);
}

static void
test_remove_sync (Test *test,
                  gconstpointer used)
{
	GError *error = NULL;
	gboolean ret;

	ret = gsecret_service_remove_sync (test->service, &DELETE_SCHEMA, NULL, &error,
	                                   "even", FALSE,
	                                   "string", "one",
	                                   "number", 1,
	                                   NULL);

	g_assert_no_error (error);
	g_assert (ret == TRUE);
}

static void
test_remove_async (Test *test,
                   gconstpointer used)
{
	GError *error = NULL;
	GAsyncResult *result = NULL;
	gboolean ret;

	gsecret_service_remove (test->service, &DELETE_SCHEMA, NULL,
	                        on_complete_get_result, &result,
	                        "even", FALSE,
	                        "string", "one",
	                        "number", 1,
	                        NULL);

	g_assert (result == NULL);

	egg_test_wait ();

	ret = gsecret_service_remove_finish (test->service, result, &error);
	g_assert_no_error (error);
	g_assert (ret == TRUE);

	g_object_unref (result);
}

static void
test_remove_locked (Test *test,
                    gconstpointer used)
{
	GError *error = NULL;
	gboolean ret;

	ret = gsecret_service_remove_sync (test->service, &DELETE_SCHEMA, NULL, &error,
	                                   "even", FALSE,
	                                   "string", "tres",
	                                   "number", 3,
	                                   NULL);

	g_assert_no_error (error);
	g_assert (ret == TRUE);
}

static void
test_remove_no_match (Test *test,
                      gconstpointer used)
{
	GError *error = NULL;
	gboolean ret;

	/* Won't match anything */
	ret = gsecret_service_remove_sync (test->service, &DELETE_SCHEMA, NULL, &error,
	                                   "even", TRUE,
	                                   "string", "one",
	                                   NULL);

	g_assert_no_error (error);
	g_assert (ret == FALSE);
}

static void
test_lookup_sync (Test *test,
                  gconstpointer used)
{
	GError *error = NULL;
	GSecretValue *value;
	gsize length;

	value = gsecret_service_lookup_sync (test->service, &STORE_SCHEMA, NULL, &error,
	                                     "even", FALSE,
	                                     "string", "one",
	                                     "number", 1,
	                                     NULL);

	g_assert_no_error (error);

	g_assert (value != NULL);
	g_assert_cmpstr (gsecret_value_get (value, &length), ==, "111");
	g_assert_cmpuint (length, ==, 3);

	gsecret_value_unref (value);
}

static void
test_lookup_async (Test *test,
                   gconstpointer used)
{
	GError *error = NULL;
	GAsyncResult *result = NULL;
	GSecretValue *value;
	gsize length;

	gsecret_service_lookup (test->service, &STORE_SCHEMA, NULL,
	                        on_complete_get_result, &result,
	                        "even", FALSE,
	                        "string", "one",
	                        "number", 1,
	                        NULL);

	g_assert (result == NULL);

	egg_test_wait ();

	value = gsecret_service_lookup_finish (test->service, result, &error);
	g_assert_no_error (error);

	g_assert (value != NULL);
	g_assert_cmpstr (gsecret_value_get (value, &length), ==, "111");
	g_assert_cmpuint (length, ==, 3);

	gsecret_value_unref (value);
	g_object_unref (result);
}

static void
test_lookup_locked (Test *test,
                    gconstpointer used)
{
	GError *error = NULL;
	GSecretValue *value;
	gsize length;

	value = gsecret_service_lookup_sync (test->service, &STORE_SCHEMA, NULL, &error,
	                                     "even", FALSE,
	                                     "string", "tres",
	                                     "number", 3,
	                                     NULL);

	g_assert_no_error (error);

	g_assert (value != NULL);
	g_assert_cmpstr (gsecret_value_get (value, &length), ==, "3333");
	g_assert_cmpuint (length, ==, 4);

	gsecret_value_unref (value);
}

static void
test_lookup_no_match (Test *test,
                      gconstpointer used)
{
	GError *error = NULL;
	GSecretValue *value;

	/* Won't match anything */
	value = gsecret_service_lookup_sync (test->service, &STORE_SCHEMA, NULL, &error,
	                                     "even", TRUE,
	                                     "string", "one",
	                                     NULL);

	g_assert_no_error (error);
	g_assert (value == NULL);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	g_set_prgname ("test-service");
	g_type_init ();

	g_test_add_func ("/service/get-sync", test_get_sync);
	g_test_add_func ("/service/get-async", test_get_async);
	g_test_add ("/service/get-more-sync", Test, "mock-service-normal.py", setup_mock, test_get_more_sync, teardown_mock);
	g_test_add ("/service/get-more-async", Test, "mock-service-normal.py", setup_mock, test_get_more_async, teardown_mock);

	g_test_add_func ("/service/new-sync", test_new_sync);
	g_test_add_func ("/service/new-async", test_new_async);
	g_test_add ("/service/new-more-sync", Test, "mock-service-normal.py", setup_mock, test_new_more_sync, teardown_mock);
	g_test_add ("/service/new-more-async", Test, "mock-service-normal.py", setup_mock, test_new_more_async, teardown_mock);

	g_test_add ("/service/connect-sync", Test, "mock-service-normal.py", setup_mock, test_connect_async, teardown_mock);
	g_test_add ("/service/connect-ensure-sync", Test, "mock-service-normal.py", setup_mock, test_connect_ensure_async, teardown_mock);
	g_test_add ("/service/ensure-sync", Test, "mock-service-normal.py", setup_mock, test_ensure_sync, teardown_mock);
	g_test_add ("/service/ensure-async", Test, "mock-service-normal.py", setup_mock, test_ensure_async, teardown_mock);

	g_test_add ("/service/search-for-paths", Test, "mock-service-normal.py", setup, test_search_paths_sync, teardown);
	g_test_add ("/service/search-for-paths-async", Test, "mock-service-normal.py", setup, test_search_paths_async, teardown);
	g_test_add ("/service/search-for-paths-nulls", Test, "mock-service-normal.py", setup, test_search_paths_nulls, teardown);
	g_test_add ("/service/search-sync", Test, "mock-service-normal.py", setup, test_search_sync, teardown);
	g_test_add ("/service/search-async", Test, "mock-service-normal.py", setup, test_search_async, teardown);
	g_test_add ("/service/search-nulls", Test, "mock-service-normal.py", setup, test_search_nulls, teardown);

	g_test_add ("/service/secret-for-path-sync", Test, "mock-service-normal.py", setup, test_secret_for_path_sync, teardown);
	g_test_add ("/service/secret-for-path-plain", Test, "mock-service-only-plain.py", setup, test_secret_for_path_sync, teardown);
	g_test_add ("/service/secret-for-path-async", Test, "mock-service-normal.py", setup, test_secret_for_path_async, teardown);
	g_test_add ("/service/secrets-for-paths-sync", Test, "mock-service-normal.py", setup, test_secrets_for_paths_sync, teardown);
	g_test_add ("/service/secrets-for-paths-async", Test, "mock-service-normal.py", setup, test_secrets_for_paths_async, teardown);
	g_test_add ("/service/secrets-sync", Test, "mock-service-normal.py", setup, test_secrets_sync, teardown);
	g_test_add ("/service/secrets-async", Test, "mock-service-normal.py", setup, test_secrets_async, teardown);

	g_test_add ("/service/delete-for-path", Test, "mock-service-delete.py", setup, test_delete_for_path_sync, teardown);
	g_test_add ("/service/delete-for-path-with-prompt", Test, "mock-service-delete.py", setup, test_delete_for_path_sync_prompt, teardown);

	g_test_add ("/service/lock-paths-sync", Test, "mock-service-lock.py", setup, test_lock_paths_sync, teardown);
	g_test_add ("/service/lock-prompt-sync", Test, "mock-service-lock.py", setup, test_lock_prompt_sync, teardown);
	g_test_add ("/service/lock-sync", Test, "mock-service-lock.py", setup, test_lock_sync, teardown);

	g_test_add ("/service/unlock-paths-sync", Test, "mock-service-lock.py", setup, test_unlock_paths_sync, teardown);
	g_test_add ("/service/unlock-prompt-sync", Test, "mock-service-lock.py", setup, test_unlock_prompt_sync, teardown);
	g_test_add ("/service/unlock-sync", Test, "mock-service-lock.py", setup, test_unlock_sync, teardown);

	g_test_add ("/service/lookup-sync", Test, "mock-service-normal.py", setup, test_lookup_sync, teardown);
	g_test_add ("/service/lookup-async", Test, "mock-service-normal.py", setup, test_lookup_async, teardown);
	g_test_add ("/service/lookup-locked", Test, "mock-service-normal.py", setup, test_lookup_locked, teardown);
	g_test_add ("/service/lookup-no-match", Test, "mock-service-normal.py", setup, test_lookup_no_match, teardown);

	g_test_add ("/service/remove-sync", Test, "mock-service-delete.py", setup, test_remove_sync, teardown);
	g_test_add ("/service/remove-async", Test, "mock-service-delete.py", setup, test_remove_async, teardown);
	g_test_add ("/service/remove-locked", Test, "mock-service-delete.py", setup, test_remove_locked, teardown);
	g_test_add ("/service/remove-no-match", Test, "mock-service-delete.py", setup, test_remove_no_match, teardown);

	return egg_tests_run_with_loop ();
}
