/* gnc-plugin-page-report.c
 * Copyright (C) 2004 Joshua Sled <jsled@asynchronous.org>
 *
 * Originally from window-report.c:
 * Copyright (C) 1997 Robin D. Clark
 * Copyright (C) 1998 Linas Vepstas
 * Copyright (C) 1999 Jeremy Collins ( gtk-xmhtml port )
 * Copyright (C) 2000 Dave Peticolas
 * Copyright (C) 2000 Bill Gribble
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652
 * Boston, MA  02111-1307,  USA       gnu@gnu.org
 */

#include "config.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <g-wrap-wct.h>
#include <libguile.h>
#include <sys/stat.h>

#include "gnc-component-manager.h"
#include "gnc-engine.h"
#include "gnc-gconf-utils.h"
#include "gnc-gnome-utils.h"
#include "gnc-html-history.h"
#include "gnc-html.h"
#include "gnc-file.h"
#include "gnc-plugin.h"
#include "gnc-plugin-page-report.h"
#include "gnc-report.h"
#include "gnc-ui-util.h"
#include "gnc-ui.h"
#include "gnc-window.h"
#include "messages.h"
#include "option-util.h"

#define WINDOW_REPORT_CM_CLASS "window-report"

/* NW: you can add GNC_MOD_REPORT to gnc-engine.h
or simply define it locally. Any unique string with
a gnucash- prefix will do. Then just set a log level 
with gnc_set_log_level().*/
static QofLogModule log_module = GNC_MOD_GUI;

static GObjectClass *parent_class = NULL;

// Property-id values.
enum {
    PROP_0,
    PROP_REPORT_ID,
  };

static void gnc_plugin_page_report_class_init( GncPluginPageReportClass *klass );
static void gnc_plugin_page_report_init( GncPluginPageReport *plugin_page );
static GObject *gnc_plugin_page_report_constructor(GType this_type, guint n_properties, GObjectConstructParam *properties);
static void gnc_plugin_page_report_finalize (GObject *object);
static void gnc_plugin_page_report_setup( GncPluginPage *ppage );

static void gnc_plugin_page_report_constr_init(GncPluginPageReport *plugin_page, gint reportId);

static GtkWidget* gnc_plugin_page_report_create_widget( GncPluginPage *plugin_page );
static void gnc_plugin_page_report_destroy_widget( GncPluginPage *plugin_page );

static int gnc_plugin_page_report_check_urltype(URLType t);
static void gnc_plugin_page_report_load_cb(gnc_html * html, URLType type,
                                      const gchar * location, const gchar * label,
                                      gpointer data);
static void gnc_plugin_page_report_expose_event_cb(GtkWidget *unused, GdkEventExpose *unused1, gpointer data);
static void gnc_plugin_page_report_refresh (gpointer data);
static void gnc_plugin_page_report_set_fwd_button(GncPluginPageReport * page, int enabled);
static void gnc_plugin_page_report_set_back_button(GncPluginPageReport * page, int enabled);
static void gnc_plugin_page_report_history_destroy_cb(gnc_html_history_node * node, gpointer user_data);
static void close_handler(gpointer user_data);
void gnc_plugin_page_report_destroy(GncPluginPageReportPrivate * win);
static void gnc_plugin_page_report_option_change_cb(gpointer data);

void gnc_plugin_page_report_remove_edited_report(GncPluginPageReportPrivate * win, SCM report);
void gnc_plugin_page_report_add_edited_report(GncPluginPageReportPrivate * win, SCM report);
void gnc_plugin_page_report_raise_editor(SCM report);

static void gnc_plugin_page_report_forw_cb(GtkAction *action, GncPluginPageReport *rep);
static void gnc_plugin_page_report_back_cb(GtkAction *action, GncPluginPageReport *rep);
static void gnc_plugin_page_report_reload_cb(GtkAction *action, GncPluginPageReport *rep);
static void gnc_plugin_page_report_stop_cb(GtkAction *action, GncPluginPageReport *rep);
static void gnc_plugin_page_report_export_cb(GtkAction *action, GncPluginPageReport *rep);
static void gnc_plugin_page_report_options_cb(GtkAction *action, GncPluginPageReport *rep);
static void gnc_plugin_page_report_print_cb(GtkAction *action, GncPluginPageReport *rep);

struct GncPluginPageReportPrivate
{
        /// The report-id
        int reportId;

        /// The report which this Page is satisifying
        SCM cur_report;
        /// The Option DB for this report.
        GNCOptionDB *cur_odb;
        SCM option_change_cb_id;

        /* initial_report is special; it's the one that's saved and 
         * restored.  The name_change_callback only gets called when
         * the initial_report name is changed. */
        SCM          initial_report; 
        GNCOptionDB  * initial_odb;
        SCM          name_change_cb_id;

