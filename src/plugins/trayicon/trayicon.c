/*
 * Claws Mail -- a GTK+ based, lightweight, and fast e-mail client
 * Copyright (C) 2003-2008 the Claws Mail Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "common/claws.h"
#include "common/version.h"
#include "plugin.h"
#include "utils.h"
#include "hooks.h"
#include "folder.h"
#include "mainwindow.h"
#include "gtkutils.h"
#include "menu.h"
#include "toolbar.h"
#include "prefs_common.h"
#include "main.h"
#include "alertpanel.h"
#include "account.h"
#include "gtk/manage_window.h"

#include "eggtrayicon.h"

#include "trayicon_prefs.h"

#include "stock_pixmap.h"

#define PLUGIN_NAME (_("Trayicon"))

static guint item_hook_id;
static guint folder_hook_id;
static guint offline_hook_id;
static guint account_hook_id;
static guint close_hook_id;
static guint iconified_hook_id;
static guint theme_hook_id;

static GdkPixmap *newmail_pixmap[2] = {NULL, NULL};
static GdkPixmap *newmail_bitmap[2] = {NULL, NULL};
static GdkPixmap *unreadmail_pixmap[2] = {NULL, NULL};
static GdkPixmap *unreadmail_bitmap[2] = {NULL, NULL};
static GdkPixmap *newmarkedmail_pixmap[2] = {NULL, NULL};
static GdkPixmap *newmarkedmail_bitmap[2] = {NULL, NULL};
static GdkPixmap *unreadmarkedmail_pixmap[2] = {NULL, NULL};
static GdkPixmap *unreadmarkedmail_bitmap[2] = {NULL, NULL};
static GdkPixmap *nomail_pixmap[2] = {NULL, NULL};
static GdkPixmap *nomail_bitmap[2] = {NULL, NULL};

static EggTrayIcon *trayicon;
static GtkWidget *eventbox;
static GtkWidget *image = NULL;

#if !(GTK_CHECK_VERSION(2,12,0))
static GtkTooltips *tooltips;
#endif
static GtkWidget *traymenu_popup;
static GtkItemFactory *traymenu_factory;
static gboolean updating_menu = FALSE;

guint destroy_signal_id;

typedef enum
{
	TRAYICON_NEW,
	TRAYICON_NEWMARKED,
	TRAYICON_UNREAD,
	TRAYICON_UNREADMARKED,
	TRAYICON_NOTHING
} TrayIconType;

static void trayicon_get_all_cb	    (gpointer data, guint action, GtkWidget *widget);
static void trayicon_compose_cb	    (gpointer data, guint action, GtkWidget *widget);
static void trayicon_compose_acc_cb (GtkMenuItem *menuitem, gpointer data );
static void trayicon_addressbook_cb (gpointer data, guint action, GtkWidget *widget);
static void trayicon_exit_cb	    (gpointer data, guint action, GtkWidget *widget);
static void trayicon_toggle_offline_cb	(gpointer data, guint action, GtkWidget *widget);
static void resize_cb		    (GtkWidget *widget, GtkRequisition *req, gpointer user_data);

static GtkItemFactoryEntry trayicon_popup_menu_entries[] =
{
	{N_("/_Get Mail"),		NULL, trayicon_get_all_cb,		0, NULL},
	{"/---",			NULL, NULL,				0, "<Separator>"},
	{N_("/_Email"),			NULL, trayicon_compose_cb,		0, NULL},
	{N_("/_Email from account"),	NULL, NULL,				0, "<Branch>"},
	{"/---",			NULL, NULL,				0, "<Separator>"},
	{N_("/Open A_ddressbook"),	NULL, trayicon_addressbook_cb,		0, NULL},
	{"/---",			NULL, NULL,				0, "<Separator>"},
	{N_("/_Work Offline"),		NULL, trayicon_toggle_offline_cb,	0, "<CheckItem>"},
	{"/---",			NULL, NULL,				0, "<Separator>"},
	{N_("/E_xit Claws Mail"),	NULL, trayicon_exit_cb,			0, NULL}
};

static gboolean trayicon_set_accounts_hook(gpointer source, gpointer data)
{
	GList *cur_ac, *cur_item = NULL;
	GtkWidget *menu;
	GtkWidget *menuitem;
	PrefsAccount *ac_prefs;

	GList *account_list = account_get_list();

	menu = gtk_item_factory_get_widget(traymenu_factory,
					   "/Email from account");

	/* destroy all previous menu item */
	cur_item = GTK_MENU_SHELL(menu)->children;
	while (cur_item != NULL) {
		GList *next = cur_item->next;
		gtk_widget_destroy(GTK_WIDGET(cur_item->data));
		cur_item = next;
	}

	for (cur_ac = account_list; cur_ac != NULL; cur_ac = cur_ac->next) {
		ac_prefs = (PrefsAccount *)cur_ac->data;

		menuitem = gtk_menu_item_new_with_label
			(ac_prefs->account_name ? ac_prefs->account_name
			 : _("Untitled"));
		gtk_widget_show(menuitem);
		gtk_menu_append(GTK_MENU(menu), menuitem);
		g_signal_connect(G_OBJECT(menuitem), "activate",
				 G_CALLBACK(trayicon_compose_acc_cb),
				 ac_prefs);
	}
	return FALSE;
}

