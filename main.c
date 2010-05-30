#include <glib.h>
#include <libmafw/mafw.h>
#include <libmafw-shared/mafw-shared.h>

#include <string.h>

#include "scrobble.h"

static GMainLoop *main_loop;
static GKeyFile *keyfile;
static sr_session_t *lastfm;
static sr_session_t *librefm;
static sr_track_t *track;

static void
metadata_callback(MafwRenderer *self,
		  const gchar *object_id,
		  GHashTable *metadata,
		  gpointer user_data,
		  const GError *error)
{
	sr_track_t *t;
	t = sr_track_dup(track);
	sr_session_add_track(lastfm, t);
	sr_session_submit(lastfm);
	t = sr_track_dup(track);
	sr_session_add_track(librefm, t);
	sr_session_submit(librefm);
}

static void
metadata_changed_cb(MafwRenderer *renderer,
		    const gchar *name,
		    GValueArray *value_array,
		    gpointer data)
{
	GValue *value = g_value_array_get_nth(value_array, 0);
	if (strcmp(name, "artist") == 0) {
		g_free(track->artist);
		track->artist = g_value_dup_string(value);
	}
	else if (strcmp(name, "title") == 0) {
		g_free(track->title);
		track->title = g_value_dup_string(value);
	}
	else if (strcmp(name, "duration") == 0) {
		track->length = (int) g_value_get_int64(value);
	}
	else if (strcmp(name, "album") == 0) {
		g_free(track->album);
		track->album = g_value_dup_string(value);
	}
}

static void
state_changed_cb(MafwRenderer *renderer,
		 MafwPlayState state,
		 gpointer user_data)
{
	switch (state) {
	case Playing:
		track->timestamp = time(NULL);
		mafw_renderer_get_current_metadata(renderer,
						   metadata_callback,
						   user_data);
		break;
	case Transitioning:
		break;
	case Paused:
		break;
	case Stopped:
		sr_session_pause(lastfm);
		sr_session_pause(librefm);
		break;
	default:
		break;
	}
}

static void
renderer_added_cb(MafwRegistry *registry,
		  GObject *renderer,
		  gpointer user_data)
{
	const gchar *name;

	if (!MAFW_IS_RENDERER(renderer))
		return;

	name = mafw_extension_get_name(MAFW_EXTENSION(renderer));

	if (strcmp(name, "Mafw-Gst-Renderer") != 0)
		return;

	g_signal_connect(renderer,
			 "state-changed",
			 G_CALLBACK(state_changed_cb),
			 user_data);
	g_signal_connect(renderer,
			 "metadata-changed",
			 G_CALLBACK(metadata_changed_cb),
			 user_data);
}

static void error_cb(int fatal,
		     const char *msg)
{
	g_warning(msg);
	if (fatal)
		g_main_loop_quit(main_loop);
}

static gboolean
load_cred(sr_session_t *s,
	  const char *id)
{
	gchar *username = NULL, *password = NULL;
	gboolean ok;

	username = g_key_file_get_string(keyfile, id, "username", NULL);
	password = g_key_file_get_string(keyfile, id, "password", NULL);

	ok = username && password;
	if (!ok)
		goto leave;

	sr_session_set_cred(s, username, password);

leave:
	g_free(username);
	g_free(password);

	return ok;
}

static sr_session_t *
get_session(const char *url,
	    const char *id)
{
	sr_session_t *s;
	s = sr_session_new(url, "tst", "1.0");
	s->error_cb = error_cb;
	if (!load_cred(s, id)) {
		sr_session_free(s);
		return NULL;
	}
	sr_session_handshake(s);
	return s;
}

static gboolean
authenticate(void)
{
	gchar *file;
	gboolean ok;

	keyfile = g_key_file_new();

	file = g_build_filename(g_get_home_dir(), ".osso",
				"scrobbler", NULL);

	ok = g_key_file_load_from_file(keyfile, file, G_KEY_FILE_NONE, NULL);
	if (!ok)
		goto leave;

	g_free(file);

	lastfm = get_session(SR_LASTFM_URL, "lastfm");
	librefm = get_session(SR_LIBREFM_URL, "librefm");
	if (!lastfm && !librefm)
		goto leave;

leave:
	g_key_file_free(keyfile);
	return ok;
}

int main(void)
{
	GError *error = NULL;
	MafwRegistry *registry;

	g_type_init();
	if (!g_thread_supported())
		g_thread_init(NULL);

	if (!authenticate())
		return -1;

	registry = MAFW_REGISTRY(mafw_registry_get_instance());
	if (!registry)
		g_error("Failed to get register");

	mafw_shared_init(registry, &error);
	if (error)
		g_error("Failed to initialize the shared library");

	g_signal_connect(registry,
			 "renderer-added",
			 G_CALLBACK(renderer_added_cb), NULL);

	track = sr_track_new();
	track->source = 'P';

	main_loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(main_loop);

	sr_track_free(track);

	sr_session_free(lastfm);
	sr_session_free(librefm);
	return 0;
}