        /* keep a list of edited reports so that we can destroy them when
         * the window is closed. */
        SCM          edited_reports;

        /* This is set to mark the fact that we need to reload the html */
        gboolean	need_reload;

        /// the gnc_html abstraction this PluginPage contains
        gnc_html *html;

        /// the container the above HTML widget is in.
        GtkContainer *container;
};

GType
gnc_plugin_page_report_get_type (void)
{
	static GType gnc_plugin_page_report_type = 0;

	if (gnc_plugin_page_report_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (GncPluginPageReportClass),
			NULL,
			NULL,
			(GClassInitFunc) gnc_plugin_page_report_class_init,
			NULL,
			NULL,
			sizeof (GncPluginPageReport),
			0,
			(GInstanceInitFunc) gnc_plugin_page_report_init
		};
		
		gnc_plugin_page_report_type = g_type_register_static (GNC_TYPE_PLUGIN_PAGE,
                                                                      "GncPluginPageReport",
                                                                      &our_info, 0);
	}

	return gnc_plugin_page_report_type;
}

static void
gnc_plugin_page_report_get_property( GObject *obj,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec )
{
        GncPluginPageReport *rep;
        GncPluginPageReportPrivate * priv;

        rep = GNC_PLUGIN_PAGE_REPORT( obj );
        priv = (GncPluginPageReportPrivate*)rep->priv;
        
        switch ( prop_id )
        {
        case PROP_REPORT_ID:
                g_value_set_int( value, priv->reportId );
                break;
        default:
                PERR( "Unknown property id %d", prop_id );
                break;
        }
}

static void
gnc_plugin_page_report_set_property( GObject *obj,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec )
{
        GncPluginPageReport *rep;
        GncPluginPageReportPrivate *priv;

        rep = GNC_PLUGIN_PAGE_REPORT( obj );
        priv = (GncPluginPageReportPrivate*)rep->priv;

        DEBUG( "setting property with id %d / %p to value %d",
               prop_id, rep->priv, g_value_get_int( value ) );

        switch ( prop_id )
        {
        case PROP_REPORT_ID:
                priv->reportId = g_value_get_int( value );
                break;
        default:
                PERR( "unknown property id %d", prop_id );
                break;
        }
               
}

static void
gnc_plugin_page_report_class_init (GncPluginPageReportClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GncPluginPageClass *gnc_plugin_page_class = GNC_PLUGIN_PAGE_CLASS(klass);

	parent_class = g_type_class_peek_parent (klass);

        object_class->constructor = gnc_plugin_page_report_constructor;
	object_class->finalize = gnc_plugin_page_report_finalize;

        object_class->set_property = gnc_plugin_page_report_set_property;
        object_class->get_property = gnc_plugin_page_report_get_property;

        // FIXME: stock reporting icon?
	//gnc_plugin_page_class->tab_icon        = GNC_STOCK_ACCOUNT;
	gnc_plugin_page_class->plugin_name     = GNC_PLUGIN_PAGE_REPORT_NAME;

	gnc_plugin_page_class->create_widget   = gnc_plugin_page_report_create_widget;
	gnc_plugin_page_class->destroy_widget  = gnc_plugin_page_report_destroy_widget;

        // create the "reportId" property
        g_object_class_install_property( object_class,
                                         PROP_REPORT_ID,
                                         g_param_spec_int( "report-id",
                                                           _("The numeric ID of the report."),
                                                           _("The numeric ID of the report."),
                                                           -1, G_MAXINT, -1, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE ) );

/* JSLED: report-selected?
	plugin_page_signals[ACCOUNT_SELECTED] =
	  g_signal_new ("account_selected",
			G_OBJECT_CLASS_TYPE (object_class),
			G_SIGNAL_RUN_FIRST,
			G_STRUCT_OFFSET (GncPluginPageReportClass, account_selected),
			NULL, NULL,
			g_cclosure_marshal_VOID__POINTER,
			G_TYPE_NONE, 1,
			G_TYPE_POINTER);
*/
}

static void
gnc_plugin_page_report_finalize (GObject *object)
{
	GncPluginPageReport *page;
	GncPluginPageReportPrivate *priv;

	ENTER("object %p", object);
	page = GNC_PLUGIN_PAGE_REPORT (object);
	g_return_if_fail (GNC_IS_PLUGIN_PAGE_REPORT (page));
	priv = page->priv;
	g_return_if_fail (priv != NULL);

	G_OBJECT_CLASS (parent_class)->finalize (object);
	LEAVE(" ");
}