static void set_trayicon_pixmap(TrayIconType icontype)
{
	GdkPixmap *pixmap = NULL;
	GdkBitmap *bitmap = NULL;
	static GdkPixmap *last_pixmap = NULL;

	switch(icontype) {
	case TRAYICON_NEW:
		pixmap = newmail_pixmap[prefs_common.work_offline];
		bitmap = newmail_bitmap[prefs_common.work_offline];
		break;
	case TRAYICON_NEWMARKED:
		pixmap = newmarkedmail_pixmap[prefs_common.work_offline];
		bitmap = newmarkedmail_bitmap[prefs_common.work_offline];
		break;
	case TRAYICON_UNREAD:
		pixmap = unreadmail_pixmap[prefs_common.work_offline];
		bitmap = unreadmail_bitmap[prefs_common.work_offline];
		break;
	case TRAYICON_UNREADMARKED:
		pixmap = unreadmarkedmail_pixmap[prefs_common.work_offline];
		bitmap = unreadmarkedmail_bitmap[prefs_common.work_offline];
		break;
	default:
		pixmap = nomail_pixmap[prefs_common.work_offline];
		bitmap = nomail_bitmap[prefs_common.work_offline];
		break;
	}

	if (pixmap == last_pixmap) {
		return;
	}

	gtk_image_set_from_pixmap(GTK_IMAGE(image), pixmap, bitmap);

	last_pixmap = pixmap;
}

static void update(FolderItem *removed_item)
{
	guint new, unread, unreadmarked, marked, total;
	guint replied, forwarded, locked, ignored, watched;
	gchar *buf;
	TrayIconType icontype = TRAYICON_NOTHING;

	folder_count_total_msgs(&new, &unread, &unreadmarked, &marked, &total,
				&replied, &forwarded, &locked, &ignored,
				&watched);
	if (removed_item) {
		total -= removed_item->total_msgs;
		new -= removed_item->new_msgs;
		unread -= removed_item->unread_msgs;
	}

	buf = g_strdup_printf(_("New %d, Unread: %d, Total: %d"), new, unread, total);

#if !(GTK_CHECK_VERSION(2,12,0))
        gtk_tooltips_set_tip(tooltips, eventbox, buf, "");
#else
	gtk_widget_set_tooltip_text(eventbox, buf);
#endif
	g_free(buf);

	if (new > 0 && unreadmarked > 0) {
		icontype = TRAYICON_NEWMARKED;
	} else if (new > 0) {
		icontype = TRAYICON_NEW;
	} else if (unreadmarked > 0) {
		icontype = TRAYICON_UNREADMARKED;
	} else if (unread > 0) {
		icontype = TRAYICON_UNREAD;
	}

	set_trayicon_pixmap(icontype);
}

static gboolean folder_item_update_hook(gpointer source, gpointer data)
{
	update(NULL);

	return FALSE;
}

static gboolean folder_update_hook(gpointer source, gpointer data)
{
	FolderUpdateData *hookdata;
	hookdata = source;
	if (hookdata->update_flags & FOLDER_REMOVE_FOLDERITEM)
		update(hookdata->item);
	else
		update(NULL);

	return FALSE;
}

static gboolean offline_update_hook(gpointer source, gpointer data)
{
	update(NULL);
	return FALSE;
}

static gboolean trayicon_close_hook(gpointer source, gpointer data)
{
	if (source) {
		gboolean *close_allowed = (gboolean*)source;

		if (trayicon_prefs.close_to_tray) {
		MainWindow *mainwin = mainwindow_get_mainwindow();

			*close_allowed = FALSE;
		if (GTK_WIDGET_VISIBLE(GTK_WIDGET(mainwin->window)))
			main_window_hide(mainwin);
	}
	}
	return FALSE;
}

