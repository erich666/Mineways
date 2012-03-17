/*
Copyright (c) 2010, Sean Kasun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <dirent.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "MinutorMap/MinutorMap.h"
#include "minutor.xpm"
#include "colorschemes.h"

#define MINZOOM 1.0
#define MAXZOOM 10.0

static GtkWidget *win;
static GtkWidget *slider,*da,*status,*progressbar;
static GtkWidget *jumpplayer,*jumpspawn;
static GtkWidget *lighting, *cavemode, *hideobscured, *depthshading, *hell, *ender;
static GtkWidget *standard;
static double curX,curZ;
static int curDepth=255;
static double curScale=1.0;
static char *world=NULL;
static unsigned char *bits;
static int curWidth,curHeight;
static int spawnX,spawnY,spawnZ;
static int playerX,playerY,playerZ;
static long long randomSeed;
static gboolean mouseUp(GtkWidget *widget,GdkEventButton *event);

static void destroy()
{
	gtk_main_quit();
}

static void updateProgress(float progress)
{
	if (progress>1.0) progress=1.0;
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progressbar),progress);
}


static gboolean drawMap(GtkWidget *widget)
{
    // don't do anything if we haven't loaded a world
    if (world==NULL) return FALSE;
	// don't draw anything for a disabled widget
	if (!gtk_widget_get_sensitive(widget)) return FALSE;
	int w=da->allocation.width;
	int h=da->allocation.height;
	if (w!=curWidth || h!=curHeight)
	{
		curWidth=w;
		curHeight=h;
		bits=g_realloc(bits,curWidth*curHeight*4);
	}
	int opts=0;
	opts|=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(cavemode))?CAVEMODE:0;
	opts|=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(hideobscured))?HIDEOBSCURED:0;
    opts|=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(depthshading))?DEPTHSHADING:0;
	opts|=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(lighting))?LIGHTING:0;
	opts|=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(hell))?HELL:0;
	opts|=gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(ender))?ENDER:0;

	DrawMap(world,curX,curZ,curDepth,curWidth,curHeight,curScale,bits,opts,updateProgress);

	gdk_draw_rgb_32_image(
		da->window,
		da->style->white_gc,
		0,0,curWidth,curHeight,
		GDK_RGB_DITHER_NONE,
		bits,
		curWidth*4);
	updateProgress(0.0);
	return TRUE;
}

static gchar *getSliderText(GtkScale *scale,gdouble value)
{
	return g_strdup_printf("%d",255-(int)value);
}
static void adjustMap(GtkRange *range,gpointer user_data)
{
	curDepth=255-(int)gtk_range_get_value(range);
	gdk_window_invalidate_rect(GTK_WIDGET(user_data)->window,NULL,FALSE);
}
static gboolean tracking=FALSE;
static double oldX,oldY;
static gboolean mouseDown(GtkWidget *widget,GdkEventButton *event)
{
	gtk_widget_grab_focus(widget);
	oldX=event->x;
	oldY=event->y;
	tracking=TRUE;
	return TRUE;
}
static gboolean mouseUp(GtkWidget *widget,GdkEventButton *event)
{
	tracking=FALSE;
	return TRUE;
}
static gboolean mouseMove(GtkWidget *widget,GdkEventMotion *event)
{
	if (tracking)
	{
		curX+=(oldX-event->x)/curScale;
		curZ+=(oldY-event->y)/curScale;
		oldX=event->x;
		oldY=event->y;
		gdk_window_invalidate_rect(widget->window,NULL,FALSE);
	}
	int mx,mz;
	const char *blockLabel=IDBlock(event->x,event->y,curX,curZ,
		curWidth,curHeight,curScale,&mx,&mz);
	char *buf=g_strdup_printf("%d,%d %s",mx,mz,blockLabel);
	gtk_statusbar_pop(GTK_STATUSBAR(status),1);
	gtk_statusbar_push(GTK_STATUSBAR(status),1,buf);
	g_free(buf);
	return TRUE;
}
static gboolean mouseWheel(GtkWidget *widget,GdkEventScroll *event)
{
	if (event->direction==GDK_SCROLL_DOWN)
	{
		curScale-=0.2;
		if (curScale<MINZOOM) curScale=MINZOOM;
		gdk_window_invalidate_rect(widget->window,NULL,FALSE);
	}
	if (event->direction==GDK_SCROLL_UP)
	{
		curScale+=0.2;
		if (curScale>MAXZOOM) curScale=MAXZOOM;
		gdk_window_invalidate_rect(widget->window,NULL,FALSE);
	}
	return TRUE;
}
static int moving=0;
static gboolean keyDown(GtkWidget *widget,GdkEventKey *event)
{
	gboolean changed=FALSE;
	switch (event->keyval)
	{
		case GDK_Up:
		case GDK_w:
			moving|=1;
			break;
		case GDK_Down:
		case GDK_s:
			moving|=2;
			break;
		case GDK_Left:
		case GDK_a:
			moving|=4;
			break;
		case GDK_Right:
		case GDK_d:
			moving|=8;
			break;
		case GDK_Page_Up:
		case GDK_e:
			curScale+=0.5;
			if (curScale>MAXZOOM)
				curScale=MAXZOOM;
			changed=TRUE;
			break;
		case GDK_Page_Down:
		case GDK_q:
			curScale-=0.5;
			if (curScale<MINZOOM)
				curScale=MINZOOM;
			changed=TRUE;
			break;
	}
	if (moving!=0)
	{
		if (moving&1) //up
			curZ-=10.0/curScale;
		if (moving&2) //down
			curZ+=10.0/curScale;
		if (moving&4) //left
			curX-=10.0/curScale;
		if (moving&8) //right
			curX+=10.0/curScale;
		changed=TRUE;
	}
	if (changed)
	{
		gdk_window_invalidate_rect(widget->window,NULL,FALSE);
		return TRUE;
	}
	return FALSE;
}
static gboolean keyUp(GtkWidget *widget,GdkEventKey *event)
{
	switch (event->keyval)
	{
		case GDK_Up:
		case GDK_w:
			moving&=~1;
			break;
		case GDK_Down:
		case GDK_s:
			moving&=~2;
			break;
		case GDK_Left:
		case GDK_a:
			moving&=~4;
			break;
		case GDK_Right:
		case GDK_d:
			moving&=~8;
			break;
	}
	return FALSE;
}

static void loadMap(const gchar *path)
{
	//clear cache
	if (world!=NULL)
		g_free(world);
	CloseAll();
	world=g_strdup(path);
	GFile *file=g_file_new_for_path(path);
	char *title=g_file_get_basename(file);
	char *titlestr=g_strdup_printf("Minutor - %s",title);
	gtk_window_set_title(GTK_WINDOW(win),titlestr);
	g_free(titlestr);
	g_free(title);
	g_object_unref(file);

	GetSpawn(path,&spawnX,&spawnY,&spawnZ);
	GetPlayer(path,&playerX,&playerY,&playerZ);
	curX=spawnX;
	curZ=spawnZ;
	GetRandomSeed(path, &randomSeed);

	gtk_widget_set_sensitive(jumpspawn,TRUE);
	gtk_widget_set_sensitive(jumpplayer,TRUE);
	gtk_widget_set_sensitive(slider,TRUE);
	gtk_widget_set_sensitive(da,TRUE);
	gdk_window_invalidate_rect(da->window,NULL,FALSE);
}

static void reloadWorld(void* data)
{
    CloseAll();
}

static gchar *getSavePath()
{
	return g_strdup_printf("%s/.minecraft/saves/",g_get_home_dir());
}

static void openWorld(GtkMenuItem *menuItem,gpointer user_data)
{
	loadMap(user_data);
}

static void openCustom(GtkMenuItem *menuItem,gpointer user_data)
{
	GtkWidget *chooser=gtk_file_chooser_dialog_new("Open World",
		GTK_WINDOW(win),GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL,GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN,GTK_RESPONSE_ACCEPT,
		NULL);
	GtkFileFilter *filter=gtk_file_filter_new();
	gtk_file_filter_add_pattern(filter,"level.dat");
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser),filter);
	if (gtk_dialog_run(GTK_DIALOG(chooser))==GTK_RESPONSE_ACCEPT)
	{
		GFile *file=gtk_file_chooser_get_file(GTK_FILE_CHOOSER(chooser));
		GFile *parent=g_file_get_parent(file);
		if (parent!=NULL)
		{
			loadMap(g_file_get_path(parent));
			g_object_unref(parent);
		}
		else
			loadMap("/");
		g_object_unref(file);
	}
	gtk_widget_destroy(chooser);
}

static void jumpToSpawn(GtkMenuItem *menuItem,gpointer user_data)
{
	curX=spawnX;
	curZ=spawnZ;
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(hell)))
	{
		curX/=8.0;
		curZ/=8.0;
	}
	gdk_window_invalidate_rect(da->window,NULL,FALSE);
}

static void jumpToPlayer(GtkMenuItem *menuItem,gpointer user_data)
{
	curX=playerX;
	curZ=playerZ;
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(hell)))
	{
		curX/=8.0;
		curZ/=8.0;
	}
	gdk_window_invalidate_rect(da->window,NULL,FALSE);
}
static void toggleHell(GtkMenuItem *menuItem,gpointer user_data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(hell)))
	{
		curX/=8.0;
		curZ/=8.0;
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ender),FALSE);
	}
	else
	{
		curX*=8.0;
		curZ*=8.0;
	}
	CloseAll();
	gdk_window_invalidate_rect(da->window,NULL,FALSE);
}

static void toggleEnd(GtkMenuItem *menuItem,gpointer user_data)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(ender)))
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(hell),FALSE);
	CloseAll();
	gdk_window_invalidate_rect(da->window,NULL,FALSE);
}
void selectColorScheme(GtkMenuItem *menuItem,gpointer user_data)
{
	int id=GPOINTER_TO_INT(user_data);
	uint32_t *colors=getColorScheme(id);
	SetMapPalette(colors,256);
	gdk_window_invalidate_rect(da->window,NULL,FALSE);
}

void createMapViewer()
{
	//map window
	win=gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win),"Minutor");
	gtk_window_set_icon(GTK_WINDOW(win),gdk_pixbuf_new_from_xpm_data(icon));
	g_signal_connect(G_OBJECT(win),"destroy",
		G_CALLBACK(destroy),NULL);

	//main vbox
	GtkWidget *vbox=gtk_vbox_new(FALSE,0);
	gtk_container_add(GTK_CONTAINER(win),vbox);

	//menu bar
	GtkWidget *menubar=gtk_menu_bar_new();
	gtk_box_pack_start(GTK_BOX(vbox),menubar,FALSE,FALSE,0);
	GtkAccelGroup *menuGroup=gtk_accel_group_new();
	//file menu
	GtkWidget *filemenu=gtk_menu_item_new_with_mnemonic("_File");
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar),filemenu);
	GtkWidget *fileitems=gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(filemenu),fileitems);

	GtkWidget *openworld=gtk_menu_item_new_with_label("Open World");
	gtk_menu_shell_append(GTK_MENU_SHELL(fileitems),openworld);
	GtkWidget *openitems=gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(openworld),openitems);

    gchar *save_dir = getSavePath();
    DIR* save_dirp = opendir(save_dir);

    int n = 0;
    if (save_dirp) 
    {
        gchar *level_dat;
        struct dirent *world_dir;
        while ((world_dir = readdir(save_dirp)) != NULL) {
            level_dat = g_strdup_printf("%s%s/level.dat", save_dir, world_dir->d_name);
            if (g_file_test(level_dat, G_FILE_TEST_IS_REGULAR)) {
                GtkWidget *w=gtk_menu_item_new_with_label(world_dir->d_name);
                if (n < 9) {
                    gtk_widget_add_accelerator(w,"activate",menuGroup,
                        GDK_1+n,GDK_CONTROL_MASK,GTK_ACCEL_VISIBLE);
                    n++;
                }
                gtk_menu_shell_append(GTK_MENU_SHELL(openitems),w);
                g_signal_connect(G_OBJECT(w),"activate",
                    G_CALLBACK(openWorld),g_strdup_printf("%s%s", save_dir, world_dir->d_name));
            }
            g_free(level_dat);
        }

        closedir(save_dirp);
    }

    g_free(save_dir);

	GtkWidget *open=gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN,menuGroup);
	gtk_menu_shell_append(GTK_MENU_SHELL(fileitems),open);
	g_signal_connect(G_OBJECT(open),"activate",
		G_CALLBACK(openCustom),NULL);

    GtkWidget *reload=gtk_image_menu_item_new_with_mnemonic("_Reload");
    gtk_widget_add_accelerator(reload,"activate",menuGroup,
            GDK_R,GDK_CONTROL_MASK,GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(fileitems),reload);
    g_signal_connect(G_OBJECT(reload),"activate",
        G_CALLBACK(reloadWorld),NULL);

	GtkWidget *sep=gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(fileitems),sep);

	GtkWidget *quit=gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT,menuGroup);
	gtk_menu_shell_append(GTK_MENU_SHELL(fileitems),quit);
	g_signal_connect(G_OBJECT(quit),"activate",
		G_CALLBACK(destroy),NULL);

	GtkWidget *viewmenu=gtk_menu_item_new_with_mnemonic("_View");
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar),viewmenu);
	GtkWidget *viewitems=gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(viewmenu),viewitems);

	jumpspawn=gtk_menu_item_new_with_label("Jump to Spawn");
	gtk_widget_add_accelerator(jumpspawn,"activate",menuGroup,
		GDK_F2,0,GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),jumpspawn);
	g_signal_connect(G_OBJECT(jumpspawn),"activate",
		G_CALLBACK(jumpToSpawn),NULL);
	gtk_widget_set_sensitive(jumpspawn,FALSE);

	jumpplayer=gtk_menu_item_new_with_label("Jump to Player");
	gtk_widget_add_accelerator(jumpplayer,"activate",menuGroup,
		GDK_F3,0,GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),jumpplayer);
	g_signal_connect(G_OBJECT(jumpplayer),"activate",
		G_CALLBACK(jumpToPlayer),NULL);
	gtk_widget_set_sensitive(jumpplayer,FALSE);
	
	GtkWidget *sep2=gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),sep2);

	hell=gtk_check_menu_item_new_with_label("Nether");
	gtk_widget_add_accelerator(hell,"activate",menuGroup,
		GDK_F5,0,GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),hell);
	g_signal_connect(G_OBJECT(hell),"activate",
		G_CALLBACK(toggleHell),NULL);

	ender=gtk_check_menu_item_new_with_label("End");
	gtk_widget_add_accelerator(ender,"activate",menuGroup,
		GDK_F6,0,GTK_ACCEL_VISIBLE);
	gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),ender);
	g_signal_connect(G_OBJECT(ender),"activate",
		G_CALLBACK(toggleEnd),NULL);
	GtkWidget *sep3=gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),sep3);


	GtkWidget *colormenu=gtk_menu_item_new_with_mnemonic("_Colors");
	gtk_menu_shell_append(GTK_MENU_SHELL(menubar),colormenu);
	GtkWidget *coloritems=gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(colormenu),coloritems);

	standard=gtk_radio_menu_item_new_with_label(NULL,"Standard");
	GSList *group=gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(standard));
	gtk_menu_shell_append(GTK_MENU_SHELL(coloritems),standard);
	g_signal_connect(G_OBJECT(standard),"activate",
		G_CALLBACK(selectColorScheme),GINT_TO_POINTER(-1));
	GtkWidget *sep4=gtk_separator_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(coloritems),sep4);

	GtkWidget *colorschemes=gtk_menu_item_new_with_label("Color Schemes...");
	gtk_menu_shell_append(GTK_MENU_SHELL(coloritems),colorschemes);
	g_signal_connect(G_OBJECT(colorschemes),"activate",
		G_CALLBACK(editColorSchemes),NULL);

	gtk_window_add_accel_group(GTK_WINDOW(win),menuGroup);

	//control hbox
	GtkWidget *hbox=gtk_hbox_new(FALSE,5);
	gtk_box_pack_start(GTK_BOX(vbox),hbox,FALSE,TRUE,0);

	//slider
	slider=gtk_hscale_new_with_range(0.0,255.0,1.0);
	gtk_widget_set_sensitive(slider,FALSE);
	gtk_box_pack_start(GTK_BOX(hbox),slider,TRUE,TRUE,0);
	g_signal_connect(G_OBJECT(slider),"format-value",
		G_CALLBACK(getSliderText),NULL);

	//map
	da=gtk_drawing_area_new();
	gtk_widget_set_sensitive(da,FALSE);
	curWidth=496;
	curHeight=400;
	gtk_drawing_area_size(GTK_DRAWING_AREA(da),curWidth,curHeight);
	gtk_box_pack_start(GTK_BOX(vbox),da,TRUE,TRUE,0);
	gtk_widget_add_events(da,GDK_SCROLL_MASK|GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|GDK_KEY_PRESS_MASK);
	g_signal_connect(G_OBJECT(da),"expose-event",
		G_CALLBACK(drawMap),NULL);
	g_signal_connect(G_OBJECT(da),"button-press-event",
		G_CALLBACK(mouseDown),NULL);
	g_signal_connect(G_OBJECT(da),"button-release-event",
		G_CALLBACK(mouseUp),NULL);
	g_signal_connect(G_OBJECT(da),"motion-notify-event",
		G_CALLBACK(mouseMove),NULL);
	g_signal_connect(G_OBJECT(da),"scroll-event",
		G_CALLBACK(mouseWheel),NULL);
	g_signal_connect(G_OBJECT(da),"key-press-event",
		G_CALLBACK(keyDown),NULL);
	g_signal_connect(G_OBJECT(da),"key-release-event",
		G_CALLBACK(keyUp),NULL);
	gtk_widget_set_can_focus(da,TRUE);
	gtk_widget_grab_focus(da);

	g_signal_connect(G_OBJECT(slider),"value-changed",
		G_CALLBACK(adjustMap),G_OBJECT(da));

    //view menu > rendering options
	lighting=gtk_check_menu_item_new_with_mnemonic("_Lighting");
	gtk_widget_add_accelerator(lighting,"activate",menuGroup,
		GDK_1,0,GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),lighting);
	g_signal_connect(G_OBJECT(lighting),"toggled",
		G_CALLBACK(drawMap),NULL);

		
    cavemode=gtk_check_menu_item_new_with_mnemonic("_Cave Mode");
    gtk_widget_add_accelerator(cavemode,"activate",menuGroup,
        GDK_2,0,GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),cavemode);
    g_signal_connect(G_OBJECT(cavemode),"toggled",
        G_CALLBACK(drawMap),NULL);

    hideobscured=gtk_check_menu_item_new_with_mnemonic("Hide _Obscured");
    gtk_widget_add_accelerator(hideobscured,"activate",menuGroup,
        GDK_3,0,GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),hideobscured);
    g_signal_connect(G_OBJECT(hideobscured),"toggled",
        G_CALLBACK(drawMap),NULL);

    depthshading=gtk_check_menu_item_new_with_mnemonic("_Depth Shading");
    gtk_widget_add_accelerator(depthshading,"activate",menuGroup,
        GDK_4,0,GTK_ACCEL_VISIBLE);
    gtk_menu_shell_append(GTK_MENU_SHELL(viewitems),depthshading);
    g_signal_connect(G_OBJECT(depthshading),"toggled",
        G_CALLBACK(drawMap),NULL);

	//statusbar
	status=gtk_statusbar_new();
	gtk_box_pack_end(GTK_BOX(vbox),status,FALSE,TRUE,0);
	//progressbar
	progressbar=gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(status),progressbar,FALSE,TRUE,0);


	bits=g_malloc(curWidth*curHeight*4);

	// 2 = position to start inserting color schemes
	initColorSchemes(GTK_MENU_SHELL(coloritems),1,group);

	//and show it
	gtk_widget_show_all(win);
}