static
GtkWidget*
gnc_plugin_page_report_create_widget( GncPluginPage *page )
{
        GncPluginPageReport *gppReport;
        GncPluginPageReportPrivate *report;
        GtkWindow *topLvl;
        URLType type;
        char * id_name;
        char * child_name;
        char * url_location = NULL;
        char * url_label = NULL;

        gppReport = GNC_PLUGIN_PAGE_REPORT(page);

        report = (GncPluginPageReportPrivate*)gppReport->priv;
        topLvl = GTK_WINDOW(gnc_ui_get_toplevel());
        report->html              = gnc_html_new( topLvl );

        gnc_html_history_set_node_destroy_cb(gnc_html_get_history(report->html),
                                             gnc_plugin_page_report_history_destroy_cb,
                                             (gpointer)report);
  
        report->container = GTK_CONTAINER(gtk_frame_new(NULL));
        gtk_frame_set_shadow_type(GTK_FRAME(report->container), GTK_SHADOW_NONE);
  
        gtk_container_add(GTK_CONTAINER(report->container), 
                          gnc_html_get_widget(report->html));
  
        gnc_register_gui_component( WINDOW_REPORT_CM_CLASS, NULL,
                                    close_handler, report );
  
        gnc_html_set_urltype_cb(report->html, gnc_plugin_page_report_check_urltype);
        gnc_html_set_load_cb(report->html, gnc_plugin_page_report_load_cb, gppReport);

        // FIXME.  This is f^-1(f(x)), isn't it?
        DEBUG( "id=%d", report->reportId );
        id_name = g_strdup_printf("id=%d", report->reportId );
        child_name = gnc_build_url( URL_TYPE_REPORT, id_name, NULL );
        type = gnc_html_parse_url( report->html, child_name, &url_location, &url_label);
        DEBUG( "passing id_name=[%s] child_name=[%s] type=[%s], location=[%s], label=[%s]",
               id_name, child_name, type, url_location, url_label );

        gnc_window_set_progressbar_window( GNC_WINDOW(page->window) );
        gnc_html_show_url(report->html, type, url_location, url_label, 0);
        gnc_window_set_progressbar_window( NULL );

        gtk_signal_connect(GTK_OBJECT(report->container), "expose_event",
                           GTK_SIGNAL_FUNC(gnc_plugin_page_report_expose_event_cb), gppReport);
  
        gtk_widget_show_all( GTK_WIDGET(report->container) );

        return GTK_WIDGET( report->container );
}

/********************************************************************
 * gnc_plugin_page_report_check_urltype
 * is it OK to show a certain URLType in this window?
 ********************************************************************/