static gboolean trayicon_got_iconified_hook(gpointer source, gpointer data)
{
	MainWindow *mainwin = mainwindow_get_mainwindow();

	if (trayicon_prefs.hide_when_iconified
			&& GTK_WIDGET_VISIBLE(GTK_WIDGET(mainwin->window))
			&& !gtk_window_get_skip_taskbar_hint(GTK_WINDOW(mainwin->window))) {
		gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mainwin->window), TRUE);
	}
	return FALSE;
}

static void resize_cb(GtkWidget *widget, GtkRequisition *req,
		      gpointer user_data)
{
	update(NULL);
}

static void fix_folderview_scroll(MainWindow *mainwin)
{
	static gboolean fix_done = FALSE;

	if (fix_done)
		return;

	gtk_widget_queue_resize(mainwin->folderview->ctree);

	fix_done = TRUE;
}

static gboolean click_cb(GtkWidget * widget,
		         GdkEventButton * event, gpointer user_data)
{
	MainWindow *mainwin;

	if (event == NULL) {
		return TRUE;
	}

	mainwin = mainwindow_get_mainwindow();

	switch (event->button) {
	case 1:
		if (GTK_WIDGET_VISIBLE(GTK_WIDGET(mainwin->window))) {
			if ((gdk_window_get_state(GTK_WIDGET(mainwin->window)->window)&GDK_WINDOW_STATE_ICONIFIED)
					|| mainwindow_is_obscured()) {
				gtk_window_deiconify(GTK_WINDOW(mainwin->window));
				gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mainwin->window), FALSE);
				main_window_show(mainwin);
				gtk_window_present(GTK_WINDOW(mainwin->window));
			} else {
				main_window_hide(mainwin);
			}
		} else {
			gtk_window_deiconify(GTK_WINDOW(mainwin->window));
			gtk_window_set_skip_taskbar_hint(GTK_WINDOW(mainwin->window), FALSE);
			main_window_show(mainwin);
			gtk_window_present(GTK_WINDOW(mainwin->window));
			fix_folderview_scroll(mainwin);
        }
		break;
	case 3:
		/* tell callbacks to skip any event */
		updating_menu = TRUE;
		/* initialize checkitem according to current offline state */
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(gtk_item_factory_get_item(traymenu_factory,
			"/Work Offline")), prefs_common.work_offline);
		gtk_widget_set_sensitive(
			GTK_WIDGET(gtk_item_factory_get_item(traymenu_factory,
			"/Get Mail")), mainwin->lock_count == 0);
		updating_menu = FALSE;

		gtk_menu_popup( GTK_MENU(traymenu_popup), NULL, NULL, NULL, NULL,
		       event->button, event->time );
		break;
	default:
		return TRUE;
	}
	return TRUE;
}

static void create_trayicon(void);

static void destroy_cb(GtkWidget *widget, gpointer *data)
{
	debug_print("Widget destroyed\n");

	create_trayicon();
}

static gboolean trayicon_update_theme(gpointer source, gpointer data)
{
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_NOMAIL, &nomail_pixmap[0], &nomail_bitmap[0]);
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_UNREADMAIL, &unreadmail_pixmap[0], &unreadmail_bitmap[0]);
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_NEWMAIL, &newmail_pixmap[0], &newmail_bitmap[0]);
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_UNREADMARKEDMAIL, &unreadmarkedmail_pixmap[0], &unreadmarkedmail_bitmap[0]);
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_NEWMARKEDMAIL, &newmarkedmail_pixmap[0], &newmarkedmail_bitmap[0]);

	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_NOMAIL_OFFLINE, &nomail_pixmap[1], &nomail_bitmap[1]);
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_UNREADMAIL_OFFLINE, &unreadmail_pixmap[1], &unreadmail_bitmap[1]);
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_NEWMAIL_OFFLINE, &newmail_pixmap[1], &newmail_bitmap[1]);
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_UNREADMARKEDMAIL_OFFLINE, &unreadmarkedmail_pixmap[1], &unreadmarkedmail_bitmap[1]);
	stock_pixmap_gdk(GTK_WIDGET(trayicon), STOCK_PIXMAP_TRAY_NEWMARKEDMAIL_OFFLINE, &newmarkedmail_pixmap[1], &newmarkedmail_bitmap[1]);

	if (image != NULL)
		update(NULL);

	return FALSE;
}