static int
gnc_plugin_page_report_check_urltype(URLType t)
{
  if (!safe_strcmp (t, URL_TYPE_REPORT)) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/**
 * Simply get the initial report given the id, so we can do initialization
 * things like setup the tab name based on the report's name.
 **/
static void
gnc_plugin_page_report_setup( GncPluginPage *ppage )
{
        GncPluginPageReport *page = GNC_PLUGIN_PAGE_REPORT(ppage);
        GncPluginPageReportPrivate *report = (GncPluginPageReportPrivate*)page->priv;
        SCM  find_report = scm_c_eval_string("gnc:find-report");
        SCM  set_needs_save = scm_c_eval_string("gnc:report-set-needs-save?!");
        SCM  inst_report;
        int  report_id;

        report->cur_report        = SCM_BOOL_F;
        report->initial_report    = SCM_BOOL_F;
        report->edited_reports    = SCM_EOL;
        report->name_change_cb_id = SCM_BOOL_F;

        scm_gc_protect_object(report->cur_report);
        scm_gc_protect_object(report->initial_report);
        scm_gc_protect_object(report->edited_reports);

        g_object_get( ppage, "report-id", &report_id, NULL );

        PINFO("report-id: %d\n", report_id);
        
        /* get the inst-report from the Scheme-side hash, and get its
         * options and editor thunk */
        if ((inst_report = scm_call_1(find_report, scm_int2num(report_id)))
            == SCM_BOOL_F) {
                return;
        }
        
        if (report->initial_report == SCM_BOOL_F) {
                scm_gc_unprotect_object(report->initial_report);
                report->initial_report = inst_report;
                scm_gc_protect_object(report->initial_report);
        }

        // all reports need [to be] saved immediately after they're created.
        PINFO("set needs save");
        scm_call_2(set_needs_save, inst_report, SCM_BOOL_T);
}

/********************************************************************
 * gnc_plugin_page_report_load_cb
 * called after a report is loaded into the gnc_html widget 
 ********************************************************************/
static void 
gnc_plugin_page_report_load_cb(gnc_html * html, URLType type, 
                               const gchar * location, const gchar * label, 
                               gpointer data)
{
        GncPluginPageReport *report = GNC_PLUGIN_PAGE_REPORT(data);
        GncPluginPageReportPrivate *win = report->priv;
        int  report_id;
        SCM  find_report    = scm_c_eval_string("gnc:find-report");
        SCM  get_options    = scm_c_eval_string("gnc:report-options");
        SCM  set_needs_save = scm_c_eval_string("gnc:report-set-needs-save?!");
        SCM  inst_report;

        ENTER( "load_cb: type=[%s], location=[%s], label=[%s]",
               type, location, label );

        /* we get this callback if a new report is requested to be loaded OR
         * if any URL is clicked.  If an options URL is clicked, we want to
         * know about it */
        if (!safe_strcmp (type, URL_TYPE_REPORT)
            && location
            && (strlen(location) > 3)
            && !strncmp("id=", location, 3)) {
                sscanf(location+3, "%d", &report_id);
                DEBUG( "parsed id=%d", report_id );
        }
        else if (!safe_strcmp( type, URL_TYPE_OPTIONS)
                 && location
                 && (strlen(location) > 10)
                 && !strncmp("report-id=", location, 10)) {
                sscanf(location+10, "%d", &report_id);
                inst_report = scm_call_1(find_report, scm_int2num(report_id));
                if (inst_report != SCM_BOOL_F) {
                        gnc_plugin_page_report_add_edited_report(win, inst_report);
                }
                return;
        } else {
                LEAVE( " unknown URL type [%s] location [%s]", type, location );
                return;
        }
        
        /* get the inst-report from the Scheme-side hash, and get its
         * options and editor thunk */
        if ((inst_report = scm_call_1(find_report, scm_int2num(report_id)))
            == SCM_BOOL_F) {
                LEAVE( "error getting inst_report" );
                return;
        }

        if (win->initial_report == SCM_BOOL_F) {
                scm_gc_unprotect_object(win->initial_report);
                win->initial_report = inst_report;
                scm_gc_protect_object(win->initial_report);
                
                DEBUG("calling set_needs_save for report with id=%d", report_id);
                scm_call_2(set_needs_save, inst_report, SCM_BOOL_T);
                
                win->initial_odb = gnc_option_db_new(scm_call_1(get_options, inst_report));  
                win->name_change_cb_id = 
                        gnc_option_db_register_change_callback(win->initial_odb,
                                                               gnc_plugin_page_report_refresh,
                                                               win,
                                                               "General", "Report name");
        }
        
        if ((win->cur_report != SCM_BOOL_F) && (win->cur_odb != NULL)) {
                gnc_option_db_unregister_change_callback_id(win->cur_odb,
                                                            win->option_change_cb_id);
                gnc_option_db_destroy(win->cur_odb);
                win->cur_odb = NULL;
        }
        
        if (win->cur_report != SCM_BOOL_F)
                scm_gc_unprotect_object(win->cur_report);
        win->cur_report = inst_report;
        scm_gc_protect_object(win->cur_report);
        
        win->cur_odb = gnc_option_db_new(scm_call_1(get_options, inst_report));  
        win->option_change_cb_id = 
                gnc_option_db_register_change_callback(win->cur_odb,
                                                       gnc_plugin_page_report_option_change_cb,
                                                       win, NULL, NULL);
        
        if (gnc_html_history_forward_p(gnc_html_get_history(win->html))) {
                gnc_plugin_page_report_set_fwd_button(report, TRUE); 
        } else {
                gnc_plugin_page_report_set_fwd_button(report, FALSE); 
        }
        
        if(gnc_html_history_back_p(gnc_html_get_history(win->html))) {
                gnc_plugin_page_report_set_back_button(report, TRUE); 
        } else {
                gnc_plugin_page_report_set_back_button(report, FALSE); 
        }

        LEAVE( "done" );
}

static void
gnc_plugin_page_report_option_change_cb(gpointer data)
{
        GncPluginPageReportPrivate * report = data;
        SCM               dirty_report = scm_c_eval_string("gnc:report-set-dirty?!");

        DEBUG( "option_change" );
        if (report->cur_report == SCM_BOOL_F)
                return;
        DEBUG( "set-dirty, queue-draw" );

        /* it's probably already dirty, but make sure */
        scm_call_2(dirty_report, report->cur_report, SCM_BOOL_T);
        
        /* Now queue the fact that we need to reload this report */
        report->need_reload = TRUE;
        // jsled: this doesn't seem to cause any effect.
        gtk_widget_queue_draw( GTK_WIDGET(report->container) );
        // jsled: this does.
        gnc_html_reload( report->html );
}

static void 
gnc_plugin_page_report_history_destroy_cb(gnc_html_history_node * node, 
                                          gpointer user_data)
{
        static SCM         remover = SCM_BOOL_F;
        int                report_id;
  
        if (remover == SCM_BOOL_F) {
                remover = scm_c_eval_string("gnc:report-remove-by-id");
        }
  
        if (node
            && !safe_strcmp (node->type, URL_TYPE_REPORT)\
            && !strncmp("id=", node->location, 3)) {
                sscanf(node->location+3, "%d", &report_id);
                /*    printf("unreffing report %d and children\n", report_id);
                      scm_call_1(remover, scm_int2num(report_id)); */
        }
        else {
                return;
        }
}

/* We got a draw event.  See if we need to reload the report */
static void
gnc_plugin_page_report_expose_event_cb(GtkWidget *unused, GdkEventExpose *unused1, gpointer data)
{
        GncPluginPageReport *page = data;
        GncPluginPageReportPrivate *win;

	g_return_if_fail(GNC_IS_PLUGIN_PAGE_REPORT(page));

	win = page->priv;
        ENTER( "report_draw" );
        if (!win->need_reload)
        {
                LEAVE( "no reload needed" );
                return;
        }

        win->need_reload = FALSE;
        gnc_window_set_progressbar_window( GNC_WINDOW(GNC_PLUGIN_PAGE(page)->window) );
        gnc_html_reload(win->html);
        gnc_window_set_progressbar_window( NULL );
        LEAVE( "reload forced" );
}

// @param data is actually GncPluginPageReportPrivate
static void
gnc_plugin_page_report_refresh(gpointer data)
{
        // FIXME?
        DEBUG( "report-refresh called" );
        // something like ... gnc_plugin_page_report_redraw( NULL, (GncPluginPageReportPrivate*)data );
        return;
}

static void
gnc_plugin_page_report_destroy_widget(GncPluginPage *plugin_page)
{
        // FIXME: cleanup other resources.
        static SCM         remover = SCM_BOOL_F;
        int                report_id;

        PINFO("destroy widget");
        if (remover == SCM_BOOL_F) {
                remover = scm_c_eval_string("gnc:report-remove-by-id");
        }

        report_id = GNC_PLUGIN_PAGE_REPORT(plugin_page)->priv->reportId;
        PINFO("unreffing report %d and children\n", report_id);
        scm_call_1(remover, scm_int2num(report_id));
}

/********************************************************************
 * gnc_report_window_destroy 
 * free and destroy a window 
 ********************************************************************/
void
gnc_plugin_page_report_destroy(GncPluginPageReportPrivate * win)
{
        SCM  get_editor = scm_c_eval_string("gnc:report-editor-widget");
        SCM  set_editor = scm_c_eval_string("gnc:report-set-editor-widget!");
        SCM  edited, editor; 

        gnc_unregister_gui_component_by_data (WINDOW_REPORT_CM_CLASS, win);

        /* close any open editors */
        for (edited = scm_list_copy(win->edited_reports); !SCM_NULLP(edited);
             edited = SCM_CDR(edited)) {
                editor = scm_call_1(get_editor, SCM_CAR(edited));
                scm_call_2(set_editor, SCM_CAR(edited), SCM_BOOL_F);
                if (editor != SCM_BOOL_F) {
                        gtk_widget_destroy(GTK_WIDGET(gw_wcp_get_ptr(editor)));
                }
        }

        if (win->initial_odb) {
                gnc_option_db_unregister_change_callback_id(win->initial_odb, 
                                                            win->name_change_cb_id);
    
                gnc_option_db_destroy(win->initial_odb);
                win->initial_odb = NULL;
        }

        gnc_html_destroy(win->html);
  
        win->container     = NULL;
        win->html          = NULL;
  
        scm_gc_unprotect_object(win->cur_report);
        scm_gc_unprotect_object(win->edited_reports);
  
        g_free(win);
}

static GtkActionEntry report_actions[] =
{
        { "FilePrintAction", GTK_STOCK_PRINT, N_("Print Report..."), NULL, NULL,
          G_CALLBACK(gnc_plugin_page_report_print_cb) },
        { "ReportExportAction", GTK_STOCK_CONVERT, N_("Export Report"), NULL, NULL,
          G_CALLBACK(gnc_plugin_page_report_export_cb) },
        { "ReportOptionsAction", GTK_STOCK_PROPERTIES, N_("Report Options"), NULL, NULL,
          G_CALLBACK(gnc_plugin_page_report_options_cb) },

        { "ReportBackAction", GTK_STOCK_GO_BACK, N_("Back"), NULL, NULL,
          G_CALLBACK(gnc_plugin_page_report_back_cb) },
        { "ReportForwAction", GTK_STOCK_GO_FORWARD, N_("Forward"), NULL, NULL,
          G_CALLBACK(gnc_plugin_page_report_forw_cb) },
        { "ReportReloadAction", GTK_STOCK_REFRESH, N_("Reload"), NULL, NULL,
          G_CALLBACK(gnc_plugin_page_report_reload_cb) },
        { "ReportStopAction", GTK_STOCK_STOP, N_("Stop"), NULL, NULL,
          G_CALLBACK(gnc_plugin_page_report_stop_cb) },
};
static guint num_report_actions = G_N_ELEMENTS( report_actions );

static action_short_labels short_labels[] = {
  { "FilePrintAction", 	    N_("Print") },
  { "ReportExportAction",   N_("Export") },
  { "ReportOptionsAction",  N_("Options") },
  { NULL, NULL },
};


static void
gnc_plugin_page_report_init ( GncPluginPageReport *plugin_page )
{
        plugin_page->priv = g_new0( GncPluginPageReportPrivate, 1 );
}

static GObject*
gnc_plugin_page_report_constructor(GType this_type, guint n_properties, GObjectConstructParam *properties)
{
        GObject *obj;
        GncPluginPageReportClass *our_class;
        GObjectClass *parent_class;
        gint reportId = -42;
        int i = 0;

        our_class = GNC_PLUGIN_PAGE_REPORT_CLASS (g_type_class_peek (GNC_TYPE_PLUGIN_PAGE_REPORT));
        parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (our_class));
        obj = parent_class->constructor(this_type, n_properties, properties);

        for (; i < n_properties; i++)
        {
                GObjectConstructParam prop = properties[i];
                if (strcmp(prop.pspec->name,"report-id") == 0)
                {
                        reportId = g_value_get_int(prop.value);
                }
        }
  
        gnc_plugin_page_report_constr_init(GNC_PLUGIN_PAGE_REPORT(obj), reportId);

        return obj;
}

static void
gnc_plugin_page_report_constr_init(GncPluginPageReport *plugin_page, gint reportId)
{
        GtkActionGroup *action_group;
        GncPluginPage *parent;
        GString *tmpStr;
        gboolean use_new;

        DEBUG( "property reportId=%d", reportId );
        plugin_page->priv->reportId = reportId;

        gnc_plugin_page_report_setup( GNC_PLUGIN_PAGE(plugin_page) );

        /* Init parent declared variables */
        parent = GNC_PLUGIN_PAGE(plugin_page);
        use_new = gnc_gconf_get_bool(GCONF_GENERAL_REPORT, KEY_USE_NEW, NULL);
        tmpStr = g_string_sized_new( 32 );
        g_string_sprintf( tmpStr, "%s: %s", _("Report"),
                          gnc_report_name( plugin_page->priv->initial_report ) );
	g_object_set(G_OBJECT(plugin_page),
		     "page-name",      tmpStr->str,
		     "page-uri",       "default:",
		     "ui-description", "gnc-plugin-page-report-ui.xml",
		     "use-new-window", use_new,
		     NULL);

        /* change me when the system supports multiple books */
        gnc_plugin_page_add_book(parent, gnc_get_current_book());

	/* Create menu and toolbar information */
	action_group =
	  gnc_plugin_page_create_action_group(parent,
					      "GncPluginPageReportActions");
	action_group = gnc_plugin_page_get_action_group(parent);
        gtk_action_group_add_actions( action_group,
                                      report_actions,
                                      num_report_actions,
                                      plugin_page );
        gnc_plugin_init_short_names (action_group, short_labels);
}