static void create_trayicon()
{
	gint n_entries = 0;

	trayicon = egg_tray_icon_new("Claws Mail");
	gtk_widget_realize(GTK_WIDGET(trayicon));
	gtk_window_set_default_size(GTK_WINDOW(trayicon), 16, 16);
	gtk_container_set_border_width(GTK_CONTAINER(trayicon), 0);

	trayicon_update_theme(NULL, NULL);

	eventbox = gtk_event_box_new();
	gtk_container_set_border_width(GTK_CONTAINER(eventbox), 0);
	gtk_container_add(GTK_CONTAINER(trayicon), GTK_WIDGET(eventbox));

	image = gtk_image_new_from_pixmap(nomail_pixmap[0], nomail_bitmap[0]);
	gtk_container_add(GTK_CONTAINER(eventbox), image);

	destroy_signal_id =
	g_signal_connect(G_OBJECT(trayicon), "destroy",
		G_CALLBACK(destroy_cb), NULL);
	g_signal_connect(GTK_OBJECT(trayicon), "size-request",
		G_CALLBACK(resize_cb), NULL);
	g_signal_connect(G_OBJECT(eventbox), "button-press-event",
		G_CALLBACK(click_cb), NULL);

#if !(GTK_CHECK_VERSION(2,12,0))
	tooltips = gtk_tooltips_new();
	gtk_tooltips_enable(tooltips);
#endif
	n_entries = sizeof(trayicon_popup_menu_entries) /
	sizeof(trayicon_popup_menu_entries[0]);
	traymenu_popup = menu_create_items(trayicon_popup_menu_entries,
						n_entries, "<TrayiconMenu>", &traymenu_factory,
						NULL);

	gtk_widget_show_all(GTK_WIDGET(trayicon));

	update(NULL);
}

int plugin_init(gchar **error)
{
	if (!check_plugin_version(MAKE_NUMERIC_VERSION(2,9,2,72),
				VERSION_NUMERIC, PLUGIN_NAME, error))
		return -1;

	item_hook_id = hooks_register_hook (FOLDER_ITEM_UPDATE_HOOKLIST, folder_item_update_hook, NULL);
	if (item_hook_id == -1) {
		*error = g_strdup(_("Failed to register folder item update hook"));
		goto err_out_item;
	}

	folder_hook_id = hooks_register_hook (FOLDER_UPDATE_HOOKLIST, folder_update_hook, NULL);
	if (folder_hook_id == -1) {
		*error = g_strdup(_("Failed to register folder update hook"));
		goto err_out_folder;
	}

	offline_hook_id = hooks_register_hook (OFFLINE_SWITCH_HOOKLIST, offline_update_hook, NULL);
	if (offline_hook_id == -1) {
		*error = g_strdup(_("Failed to register offline switch hook"));
		goto err_out_offline;
	}

	account_hook_id = hooks_register_hook (ACCOUNT_LIST_CHANGED_HOOKLIST, trayicon_set_accounts_hook, NULL);
	if (account_hook_id == -1) {
		*error = g_strdup(_("Failed to register account list changed hook"));
		goto err_out_account;
	}

	close_hook_id = hooks_register_hook (MAIN_WINDOW_CLOSE, trayicon_close_hook, NULL);
	if (close_hook_id == -1) {
		*error = g_strdup(_("Failed to register close hook"));
		goto err_out_close;
	}

	iconified_hook_id = hooks_register_hook (MAIN_WINDOW_GOT_ICONIFIED, trayicon_got_iconified_hook, NULL);
	if (iconified_hook_id == -1) {
		*error = g_strdup(_("Failed to register got iconified hook"));
		goto err_out_iconified;
	}

	theme_hook_id = hooks_register_hook(THEME_CHANGED_HOOKLIST, trayicon_update_theme, NULL);
	if (theme_hook_id == -1) {
		*error = g_strdup(_("Failed to register theme change hook"));
		goto err_out_theme;
	}

	create_trayicon();
	trayicon_set_accounts_hook(NULL, NULL);

	trayicon_prefs_init();

	if (trayicon_prefs.hide_at_startup && claws_is_starting()) {
		MainWindow *mainwin = mainwindow_get_mainwindow();

		if (GTK_WIDGET_VISIBLE(GTK_WIDGET(mainwin->window)))
			main_window_hide(mainwin);
		main_set_show_at_startup(FALSE);
	}

	return 0;

err_out_theme:
	hooks_unregister_hook(MAIN_WINDOW_GOT_ICONIFIED, iconified_hook_id);
err_out_iconified:
	hooks_unregister_hook(MAIN_WINDOW_CLOSE, close_hook_id);
err_out_close:
	hooks_unregister_hook(ACCOUNT_LIST_CHANGED_HOOKLIST, account_hook_id);
err_out_account:
	hooks_unregister_hook(OFFLINE_SWITCH_HOOKLIST, offline_hook_id);
err_out_offline:
	hooks_unregister_hook(FOLDER_UPDATE_HOOKLIST, folder_hook_id);
err_out_folder:
	hooks_unregister_hook(FOLDER_ITEM_UPDATE_HOOKLIST, item_hook_id);
err_out_item:
	return -1;
}