GncPluginPage*
gnc_plugin_page_report_new( int reportId )
{
	GncPluginPageReport *plugin_page;

        DEBUG( "report id = %d", reportId );
        plugin_page = g_object_new( GNC_TYPE_PLUGIN_PAGE_REPORT,
                                    "report-id", reportId, NULL );
        DEBUG( "plugin_page: %p", plugin_page );
        DEBUG( "set %d on page %p", reportId, plugin_page );
	return GNC_PLUGIN_PAGE( plugin_page );
}

void
gnc_plugin_page_report_remove_edited_report(GncPluginPageReportPrivate * win,
                                            SCM report)
{ 
        SCM new_edited = scm_delete(win->edited_reports, report);
        scm_gc_unprotect_object(win->edited_reports);
        win->edited_reports = new_edited;
        scm_gc_protect_object(win->edited_reports);
}

void
gnc_plugin_page_report_add_edited_report(GncPluginPageReportPrivate * win,
                                         SCM report)
{
        SCM new_edited = scm_cons(report, win->edited_reports);
        scm_gc_unprotect_object(win->edited_reports);
        win->edited_reports = new_edited;
        scm_gc_protect_object(win->edited_reports);
}

void
gnc_plugin_page_report_raise_editor(SCM report)
{
        SCM get_editor = scm_c_eval_string("gnc:report-editor-widget");
        SCM editor = scm_call_1(get_editor, report);
        gtk_window_present(gw_wcp_get_ptr(editor));
}

static void
close_handler (gpointer user_data)
{
        GncPluginPageReportPrivate *win = user_data;  
        DEBUG("in close handler\n");
        gnc_plugin_page_report_destroy (win);
}

static void
gnc_plugin_page_report_set_fwd_button(GncPluginPageReport *report, int enabled)
{
        GtkActionGroup *action_group;
        GValue value = { 0 };
        GtkAction *act;

	action_group = gnc_plugin_page_get_action_group(GNC_PLUGIN_PAGE(report));
        act = gtk_action_group_get_action( action_group, "ReportForwAction" );
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, enabled);
        g_object_set_property( G_OBJECT(act), "sensitive", &value );
}

static void
gnc_plugin_page_report_set_back_button(GncPluginPageReport *report, int enabled)
{
        GtkActionGroup *action_group;
        GValue value = { 0 };
        GtkAction *act;

	action_group = gnc_plugin_page_get_action_group(GNC_PLUGIN_PAGE(report));
        act = gtk_action_group_get_action( action_group, "ReportBackAction" );
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, enabled);
        g_object_set_property( G_OBJECT(act), "sensitive", &value );
}

// ------------------------------------------------------------
// GTK ACTION CALLBACKS

static void
gnc_plugin_page_report_forw_cb( GtkAction *action, GncPluginPageReport *report )
{
        gnc_html_history_node * node = NULL;

        DEBUG( "forw" );
        gnc_html_history_forward(gnc_html_get_history(report->priv->html));
        node = gnc_html_history_get_current(gnc_html_get_history(report->priv->html));
        if (node) {
                gnc_html_show_url(report->priv->html, node->type, node->location, 
                                  node->label, 0);
        }
}

static void
gnc_plugin_page_report_back_cb( GtkAction *action, GncPluginPageReport *report )
{
        gnc_html_history_node * node;
  
        DEBUG( "back" );
        gnc_html_history_back(gnc_html_get_history(report->priv->html));
        node = gnc_html_history_get_current(gnc_html_get_history(report->priv->html));
        if(node) {
                gnc_html_show_url(report->priv->html, node->type, node->location, 
                                  node->label, 0);
        }
}

static void
gnc_plugin_page_report_reload_cb( GtkAction *action, GncPluginPageReport *report )
{
        SCM dirty_report;

        DEBUG( "reload" );
        if (report->priv->cur_report == SCM_BOOL_F)
                return;

        DEBUG( "reload-redraw" );
        dirty_report = scm_c_eval_string("gnc:report-set-dirty?!");
        scm_call_2(dirty_report, report->priv->cur_report, SCM_BOOL_T);

        report->priv->need_reload = TRUE;
        /* now queue the fact that we need to reload this report */

        // this doens't seem to do anything...
        gtk_widget_queue_draw( GTK_WIDGET(report->priv->container) );

        // this does...
        gnc_html_reload( report->priv->html );
}

static void
gnc_plugin_page_report_stop_cb( GtkAction *action, GncPluginPageReport *report )
{
        gnc_html_cancel(report->priv->html);
}

/* Returns SCM_BOOL_F if cancel. Returns SCM_BOOL_T if html.
 * Otherwise returns pair from export_types. */
static SCM
gnc_get_export_type_choice (SCM export_types)
{
        GList * choices = NULL;
        gboolean bad = FALSE;
        GList * node;
        int choice;
        SCM tail;

        if (!SCM_LISTP (export_types))
                return SCM_BOOL_F;

        for (tail = export_types; !SCM_NULLP (tail); tail = SCM_CDR (tail))
        {
                SCM pair = SCM_CAR (tail);
                const gchar * name;
                SCM scm;

                if (!SCM_CONSP (pair))
                {
                        g_warning ("unexpected list element");
                        bad = TRUE;
                        break;
                }

                scm = SCM_CAR (pair);
                if (!SCM_STRINGP (scm))
                {
                        g_warning ("unexpected pair element");
                        bad = TRUE;
                        break;
                }

                name = SCM_STRING_CHARS (scm);
                choices = g_list_prepend (choices, g_strdup (name));
        }

        if (!bad)
        {
                choices = g_list_reverse (choices);

                choices = g_list_prepend (choices, g_strdup (_("HTML")));

                choice = gnc_choose_radio_option_dialog
                        (NULL, _("Choose export format"),
                         _("Choose the export format for this report:"), 0, choices);
        }
        else
                choice = -1;

        for (node = choices; node; node = node->next)
                g_free (node->data);
        g_list_free (choices);

        if (choice < 0)
                return SCM_BOOL_F;

        if (choice == 0)
                return SCM_BOOL_T;

        choice--;
        if (choice >= scm_ilength (export_types))
                return SCM_BOOL_F;

        return scm_list_ref (export_types, scm_int2num (choice));
}