gboolean plugin_done(void)
{
	trayicon_prefs_done();

	hooks_unregister_hook(FOLDER_ITEM_UPDATE_HOOKLIST, item_hook_id);
	hooks_unregister_hook(FOLDER_UPDATE_HOOKLIST, folder_hook_id);
	hooks_unregister_hook(OFFLINE_SWITCH_HOOKLIST, offline_hook_id);
	hooks_unregister_hook(ACCOUNT_LIST_CHANGED_HOOKLIST, account_hook_id);
	hooks_unregister_hook(MAIN_WINDOW_CLOSE, close_hook_id);
	hooks_unregister_hook(MAIN_WINDOW_GOT_ICONIFIED, iconified_hook_id);
	hooks_unregister_hook(THEME_CHANGED_HOOKLIST, theme_hook_id);

	if (claws_is_exiting())
		return TRUE;

	g_signal_handler_disconnect(G_OBJECT(trayicon), destroy_signal_id);
	
	gtk_widget_destroy(GTK_WIDGET(trayicon));

	while (gtk_events_pending()) {
		gtk_main_iteration();
	}
	return TRUE;
}

const gchar *plugin_name(void)
{
	return PLUGIN_NAME;
}

const gchar *plugin_desc(void)
{
	return _("This plugin places a mailbox icon in the system tray that "
	         "indicates if you have new or unread mail.\n"
	         "\n"
	         "The mailbox is empty if you have no unread mail, otherwise "
	         "it contains a letter. A tooltip shows new, unread and total "
	         "number of messages.");
}

const gchar *plugin_type(void)
{
	return "GTK2";
}

const gchar *plugin_licence(void)
{
	return "GPL3+";
}

const gchar *plugin_version(void)
{
	return VERSION;
}


/* popup menu callbacks */
static void trayicon_get_all_cb( gpointer data, guint action, GtkWidget *widget )
{
	MainWindow *mainwin = mainwindow_get_mainwindow();
	inc_all_account_mail_cb(mainwin, 0, NULL);
}

static void trayicon_compose_cb( gpointer data, guint action, GtkWidget *widget )
{
	MainWindow *mainwin = mainwindow_get_mainwindow();
	compose_mail_cb(mainwin, 0, NULL);
}

static void trayicon_compose_acc_cb( GtkMenuItem *menuitem, gpointer data )
{
	compose_new((PrefsAccount *)data, NULL, NULL);
}

static void trayicon_addressbook_cb( gpointer data, guint action, GtkWidget *widget )
{
	addressbook_open(NULL);
}

static void trayicon_toggle_offline_cb( gpointer data, guint action, GtkWidget *widget )
{
	/* toggle offline mode if menu checkitem has been clicked */
	if (!updating_menu) {
		MainWindow *mainwin = mainwindow_get_mainwindow();
		main_window_toggle_work_offline(mainwin, !prefs_common.work_offline, TRUE);
	}
}

static void app_exit_cb(MainWindow *mainwin, guint action, GtkWidget *widget)
{
	if (prefs_common.confirm_on_exit) {
		if (alertpanel(_("Exit"), _("Exit Claws Mail?"),
			       GTK_STOCK_CANCEL, GTK_STOCK_OK,
			       NULL) != G_ALERTALTERNATE) {
			return;
		}
		manage_window_focus_in(mainwin->window, NULL, NULL);
	}

	app_will_exit(NULL, mainwin);
}

static void trayicon_exit_cb( gpointer data, guint action, GtkWidget *widget )
{
	MainWindow *mainwin = mainwindow_get_mainwindow();

	if (mainwin->lock_count == 0) {
		app_exit_cb(mainwin, 0, NULL);
	}
}

struct PluginFeature *plugin_provides(void)
{
	static struct PluginFeature features[] = 
		{ {PLUGIN_NOTIFIER, N_("Trayicon")},
		  {PLUGIN_NOTHING, NULL}};
	return features;
}