static char *
gnc_get_export_filename (SCM choice)
{
        char * filepath;
        struct stat statbuf;
        char * title;
        const gchar * type;
        int rc;

        if (choice == SCM_BOOL_T)
                type = _("HTML");
        else
        {
                type = SCM_STRING_CHARS(SCM_CAR (choice));
        }

        /* %s is the type of what is about to be saved, e.g. "HTML". */
        title = g_strdup_printf (_("Save %s To File"), type);

        filepath = gnc_file_dialog (title, NULL, NULL, GNC_FILE_DIALOG_EXPORT);

        g_free (title);

        if (!filepath)
                return NULL;

        rc = stat (filepath, &statbuf);

        /* Check for an error that isn't a non-existant file. */
        if (rc != 0 && errno != ENOENT)
        {
                /* %s is the strerror(3) string of the error that occurred. */
                const char *format = _("You cannot save to that filename.\n\n%s");

                gnc_error_dialog (NULL, format, strerror(errno));
                g_free(filepath);
                return NULL;
        }

        /* Check for a file that isn't a regular file. */
        if (rc == 0 && !S_ISREG (statbuf.st_mode))
        {
                const char *message = _("You cannot save to that file.");

                gnc_error_dialog (NULL, message);
                g_free(filepath);
                return NULL;
        }

        if (rc == 0)
        {
                const char *format = _("The file \n    %s\n already exists.\n"
                                       "Are you sure you want to overwrite it?");

                if (!gnc_verify_dialog (NULL, FALSE, format, filepath)) {
                        g_free(filepath);
                        return NULL;
                }
        }

        return filepath;
}

static void
gnc_plugin_page_report_export_cb( GtkAction *action, GncPluginPageReport *report )
{
        GncPluginPageReportPrivate *priv = report->priv;
        char * filepath;
        SCM export_types;
        SCM export_thunk;
        gboolean result;
        SCM choice;

        export_types = scm_call_1 (scm_c_eval_string ("gnc:report-export-types"),
                                   priv->cur_report);

        export_thunk = scm_call_1 (scm_c_eval_string ("gnc:report-export-thunk"),
                                   priv->cur_report);

        if (SCM_LISTP (export_types) && SCM_PROCEDUREP (export_thunk))
                choice = gnc_get_export_type_choice (export_types);
        else
                choice = SCM_BOOL_T;

        if (choice == SCM_BOOL_F)
                return;

        filepath = gnc_get_export_filename (choice);
        if (!filepath)
                return;

        if (SCM_CONSP (choice))
        {
                SCM file_scm;
                SCM res;

                choice = SCM_CDR (choice);
                file_scm = scm_makfrom0str (filepath);

                res = scm_call_3 (export_thunk, priv->cur_report, choice, file_scm);

                result = (res != SCM_BOOL_F);
        }
        else
                result = gnc_html_export (priv->html, filepath);

        if (!result)
        {
                const char *fmt = _("Could not open the file\n"
                                    "     %s\n%s");
                gnc_error_dialog( NULL, fmt, filepath ? filepath : "(null)",
                                  strerror (errno) ? strerror (errno) : "" );
        }

        g_free(filepath);
        return;
}

static void
gnc_plugin_page_report_options_cb( GtkAction *action, GncPluginPageReport *report )
{
        GncPluginPageReportPrivate *priv = report->priv;
        SCM start_editor = scm_c_eval_string("gnc:report-edit-options");
        
        if (priv->cur_report == SCM_BOOL_F)
                return;

        if (scm_call_1(start_editor, priv->cur_report) == SCM_BOOL_F) {
                gnc_warning_dialog(GTK_WIDGET(gnc_ui_get_toplevel()),
                                   _("There are no options for this report."));
        }
        else {
                gnc_plugin_page_report_add_edited_report(priv, priv->cur_report);
        }
}

static void
gnc_plugin_page_report_print_cb( GtkAction *action, GncPluginPageReport *report )
{
        gnc_html_print(report->priv->html);
}

/********************************************************************
 * gnc_main_window_open_report()
 * open an report in a top level window from an ID number 
 ********************************************************************/

void
gnc_main_window_open_report(int report_id, GncMainWindow *window)
{
        GncPluginPage *reportPage;

        if (window)
                g_return_if_fail(GNC_IS_MAIN_WINDOW(window));

        reportPage = gnc_plugin_page_report_new( report_id );
        gnc_main_window_open_page( window, reportPage );
}

void
gnc_main_window_open_report_url(const char * url, GncMainWindow *window)
{
        GncPluginPage *reportPage;

        DEBUG( "report url: [%s]\n", url );

        if (window)
                g_return_if_fail(GNC_IS_MAIN_WINDOW(window));

        reportPage = gnc_plugin_page_report_new( 42 /* url? */ );
        gnc_main_window_open_page( window, reportPage );
}